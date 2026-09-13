#!/usr/bin/env python3
"""Train FIADA's tiny MLP driver with episodic CEM reinforcement learning."""
import argparse, json
from pathlib import Path
import numpy as np

CONTROL=np.array([[-650,-350],[-390,-390],[-140,-320],[80,-410],[330,-330],[610,-370],
 [760,-250],[725,-90],[580,-25],[340,-110],[110,-35],[-105,-125],[-300,-45],
 [-365,90],[-260,185],[-35,125],[185,65],[405,145],[590,105],[665,225],
 [560,355],[320,415],[70,330],[-180,420],[-455,360],[-690,295],[-785,145],
 [-745,-75],[-705,-260]],dtype=np.float64)
def catmull(p0,p1,p2,p3,t):
 t2=t*t;t3=t2*t
 return .5*((2*p1)+(-p0+p2)*t+(2*p0-5*p1+4*p2-p3)*t2+(-p0+3*p1-3*p2+p3)*t3)
COURSE=np.vstack([catmull(CONTROL[(i-1)%len(CONTROL)],CONTROL[i],CONTROL[(i+1)%len(CONTROL)],CONTROL[(i+2)%len(CONTROL)],t)
 for i in range(len(CONTROL)) for t in np.linspace(0,1,20,endpoint=False)])
TANG=np.roll(COURSE,-1,axis=0)-COURSE
TANG/=np.linalg.norm(TANG,axis=1,keepdims=True)
N=len(COURSE); OBS=8; H=12; OUT=3; PARAMS=OBS*H+H+H*OUT+OUT

def unpack(w):
 a=OBS*H;b=a+H;c=b+H*OUT
 return w[:,:a].reshape(-1,OBS,H),w[:,a:b],w[:,b:c].reshape(-1,H,OUT),w[:,c:]
def evaluate(w,steps=1100):
 pop=len(w); W1,b1,W2,b2=unpack(w)
 x=np.full(pop,COURSE[0,0]);y=np.full(pop,COURSE[0,1]);ang=np.full(pop,np.arctan2(TANG[0,1],TANG[0,0]))
 speed=np.zeros(pop);vy=np.zeros(pop);yaw=np.zeros(pop);steer=np.zeros(pop);idx=np.zeros(pop,dtype=np.int32)
 score=np.zeros(pop); alive=np.ones(pop,dtype=bool); offroad_steps=np.zeros(pop,dtype=np.int32)
 for _ in range(steps):
  d2=(COURSE[None,:,0]-x[:,None])**2+(COURSE[None,:,1]-y[:,None])**2
  newidx=np.argmin(d2,axis=1)
  delta=(newidx-idx+N//2)%N-N//2; delta=np.clip(delta,-3,12); idx=newidx
  target=(idx+14)%N; tangent=TANG[idx]; future=TANG[target]
  desired=np.arctan2(COURSE[target,1]-y,COURSE[target,0]-x)
  err=(desired-ang+np.pi)%(2*np.pi)-np.pi
  normal=np.stack([-tangent[:,1],tangent[:,0]],axis=1)
  lateral=np.sum((np.stack([x,y],1)-COURSE[idx])*normal,axis=1)
  curve=np.arctan2(tangent[:,0]*future[:,1]-tangent[:,1]*future[:,0],np.sum(tangent*future,axis=1))
  road=np.abs(lateral)<68
  obs=np.stack([np.sin(err),np.cos(err),np.clip(lateral/68,-2,2),speed/45,vy/15,yaw/2,curve,road],1)
  hidden=np.tanh(np.einsum('pi,pih->ph',obs,W1)+b1)
  out=np.einsum('ph,pho->po',hidden,W2)+b2
  throttle=1/(1+np.exp(-out[:,0])); targetsteer=np.tanh(out[:,1]); drift=1/(1+np.exp(-out[:,2]))
  steer+=(targetsteer-steer)*.45
  grip=np.where(road,1.0,.34)*np.where(drift>.62,.78,1)
  acceleration=throttle*13.0-0.006*speed*speed-np.where(road,.7,5.0)
  speed=np.clip(speed+acceleration*.05,0,62)
  wantedYaw=speed*np.tan(steer*.42)/2.6
  yaw+=(wantedYaw-yaw)*np.minimum(1,.05*(3.5+grip*7))
  vy+=(yaw*speed*.12-vy*(1.4*grip))*0.05
  ang+=yaw*.05
  x+=(np.cos(ang)*speed-np.sin(ang)*vy)*.05*14
  y+=(np.sin(ang)*speed+np.cos(ang)*vy)*.05*14
  offroad_steps=np.where(road,0,offroad_steps+1)
  escaped=(x<-760)|(x>760)|(y<-500)|(y>500)|(offroad_steps>24)
  newly_dead=alive & escaped
  alive &= ~escaped
  score+=alive*(delta*4.0+speed*.006-road*.22-np.abs(err)*.012)
  score-=newly_dead*30
  speed*=alive
 return score

def emit(best,outdir):
 W1,b1,W2,b2=unpack(best[None,:])
 payload={'obs':OBS,'hidden':H,'out':OUT,'weights':best.tolist()}
 (outdir/'trained_policy.json').write_text(json.dumps(payload,indent=2),encoding='utf-8')
 vals=',\n    '.join(', '.join(f'{v:.8f}F' for v in best[i:i+8]) for i in range(0,len(best),8))
 header=f'''#pragma once
#include <array>
namespace fiada::policy {{
inline constexpr int kObservations={OBS}, kHidden={H}, kOutputs={OUT};
inline constexpr std::array<float,{PARAMS}> kWeights{{{{
    {vals}
}}}};
}}  // namespace fiada::policy
'''
 (outdir/'trained_policy.hpp').write_text(header,encoding='utf-8')

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--generations',type=int,default=90);ap.add_argument('--population',type=int,default=96)
 ap.add_argument('--seed',type=int,default=260912);ap.add_argument('--output',type=Path,default=Path('src'));a=ap.parse_args()
 rng=np.random.default_rng(a.seed); mean=rng.normal(0,.04,PARAMS)
 # Procedural imitation warm-start; final selection and refinement use only returns.
 W1,b1,W2,b2=unpack(mean[None,:])
 samples=12000
 obs=rng.uniform(-1,1,(samples,OBS)); obs[:,1]=np.sqrt(np.maximum(0,1-obs[:,0]**2))
 obs[:,2]*=1.4; obs[:,3]=rng.uniform(0,1.25,samples); obs[:,4:6]*=.8; obs[:,6]*=.8; obs[:,7]=1
 steer=np.clip(1.75*obs[:,0]-1.25*obs[:,2]-.28*obs[:,5]+1.4*obs[:,6],-.92,.92)
 desired_speed=np.clip(.92-1.15*np.abs(obs[:,6])-.34*np.abs(obs[:,0]),.25,.95)
 throttle=np.clip(2.8+7.0*(desired_speed-obs[:,3]),-5,5)
 target=np.stack([throttle,np.arctanh(steer),np.full(samples,-4.5)],1)
 lr=.018
 for epoch in range(700):
  h=np.tanh(obs@W1[0]+b1[0]); out=h@W2[0]+b2[0]; d=(out-target)/samples
  gW2=h.T@d; gb2=d.sum(0); dh=(d@W2[0].T)*(1-h*h)
  gW1=obs.T@dh; gb1=dh.sum(0)
  W2[0]-=lr*gW2; b2[0]-=lr*gb2; W1[0]-=lr*gW1; b1[0]-=lr*gb1
 std=np.full(PARAMS,.035); elite=max(6,a.population//8)
 best=None;bestscore=-1e30
 for g in range(a.generations):
  sample=mean+rng.normal(size=(a.population,PARAMS))*std
  scores=evaluate(sample); order=np.argsort(scores)[-elite:]
  if scores[order[-1]]>bestscore: bestscore=float(scores[order[-1]]);best=sample[order[-1]].copy()
  mean=.82*mean+.18*sample[order].mean(0);std=np.maximum(.055,.88*std+.12*sample[order].std(0))
  if g%10==0 or g+1==a.generations: print(f'generation={g+1} reward={bestscore:.2f} elite={scores[order].mean():.2f}',flush=True)
 a.output.mkdir(parents=True,exist_ok=True);emit(best,a.output);print(f'exported reward={bestscore:.2f} parameters={PARAMS}')
if __name__=='__main__': main()

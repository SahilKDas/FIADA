#include "game.hpp"
#include "neural_policy.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numeric>
#include <random>
#include <string_view>
#include <vector>

namespace {
using fiada::policy::Observation;
using fiada::policy::Output;
constexpr float dt=1.0F/120.0F;
float sigmoid(float x){return 1.0F/(1.0F+std::exp(-std::clamp(x,-12.0F,12.0F)));}
float logit(float x){x=std::clamp(x,.015F,.985F);return std::log(x/(1.0F-x));}

fiada::Input expert(const Observation& o){
  const float curvature=std::max({std::abs(o[6]),std::abs(o[16]),std::abs(o[20]),std::abs(o[24]),std::abs(o[28])});
  const float desired=std::clamp(55.0F-38.0F*curvature-7.0F*std::min(std::abs(o[2]),1.0F),15.0F,58.0F);
  const float speed=o[3]*45.0F;
  const float mainSteer=1.72F*o[0]-1.18F*o[2]-.31F*o[5]+.46F*o[14]+.26F*o[18];
  const float steer=std::clamp(o[13]>.20F?1.65F*o[30]-.22F*o[5]:mainSteer,-1.0F,1.0F);
  const float drive=std::clamp((desired-speed)/9.0F,-1.0F,1.0F);
  const bool inventory=(o[10]+o[11]+o[12])>.5F;
  return {.throttle=std::max(drive,0.0F),.brake=std::max(-drive,0.0F),.steer=steer,
          .reset=false,.drift=curvature>.13F && speed>14.0F && o[8]<.40F && o[9]<.05F,
          .toggleAi=false,.useItem=inventory && o[13]>.24F};
}
Output target(const fiada::Input& a){
  return {logit(a.throttle),logit(a.brake),std::atanh(std::clamp(a.steer,-.98F,.98F)),a.drift?3.2F:-3.2F,a.useItem?3.4F:-3.4F};
}
fiada::Input decode(const Output& o,const Observation& obs){
  const float throttle=sigmoid(o[0]),brake=sigmoid(o[1]);
  float steer=std::tanh(o[2]);
  if((obs[10]+obs[11]+obs[12])<.5F&&obs[13]>.18F)steer=std::lerp(steer,std::clamp(1.65F*obs[30]-.22F*obs[5],-1.0F,1.0F),.72F);
  return {.throttle=throttle,.brake=brake*(1.0F-throttle),.steer=steer,
          .reset=false,.drift=sigmoid(o[3])>.40F,.toggleAi=false,.useItem=(o[10]+o[11]+o[12])>.5F && o[13]>.18F && (o[13]>.55F || sigmoid(o[4])>.05F)};
}
struct Controller{
  fiada::policy::State state{}; Output cached{}; int tick{};
  fiada::Input act(const Observation& obs,const std::vector<float>& weights){
    if((tick++&3)==0)cached=fiada::policy::forward(obs,weights,state);
    return decode(cached,obs);
  }
};
void learnHead(std::vector<float>& weights,int episodes,int seconds,float rate,unsigned seedBase){
  for(int episode=0;episode<episodes;++episode){
    fiada::Game game(false);game.beginTrainingEpisode(seedBase+episode*7919U);
    fiada::policy::State state{};
    for(int step=0;step<seconds*120 && !game.trainingTerminal();++step){
      const auto obs=game.observation();
      if((step&3)==0){
        std::array<float,fiada::policy::kDense> dense{};
        const auto prediction=fiada::policy::forward(obs,weights,state,&dense);
        const auto wanted=target(expert(obs));
        for(int output=0;output<fiada::policy::kOutputs;++output){
          const float emphasis=(output==4&&wanted[output]>0.0F)?14.0F:(output==3?2.5F:(output==2?1.7F:1.0F));
          const float error=emphasis*std::clamp(prediction[output]-wanted[output],-4.0F,4.0F);
          for(int d=0;d<fiada::policy::kDense;++d)
            weights[fiada::policy::kOutputOffset+d*fiada::policy::kOutputs+output]-=rate*error*dense[d];
          weights[fiada::policy::kOutputBiasOffset+output]-=rate*error;
        }
      }
      game.update(dt,expert(obs));
    }
  }
}
void learnItems(std::vector<float>& weights,int rounds,float rate){
  for(int round=0;round<rounds;++round)for(int item=1;item<=2;++item){
    fiada::Game game(false);game.beginItemTrainingEpisode(item,810000U+round*101U+item);
    fiada::policy::State state{};
    // Balance the single edge-triggered item press against the many post-use frames.
    const auto entryObs=game.observation();
    for(int repeat=0;repeat<48;++repeat){
      fiada::policy::State entryState{};std::array<float,fiada::policy::kDense> dense{};
      const auto prediction=fiada::policy::forward(entryObs,weights,entryState,&dense);const float error=std::clamp(prediction[4]-3.4F,-4.0F,4.0F);
      for(int d=0;d<fiada::policy::kDense;++d)weights[fiada::policy::kOutputOffset+d*fiada::policy::kOutputs+4]-=.00018F*error*dense[d];
      weights[fiada::policy::kOutputBiasOffset+4]-=.00018F*error;
    }
    for(int step=0;step<14*120&&!game.trainingTerminal();++step){
      const auto obs=game.observation();
      if((step&3)==0){
        std::array<float,fiada::policy::kDense> dense{};const auto prediction=fiada::policy::forward(obs,weights,state,&dense);const auto wanted=target(expert(obs));
        for(int output=4;output<fiada::policy::kOutputs;++output){
          const float emphasis=output==4?18.0F:(output==2?2.5F:1.0F);const float error=emphasis*std::clamp(prediction[output]-wanted[output],-4.0F,4.0F);
          for(int d=0;d<fiada::policy::kDense;++d)weights[fiada::policy::kOutputOffset+d*fiada::policy::kOutputs+output]-=rate*error*dense[d];
          weights[fiada::policy::kOutputBiasOffset+output]-=rate*error;
        }
      }
      game.update(dt,expert(obs));
    }
  }
}
struct Metrics{int completed{},terminals{},mini{},pickups{},uses{},cuts{};std::array<int,3>itemTypes{},cutTypes{};double offroad{};std::vector<float>laps;};
Metrics evaluate(const std::vector<float>& weights,int episodes=33,int seconds=90){
  Metrics m;
  for(int episode=0;episode<episodes;++episode){
    fiada::Game game(false);game.beginTrainingEpisode(episode==0?0:900000+episode*7919U);Controller controller;
    for(int step=0;step<seconds*120&&!game.trainingTerminal()&&game.laps()==0;++step)
      game.update(dt,controller.act(game.observation(),weights));
    m.completed+=game.laps()>0;m.terminals+=game.trainingTerminal();m.mini+=game.miniTurbos();m.pickups+=game.itemPickups();m.uses+=game.itemUses();m.cuts+=game.shortcutsTaken();m.offroad+=game.offroadTime();
    for(int i=0;i<3;++i){m.itemTypes[i]+=game.itemPickups(i+1);m.cutTypes[i]+=game.shortcutsTaken(i+1);}if(game.laps()>0)m.laps.push_back(game.bestLap());
    if(episode==0)std::cout<<"canonical_lap="<<(game.laps()>0)<<" checkpoint="<<game.checkpoint()<<" lap_seconds="<<game.bestLap()<<"\n";
  }
  // Held-out capability trials verify both deterministic replacement items.
  for(int item=1;item<=2;++item){
    fiada::Game game(false);game.beginItemTrainingEpisode(item,990000U+item*1777U);Controller controller;
    for(int step=0;step<20*120&&!game.trainingTerminal();++step)game.update(dt,controller.act(game.observation(),weights));
    m.uses+=game.itemUses();m.cuts+=game.shortcutsTaken();m.cutTypes[item-1]+=game.shortcutsTaken(item);
    std::cout<<"item_trial="<<item<<" uses="<<game.itemUses()<<" offroad_cuts="<<game.shortcutsTaken(item)<<" checkpoint="<<game.checkpoint()<<"\n";
  }
  return m;
}
void print(const Metrics&m){
  auto laps=m.laps;std::sort(laps.begin(),laps.end());const float best=laps.empty()?0:laps.front(),median=laps.empty()?0:laps[laps.size()/2];
  std::cout<<"holdout_laps="<<m.completed<<"/33 terminal_escapes="<<m.terminals<<"/33 best_lap="<<best<<" median_lap="<<median<<" offroad_seconds="<<m.offroad
           <<" mini_turbos="<<m.mini<<" item_pickups="<<m.pickups<<" item_uses="<<m.uses<<" offroad_cuts="<<m.cuts
           <<" pickup_types="<<m.itemTypes[0]<<","<<m.itemTypes[1]<<","<<m.itemTypes[2]<<" cut_types="<<m.cutTypes[0]<<","<<m.cutTypes[1]<<","<<m.cutTypes[2]<<"\n";
}
}
int main(int argc,char**argv){
  const bool evaluation=argc>2&&std::string_view(argv[1])=="--evaluate";
  const bool calibration=argc>2&&std::string_view(argv[1])=="--calibrate-items";
  const std::filesystem::path model=(evaluation||calibration)?argv[2]:(argc>1?argv[1]:"assets/policy/fiada_policy.bin");
  std::vector<float> weights;
  if(evaluation){if(!fiada::policy::load(model,weights))return 2;print(evaluate(weights));return 0;}
  if(calibration){if(!fiada::policy::load(model,weights))return 2;learnItems(weights,18,.000025F);if(!fiada::policy::save(model,weights))return 2;print(evaluate(weights));return 0;}
  if(!fiada::policy::load(model,weights))fiada::policy::initialize(weights,0xF1ADAU);
  for(int epoch=0;epoch<12;++epoch){learnHead(weights,4,30,.00010F/(1.0F+epoch*.12F),10000U+epoch*100003U);std::cout<<"imitation_epoch="<<epoch+1<<"\n";}
  // Exact-simulation DAgger refinement: label randomized production states with the expert.
  for(int epoch=0;epoch<6;++epoch){learnHead(weights,3,36,.000045F,700000U+epoch*30011U);std::cout<<"refinement_epoch="<<epoch+1<<"\n";}
  for(int round=0;round<12;++round){learnItems(weights,2,.000065F);std::cout<<"item_curriculum="<<round+1<<"\n";}
  if(!fiada::policy::save(model,weights))return 2;
  std::cout<<"wrote_parameters="<<weights.size()<<" model_bytes="<<fiada::policy::kModelBytes<<"\n";print(evaluate(weights));return 0;
}
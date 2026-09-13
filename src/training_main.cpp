#include "game.hpp"
#include "trained_policy.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <numeric>
#include <random>
#include <string_view>
#include <vector>

namespace {
constexpr std::size_t P = fiada::policy::kWeights.size();
fiada::Input act(const std::array<float,fiada::policy::kObservations>& obs, const std::array<float,P>& w) {
  std::array<float,fiada::policy::kHidden> h{}; std::size_t k=0;
  for(int i=0;i<fiada::policy::kObservations;++i) for(int j=0;j<fiada::policy::kHidden;++j) h[j]+=obs[i]*w[k++];
  for(float& v:h) v=std::tanh(v+w[k++]);
  std::array<float,fiada::policy::kOutputs> o{};
  for(int i=0;i<fiada::policy::kHidden;++i) for(int j=0;j<fiada::policy::kOutputs;++j) o[j]+=h[i]*w[k++];
  for(float& v:o) v+=w[k++];
  const auto sigmoid=[](float x){return 1.0F/(1.0F+std::exp(-x));};
  float longitudinal=std::tanh(o[0]);
  const float desiredSpeed=7.0F+43.0F*std::exp(-5.2F*std::abs(obs[6]))
                          -5.0F*std::min(std::abs(obs[2]),1.0F);
  if(obs[3]*45.0F>desiredSpeed)
    longitudinal=-std::clamp((obs[3]*45.0F-desiredSpeed)/12.0F,0.25F,1.0F);
  else if(std::abs(obs[2])>.82F) longitudinal=std::min(longitudinal,.35F);
  return {.throttle=std::max(longitudinal,0.0F),.brake=std::max(-longitudinal,0.0F),
          .steer=std::clamp(std::lerp(std::tanh(o[1]),
            std::clamp(1.8F*obs[0]-1.25F*obs[2]+1.4F*obs[6]-0.3F*obs[5],-1.0F,1.0F),
            std::clamp(.18F+.38F*std::max(std::abs(obs[0]),std::abs(obs[2])),.18F,.72F)),-1.0F,1.0F),
          .reset=false,
          .drift=(sigmoid(o[2])>.48F || (std::abs(obs[6])>.13F && obs[3]>.30F)) && obs[9]<.05F && obs[8]<.36F,.toggleAi=false,
          .useItem=(obs[10]+obs[11]+obs[12])>.5F && (obs[13]>.28F || sigmoid(o[3])>.72F)};
}
float evaluate(const std::array<float,P>& weights, std::uint32_t baseSeed, bool randomized=true) {
  float total=0; const std::uint32_t episodes=randomized?3:1;
  for(std::uint32_t episode=0;episode<episodes;++episode) {
    fiada::Game game(false); game.beginTrainingEpisode(episode == 0 ? 0 : baseSeed+episode*7919);
    for(int step=0;step<120*42 && !game.trainingTerminal();++step) {
      game.update(1.0F/120.0F,act(game.observation(),weights));
      total+=game.trainingReward();
    }
    total+=game.laps()*700.0F;
  }
  return total/static_cast<float>(episodes);
}
}
int main(int argc,char** argv){
  if (argc > 2 && std::string_view(argv[1]) == "--evaluate") {
    std::array<float,P> weights{};
    std::ifstream input(argv[2], std::ios::binary);
    input.read(reinterpret_cast<char*>(weights.data()), sizeof(weights));
    if (!input) return 2;
    int completed=0, terminals=0, totalMiniTurbos=0, pickups=0, uses=0, cuts=0; long long driftSteps=0, turboSteps=0;
    for (std::uint32_t episode=0; episode<33; ++episode) {
      fiada::Game game(false); game.beginTrainingEpisode(episode==0?0:900000+episode*7919);
      for(int step=0;step<120*90 && !game.trainingTerminal() && game.laps()==0;++step)
        { const auto command=act(game.observation(),weights); driftSteps += command.drift; game.update(1.0F/120.0F,command); turboSteps += game.turboTime()>0.0F; }
      completed += game.laps()>0; terminals += game.trainingTerminal(); totalMiniTurbos += game.miniTurbos(); pickups+=game.itemPickups(); uses+=game.itemUses(); cuts+=game.shortcutsTaken();
      if(episode==0) std::cout<<"canonical_lap="<<(game.laps()>0)<<" checkpoint="<<game.checkpoint()<<"\n";
    }
    std::cout<<"holdout_laps="<<completed<<"/33 terminal_escapes="<<terminals<<"/33 mini_turbos="<<totalMiniTurbos<<" drift_seconds="<<driftSteps/120.0<<" turbo_seconds="<<turboSteps/120.0<<" item_pickups="<<pickups<<" item_uses="<<uses<<" shortcuts="<<cuts<<"\n";
    return completed>0 ? 0 : 3;
  }
  std::filesystem::path output=argc>1?argv[1]:"assets/policy/fiada_policy.bin";
  std::mt19937 rng(0xF1ADA); std::normal_distribution<float> normal;
  std::array<float,P> mean=fiada::policy::kWeights, deviation{};
  { std::ifstream prior(output, std::ios::binary | std::ios::ate);
    if (prior && prior.tellg() == static_cast<std::streamoff>(sizeof(mean))) { prior.seekg(0); prior.read(reinterpret_cast<char*>(mean.data()), sizeof(mean)); }
    else { mean.fill(0.0F); mean[P-3]=1.5F; mean[P-1]=-1.25F; }
  }
  deviation.fill(.018F);
  constexpr int population=24, elite=5, generations=30;
  std::array<float,P> best=mean; float bestScore=evaluate(best,1000,true);
  struct Candidate{std::array<float,P>w;float score;};
  std::vector<Candidate> candidates(population);
  for(int generation=0;generation<generations;++generation){
    for(int n=0;n<population;++n){
      for(std::size_t i=0;i<P;++i)candidates[n].w[i]=mean[i]+normal(rng)*deviation[i];
      candidates[n].score=evaluate(candidates[n].w,100000+generation*101,true);
    }
    std::partial_sort(candidates.begin(),candidates.begin()+elite,candidates.end(),
      [](const auto&a,const auto&b){return a.score>b.score;});
    if(candidates[0].score>bestScore){bestScore=candidates[0].score;best=candidates[0].w;}
    for(std::size_t i=0;i<P;++i){
      float m=0;for(int e=0;e<elite;++e)m+=candidates[e].w[i];m/=elite;
      float variance=0;for(int e=0;e<elite;++e){float d=candidates[e].w[i]-m;variance+=d*d;}
      mean[i]=.78F*mean[i]+.22F*m;
      deviation[i]=std::max(.0025F,.86F*deviation[i]+.14F*std::sqrt(variance/elite));
    }
    if(generation%5==0||generation+1==generations)
      std::cout<<"generation="<<generation+1<<" native_reward="<<bestScore<<"\n";
  }
  std::filesystem::create_directories(output.parent_path());
  std::ofstream stream(output,std::ios::binary);
  stream.write(reinterpret_cast<const char*>(best.data()),sizeof(best));
  if(!stream) return 2;
  std::cout<<"wrote "<<P<<" parameters to "<<output<<" reward="<<bestScore<<"\n";
  return 0;
}

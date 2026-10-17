#pragma once
#include "g1_clock_prediction.hpp"
namespace v12 {
// Last actually delivered physical model. Extrapolation supplies local HOLD
// presentation only, with UNKNOWN error bound. It never enters admission,
// replay validation, downstream publication or relative-lock eligibility.
struct G1RetainedEstimate {
  G1ClockPrediction clock{};
  uint64_t root=0,boot=0,observed_us=0,original20=0;
  uint32_t source=0;
  void clear(){*this={};}
  bool retain(const G1ClockPrediction& next,uint64_t origin,uint64_t origin_boot,
              uint32_t epoch,uint64_t original,uint64_t now){
    if(!origin||!origin_boot||!epoch||!original||!next.tick||next.tick<original||next.at(next.tick)<=0)return false;
    clock=next;root=origin;boot=origin_boot;source=epoch;observed_us=now;original20=original;return true;
  }
  bool deliver(uint64_t tick,uint64_t& utc_us)const{
    if(!root||!clock.tick||tick<clock.tick)return false;
    const auto ns=clock.at(tick);if(ns<=0)return false;utc_us=uint64_t(ns)/1000;return true;
  }
};
static_assert(sizeof(G1RetainedEstimate)<=112,"Retained estimate RAM bound");
}

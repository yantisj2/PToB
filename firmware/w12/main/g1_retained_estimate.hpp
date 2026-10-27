#pragma once
#include "g1_clock_prediction.hpp"
namespace v12 {
// Last actually delivered physical model. Extrapolation supplies local HOLD
// presentation with UNKNOWN error bound. N30 may reuse its actuator after a
// fresh authenticated physical pair; it grants no admission, replay validity,
// downstream publication or relative-lock eligibility.
struct G1RetainedEstimate {
  G1ClockPrediction clock{};
  uint64_t root=0,boot=0,observed_us=0,original20=0;
  uint32_t source=0;
  bool trained=false;
  void clear(){*this={};}
  bool retain(const G1ClockPrediction& next,uint64_t origin,uint64_t origin_boot,
              uint32_t epoch,uint64_t original,uint64_t now,bool frequency_trained=false){
    if(!origin||!origin_boot||!epoch||!original||!next.tick||next.tick<original||next.at(next.tick)<=0)return false;
    clock=next;root=origin;boot=origin_boot;source=epoch;observed_us=now;original20=original;trained=frequency_trained;return true;
  }
  // Actuator continuity only. The original physical observation, not a JOIN
  // or polling time, bounds reuse to30s in the same continuous local counter.
  bool handoff_ready(uint64_t tick)const{
    return trained&&root&&boot&&source&&original20&&clock.tick&&tick>=clock.tick&&
      tick>=original20&&tick-original20<=UINT64_C(600000000)&&
      clock.output_correction_ns==0&&clock.receiver_phase_ns==0&&clock.at(tick)>0;
  }
  bool deliver(uint64_t tick,uint64_t& utc_us)const{
    if(!root||!clock.tick||tick<clock.tick)return false;
    const auto ns=clock.at(tick);if(ns<=0)return false;utc_us=uint64_t(ns)/1000;return true;
  }
};
static_assert(sizeof(G1RetainedEstimate)<=112,"Retained estimate RAM bound");
}

#pragma once
#include "g1_clock_prediction.hpp"
namespace v12 {
// Causal relative forecast with the same actuator as the delivered map.
// This estimates agreement with captured GM ticks. Fixed RF/channel/propagation
// bias, UTC/source error and physical output error remain unqualified.
struct G1RelativeQuality {
  uint64_t original20=0,evaluated20=0;
  uint32_t samples=0,rms_ns=UINT32_MAX,max_ns=UINT32_MAX;
  uint32_t scope_generation=1;
  double rate_guard_ppb=0;
  double accel_guard_ppb_s=0;
  bool ready()const{return scope_generation&&original20&&samples>=16&&rms_ns!=UINT32_MAX&&
    max_ns!=UINT32_MAX&&
    std::isfinite(rate_guard_ppb)&&rate_guard_ppb>0&&std::isfinite(accel_guard_ppb_s)&&accel_guard_ppb_s>=0;}
  double phase_left(uint64_t now,const G1ClockPrediction& clock)const{
    if(!clock.tick||now<clock.tick)return INFINITY;
    // Keep fractional nanoseconds conservative, as in the original forecast.
    return std::max(0.0,std::abs(double(clock.phase))-double(now-clock.tick)/20000000*clock.phase_slew_ns_s());
  }
  uint32_t legacy_sum_estimate(uint64_t now,const G1ClockPrediction& clock)const{
    if(!ready()||!clock.tick||now<clock.tick||now<original20)return UINT32_MAX;
    const double value=3.0*rms_ns+50+phase_left(now,clock)+double(now-original20)/20000000*rate_guard_ppb;
    return std::isfinite(value)?uint32_t(std::min<double>(UINT32_MAX,std::ceil(value))):UINT32_MAX;
  }
  uint32_t estimate(uint64_t now,const G1ClockPrediction& clock)const{
    if(!ready()||!clock.tick||now<clock.tick||now<original20)return UINT32_MAX;
    // Full causal errors describe prior delivered models; the current phase
    // can be a new error component. The C2 field audit disproved treating them
    // as one MAX envelope: three displayed frozen horizons crossed1us.
    // Retain the recent worst tail and conservatively add current phase.
    // No covariance, calibrated bound or future independence is assumed.
    const double envelope=std::max(3.0*rms_ns,double(max_ns));
    const double age=double(now-original20)/20000000;
    const double value=envelope+50+phase_left(now,clock)+age*rate_guard_ppb+.5*age*age*accel_guard_ppb_s;
    return std::isfinite(value)?uint32_t(std::min<double>(UINT32_MAX,std::ceil(value))):UINT32_MAX;
  }
  uint32_t hold_ms(uint64_t now,const G1ClockPrediction& clock,uint64_t replay_left_us)const{
    const auto error=estimate(now,clock);
    if(error>=1000)return 0;
    // A projection, never an empirical P99 or a new permission deadline.
    const double age=double(now-original20)/20000000;
    const double slope=rate_guard_ppb+age*accel_guard_ppb_s;
    const double remaining=999.0-error;
    const double budget=accel_guard_ppb_s>0?2000*remaining/(slope+std::sqrt(slope*slope+2*accel_guard_ppb_s*remaining)):remaining*1000/slope;
    return uint32_t(std::max(0.0,std::min<double>(std::min<uint64_t>(UINT32_MAX,replay_left_us/1000),std::floor(budget))));
  }
  bool locked(uint64_t now,const G1ClockPrediction& clock)const{
    // Existing development screens; no change to strict precision/PTS gates.
    return ready()&&rms_ns<=400&&max_ns<1000&&estimate(now,clock)<1000;
  }
};
}

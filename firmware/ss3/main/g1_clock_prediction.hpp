#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#ifdef PTOB_W12_SHADOW_HOLDOVER
#include "w12_thermal_projection.hpp"
#endif
namespace v12 {
// Continuous20MHz software-clock actuator. Large acquisition phase uses at
// most50ppm; N30 and W12 fine builds enter1ppm slew below2us; legacy builds within1us. Frequency remains
// within±50ppm. Neither software steering nor lock grants RF calibration.
struct G1ClockPrediction {
  uint64_t tick=0;int64_t epoch=0,phase=0;double rate=0;int64_t utc_offset=0;
  int32_t output_correction_ns=0; // Snapshot-only untested experiment; raw fitter always zero.
  int32_t receiver_phase_ns=0; // Delivered-copy adjustment; raw oscillator history remains unshifted.
#ifdef PTOB_W12_SHADOW_HOLDOVER
  W12ThermalProjection thermal{};
#endif
  double phase_slew_ns_s()const {
    const double magnitude=std::abs(double(phase));
#if defined(PTOB_N30) || defined(PTOB_W12_FINE_CLOCK) || defined(PTOB_SS3_SERVO)
    return magnitude<2000?1000:std::min(50000.0,4*magnitude);
#else
    return magnitude<=1000?1000:std::min(50000.0,4*magnitude);
#endif
  }
  int64_t phase_slewed(uint64_t now)const {
    if(!tick||now<tick)return 0;
    return int64_t(std::copysign(std::min(std::abs(double(phase)),
      double(now-tick)/20000000*phase_slew_ns_s()),double(phase)));
  }
  int64_t phase_remaining(uint64_t now)const{return phase-phase_slewed(now);}
  int64_t relative_at(uint64_t now)const {
    if(!tick||now<tick||now-tick>uint64_t(INT64_MAX)/50||!std::isfinite(rate)||std::abs(rate)>50000)return 0;
    const auto dt=now-tick;const double seconds=double(dt)/20000000;
    const double slew=std::copysign(std::min(std::abs(double(phase)),seconds*phase_slew_ns_s()),double(phase));
    const int64_t elapsed=int64_t(dt)*50,steer=int64_t(std::llround(seconds*rate+slew));
    if(epoch<0||elapsed>INT64_MAX-epoch)return 0;
    const int64_t nominal=epoch+elapsed;
    if((steer>0&&nominal>INT64_MAX-steer)||(steer<0&&nominal<-steer))return 0;
    const auto projected=nominal+steer;
    if((receiver_phase_ns>0&&projected>INT64_MAX-receiver_phase_ns)||
       (receiver_phase_ns<0&&projected<=-int64_t(receiver_phase_ns)))return 0;
    const auto delivered=projected+receiver_phase_ns;
#ifdef PTOB_W12_SHADOW_HOLDOVER
    int64_t correction=0;if(!thermal.correction(now,correction))return 0;
    if((correction>0&&delivered>INT64_MAX-correction)||(correction<0&&delivered<=-correction))return 0;
    return delivered+correction;
#else
    return delivered;
#endif
  }
#ifdef PTOB_W12_HOLDOVER_PRODUCTION
  // Seed the current authenticated servo from the clock actually delivered in
  // HOLD. Preserve its target and fitted rate; existing bounded phase slew
  // removes the residual without a step. No source permission is changed.
  bool resume_from(const G1ClockPrediction& held,uint64_t now,int32_t delivered_receiver_ns){
    const auto target=relative_at(now),held_utc=held.at(now);
    if(target<=0||held_utc<=utc_offset||output_correction_ns||receiver_phase_ns)return false;
    const auto value=held_utc-utc_offset-delivered_receiver_ns;
    if(value<=0||std::abs(double(target-value))>1000000)return false;
    const auto remaining=phase_remaining(now);
    tick=now;epoch=value;phase=target-value+remaining;thermal={};return true;
  }
#endif
  int64_t at(uint64_t now)const {
    const auto relative=relative_at(now);
    if(relative<=0||utc_offset<0||relative>INT64_MAX-utc_offset)return 0;
    const auto raw=relative+utc_offset;
    if(output_correction_ns<0||raw>INT64_MAX-output_correction_ns)return 0;
    return raw+output_correction_ns;
  }
};
}

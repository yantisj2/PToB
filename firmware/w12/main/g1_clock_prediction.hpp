#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
namespace v12 {
// Continuous20MHz software-clock actuator. Large acquisition phase uses at
// most50ppm; within1us retain the original1ppm fine slew. Frequency remains
// within±50ppm. Neither software steering nor lock grants RF calibration.
struct G1ClockPrediction {
  uint64_t tick=0;int64_t epoch=0,phase=0;double rate=0;int64_t utc_offset=0;
  int32_t output_correction_ns=0; // Snapshot-only untested experiment; raw fitter always zero.
  double phase_slew_ns_s()const {
    const double magnitude=std::abs(double(phase));
    return magnitude<=1000?1000:std::min(50000.0,4*magnitude);
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
    return nominal+steer;
  }
  int64_t at(uint64_t now)const {
    const auto relative=relative_at(now);
    if(relative<=0||utc_offset<0||relative>INT64_MAX-utc_offset)return 0;
    const auto raw=relative+utc_offset;
    if(output_correction_ns<0||raw>INT64_MAX-output_correction_ns)return 0;
    return raw+output_correction_ns;
  }
};
}

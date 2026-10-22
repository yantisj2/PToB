#pragma once
#include <cstdint>

namespace v12 {
// Pure policy only. The platform supplies explicit fault/stalled-worker bits,
// requests the existing immediate drain, records restart evidence and performs
// the supported reset. The120s fault alias ignores ordinary source loss;
// N30 supplies that loss separately to its300s instance.
template<uint64_t Delay> class ContinuousFaultWatchdogModel {
 public:
  static constexpr uint64_t DelayUs=Delay;

  void observe(uint64_t now,uint32_t mask){
    check_time(now);
    current_mask_=mask;
    if(!mask){active_=false;first_mask_=0;start_us_=0;return;}
    if(!active_){active_=true;first_mask_=mask;start_us_=now;}
  }

  bool due(uint64_t now)const{
    // Subtraction is safe even near UINT64_MAX; no absolute deadline is added.
    return active_&&!restart_taken_&&!timing_fault_&&seen_time_&&
      now>=last_us_&&now>=start_us_&&now-start_us_>=DelayUs;
  }

  bool take_restart(uint64_t now){
    check_time(now);
    if(!due(now))return false;
    restart_taken_=true;
    return true;
  }

  uint32_t current_mask()const{return current_mask_;}
  uint32_t first_mask()const{return first_mask_;}
  uint64_t start_us()const{return start_us_;}
  bool active()const{return active_;}
  bool timing_fault()const{return timing_fault_;}
  bool restart_taken()const{return restart_taken_;}

 private:
  void check_time(uint64_t now){
    // A regressed/wrapped time cannot prove a full watchdog interval. This diagnostic
    // is sticky until construction of a fresh model; healthy input still
    // clears its fault interval. A separate platform/hardware watchdog remains
    // responsible for a broken monotonic clock or a stalled supervisor.
    if(seen_time_&&now<last_us_)timing_fault_=true;
    seen_time_=true;last_us_=now;
  }
  uint64_t start_us_=0,last_us_=0;
  uint32_t current_mask_=0,first_mask_=0;
  bool active_=false,seen_time_=false,timing_fault_=false,restart_taken_=false;
};
using FaultWatchdogModel=ContinuousFaultWatchdogModel<UINT64_C(120000000)>;
static_assert(sizeof(FaultWatchdogModel)<=32,"Fault watchdog model exceeds fixed RAM budget");
}

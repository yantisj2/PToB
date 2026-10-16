#pragma once
#include <algorithm>
#include <cstdint>
namespace v12 {
// Presentation only. Raw causal errors and model/authority gates are unchanged.
// Improvements must survive two seconds. Reductions in the held budget and
// worsening RMS take effect immediately; the displayed horizon counts down.
class G1QualityDisplay {
  static constexpr uint64_t Hz=20000000,Settle=2*Hz;
  uint64_t last_=0,rms_since_=0,hold_since_=0,lock_since_=0,hold_until_=0;
  uint32_t generation_=0,rms_=UINT32_MAX,pending_rms_=0,pending_hold_=0;
  bool rms_pending_=false,hold_pending_=false,lock_pending_=false,locked_=false;
 public:
  struct View {uint32_t rms_ns=UINT32_MAX,hold_ms=0;bool locked=false;};
  uint8_t pending()const{return uint8_t(rms_pending_)|(uint8_t(hold_pending_)<<1)|(uint8_t(lock_pending_)<<2);}
  void clear(){*this={};}
  View update(uint64_t now,uint32_t generation,uint32_t rms,uint32_t hold,bool lock_candidate){
    if(!now||!generation||rms==UINT32_MAX){clear();return {};}
    if(generation_!=generation||(last_&&now<last_)){clear();generation_=generation;}
    last_=now;
    if(rms>=rms_||rms_==UINT32_MAX){rms_=rms;rms_pending_=false;}
    else {
      if(!rms_pending_){rms_pending_=true;pending_rms_=rms;rms_since_=now;}
      else pending_rms_=std::max(pending_rms_,rms);
      if(now-rms_since_>=Settle){rms_=pending_rms_;rms_pending_=false;}
    }
    uint32_t remaining=hold_until_>now?uint32_t(std::min<uint64_t>(UINT32_MAX,(hold_until_-now)/(Hz/1000))):0;
    if(hold<=remaining){remaining=hold;hold_pending_=false;}
    else {
      if(!hold_pending_){hold_pending_=true;pending_hold_=hold;hold_since_=now;}
      else pending_hold_=std::min(pending_hold_,hold);
      if(now-hold_since_>=Settle){
        // At most four seconds of upward movement per settled two-second
        // interval, clipped to the current raw projection in every update.
        // This exceeds the simultaneous countdown so a stable model can
        // accumulate a useful horizon instead of oscillating at zero.
        remaining=std::min<uint32_t>(std::min(hold,pending_hold_),remaining>UINT32_MAX-4000?UINT32_MAX:remaining+4000);
        hold_pending_=false;
      }
    }
    const auto duration=uint64_t(remaining)*(Hz/1000);
    hold_until_=duration<=UINT64_MAX-now?now+duration:now;
    if(!lock_candidate||hold<1000){locked_=lock_pending_=false;}
    else if(!locked_){
      if(!lock_pending_){lock_pending_=true;lock_since_=now;}
      if(now-lock_since_>=Settle){locked_=true;lock_pending_=false;}
    }
    return {rms_,remaining,locked_};
  }
};
}

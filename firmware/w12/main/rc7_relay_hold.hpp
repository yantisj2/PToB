// Copyright (c) 2026 Jonathan Yantis. MIT.
#pragma once
#include <cstdint>
extern "C" {
#include "r2/rc7/protocol/rc_dynamic.h"
}
namespace v12 {
// Radio continuity only. A retained train supplies no timing permission.
// A loss episode clears on fresh qualified physical timing, or on sustained
// recovery for callers without an original stamp. Child leases keep their expiry.
class Rc7RelayHold {
 public:
  static constexpr uint64_t HoldUs=30000000, RecoveryUs=2000000;
 private:
  rc7_path scope_{};
  uint64_t deadline_=0,recovered_=0,last_=0,original20_=0;
  bool armed_=false;
 public:
  void clear(){scope_={};deadline_=recovered_=last_=original20_=0;armed_=false;}
  uint64_t deadline()const{return deadline_;}
  bool can_retain(uint64_t now)const{
    return armed_&&now>=last_&&(!deadline_||now<deadline_);
  }
  bool update(uint64_t now,bool timing,const rc7_path& scope,bool parent_current=true,
              uint64_t original20=0){
    if(!parent_current||!now||!rc7_path_valid(&scope)||!scope.depth){clear();return false;}
    if(last_&&now<last_){clear();return false;}
    if(!rc7_path_same(&scope_,&scope)){clear();scope_=scope;}
    if(last_&&now-last_>250000)recovered_=0;
    last_=now;
    if(deadline_&&now>=deadline_){armed_=false;deadline_=recovered_=0;return false;}
    if(timing){
      armed_=true;
      if(original20>original20_){
        original20_=original20;
        deadline_=recovered_=0;
      }else if(!original20&&deadline_){
        if(!recovered_)recovered_=now;
        if(now-recovered_>=RecoveryUs)deadline_=recovered_=0;
      }
      return true;
    }
    recovered_=0;
    if(!armed_)return false;
    if(!deadline_){
      if(now>UINT64_MAX-HoldUs){clear();return false;}
      deadline_=now+HoldUs;
    }
    return true;
  }
};
}

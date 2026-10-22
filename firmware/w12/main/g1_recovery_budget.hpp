// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace v12 {
// Scheduling only. Brief good observations pause accumulated loss; only a
// continuous recovery clears it. No observation grants time or source authority.
class G1RecoveryBudget {
 uint64_t at_=0,bad_=0,good_since_=0;bool seen_=false,good_=false;
 public:
 void reset(uint64_t now=0){at_=now;bad_=good_since_=0;seen_=now!=0;good_=false;}
 bool observe(bool good,uint64_t now,uint64_t recovery,uint64_t max_gap=UINT64_MAX){
  const bool reset_clock=!seen_||now<at_;
  if(reset_clock){reset();seen_=true;at_=now;good_=good;good_since_=good?now:0;return true;}
  const auto dt=now-at_;const bool continuous=good&&good_&&dt<=max_gap;
  if(!continuous){
   if(!good||!good_||dt>max_gap)bad_=dt>UINT64_MAX-bad_?UINT64_MAX:bad_+dt;
   good_since_=good?now:0;
  }
  at_=now;good_=good;
  if(stable(now,recovery))bad_=0;
  return false;
 }
 bool stable(uint64_t now,uint64_t recovery)const{return seen_&&good_&&now>=good_since_&&now-good_since_>=recovery;}
 bool good()const{return good_;}
 uint64_t lost()const{return bad_;}
 uint64_t recovery_left(uint64_t now,uint64_t recovery)const{return !good_||now<good_since_?recovery:now-good_since_>=recovery?0:recovery-(now-good_since_);}
 uint64_t loss_left(uint64_t limit)const{return bad_>=limit?0:limit-bad_;}
};
}

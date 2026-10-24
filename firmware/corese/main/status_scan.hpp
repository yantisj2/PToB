// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace corese {
// A presentation listener may borrow one short discovery window only while
// the timing owner independently reports a protected, healthy clock. This
// scheduler cannot grant time authority or extend a radio/source deadline.
class StatusScan {
 uint64_t last_=0,next_=0,start_=0,until_=0;uint32_t windows_=0;bool active_=false;
 public:
 static constexpr uint64_t WindowUs=80000,IntervalUs=800000;
 bool poll(uint64_t now,bool eligible){
  if(now<last_){active_=false;return false;}last_=now;
  if(!eligible){active_=false;return false;}
  if(allows(now))return true;
  active_=false;
  if(now<next_||now>UINT64_MAX-IntervalUs)return false;
  start_=now;until_=now+WindowUs;next_=now+IntervalUs;active_=true;
  if(windows_!=UINT32_MAX)++windows_;
  return true;
 }
 bool allows(uint64_t now)const{return active_&&now>=start_&&now<until_;}
 bool observed(uint64_t at)const{return start_&&at>=start_&&at<until_;}
 uint64_t deadline()const{return until_;}
 uint32_t windows()const{return windows_;}
};
}

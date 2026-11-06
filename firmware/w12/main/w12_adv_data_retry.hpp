// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace w12 {
// A failed controller data command never becomes a successful publication.
// Retry only transient controller capacity at the paused capability owner;
// persistent capacity, other errors and backward time retain the fatal path.
class AdvDataRetry {
 uint64_t started_=0,retry_at_=0;unsigned attempts_=0;
 public:
 bool ready(uint64_t now)const{return !attempts_||now<started_||now>=retry_at_;}
 bool defer(int result,uint64_t now){
  if(!result){started_=retry_at_=0;attempts_=0;return false;}
  if(result!=0x207||now>UINT64_MAX-100000)return false;
  if(!attempts_)started_=now;
  if(now<started_||now-started_>=500000||++attempts_>=3)return false;
  retry_at_=now+100000;return true;
 }
};
}

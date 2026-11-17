// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace w12 {
// A failed controller data command never becomes a successful publication.
// Retry only transient controller capacity at the paused capability owner;
// persistent capacity, other errors and backward time retain the fatal path.
class AdvDataRetry {
 uint64_t started_=0,retry_at_=0;unsigned attempts_=0;bool recreated_=false,configure_=false;
 public:
 void reset(){started_=retry_at_=0;attempts_=0;recreated_=configure_=false;}
 bool expired(uint64_t now)const{return attempts_&&(now<started_||now-started_>=500000);}
 bool needs_configure()const{return configure_;}
 void removed(){configure_=true;}
 void configured(){configure_=false;}
 bool ready(uint64_t now)const{return !attempts_||expired(now)||now>=retry_at_;}
 bool defer(int result,uint64_t now){
  if(!result){reset();return false;}
  if(result!=0x207||now>UINT64_MAX-100000)return false;
  if(!attempts_)started_=now;
  if(now<started_||now-started_>=500000)return false;
  // The third refusal requests the single recreation. After recreation,
  // capacity may use the remaining original budget instead of ending a
  // 500ms episode after only about300ms because of a fixed attempt count.
  if(++attempts_>=3&&!recreated_)return false;
  retry_at_=now+100000;return true;
 }
 // Reclaim only the stopped CAP instance, once, within the original budget.
 bool recreate(uint64_t now){
  if(recreated_||attempts_!=3||now<started_||now-started_>=500000||now>UINT64_MAX-100000)return false;
  recreated_=true;retry_at_=now+100000;return true;
 }
};
}

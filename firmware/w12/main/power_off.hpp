// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "update_gate.hpp"
namespace w12 {
class PowerOff {
public:
 bool request(v12::UpdateGate& drain,uint64_t now){
  if(requested_||!drain.begin(now))return false;
  requested_=true;return true;
 }
 bool requested()const{return requested_;}
 bool ready(v12::UpdateStage stage,bool pressed,uint64_t now){
  if(!requested_||stage!=v12::UpdateStage::Ready||pressed){released_=false;return false;}
  if(!released_||now<release_at_){released_=true;release_at_=now;return false;}
  return now-release_at_>=40000;
 }
private:
 bool requested_=false,released_=false;uint64_t release_at_=0;
};
// Exact Meshnology W12 rails only. Wake is a fresh GPIO0 press and a reboot.
int power_wake_init();
int power_off();
}

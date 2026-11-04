// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace w12 {
// Local display only. Relay permission and RC9 admission consume actual live
// authenticated BLE quality independently of this source-selection dwell.
class LoRaClockSelection {
 uint64_t since_=0,last_=0,root_=0,boot_=0,origin_=0;uint32_t epoch_=0;
 bool good_=false,lora_=false;
 public:
 bool select(bool ble,bool lora_current,uint64_t root,uint64_t boot,uint32_t epoch,uint64_t origin,uint64_t now){
  const bool qualified=ble&&root&&boot&&epoch&&origin;
  const bool same=good_&&now>=last_&&now-last_<250000&&root_==root&&boot_==boot&&epoch_==epoch&&origin_==origin;
  if(!qualified)good_=false;
  else if(!same){since_=now;good_=true;root_=root;boot_=boot;epoch_=epoch;origin_=origin;}
  last_=now;
  if(qualified&&(!lora_||(now>=since_&&now-since_>=30000000))){lora_=false;return false;}
  if(!lora_current)return !qualified;
  lora_=true;return true;
 }
};
}

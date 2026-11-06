// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace w12 {
// Local display only. Relay permission and RC9 admission consume actual live
// authenticated BLE quality independently of this source-selection dwell.
class LoRaClockSelection {
 uint64_t since_=0,last_=0,root_=0,boot_=0,origin_=0,pause_=0;uint32_t epoch_=0,quality_=0,publication_=0;
 bool good_=false,lora_=false;
 uint64_t max_gap_=0,first_loss_=0,first_root_=0,first_boot_=0;
 uint32_t first_epoch_=0,first_refusal_=0,losses_=0,resets_=0;uint8_t first_reason_=0;
 static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
 public:
 static constexpr uint64_t PermissionGapUs=1500000;
 struct View {uint64_t observed=0,since=0,max_gap=0,pause=0,first_loss=0,first_root=0,first_boot=0;
  uint32_t remaining=30000000,first_epoch=0,first_refusal=0,losses=0,resets=0;uint8_t first_reason=0;bool qualified=false,lora=false;};
 View view(uint64_t now)const{
  const bool fresh=last_&&now>=last_&&now-last_<250000;
  const auto end=good_?now:pause_;const auto elapsed=end>=since_?end-since_:0;
  return {last_,since_,max_gap_,pause_&&now>=pause_?now-pause_:0,first_loss_,first_root_,first_boot_,
   uint32_t(fresh&&(good_||pause_)?elapsed>=30000000?0:30000000-elapsed:30000000),first_epoch_,first_refusal_,losses_,resets_,first_reason_,good_&&fresh,lora_};
 }
 bool select(bool ble,bool lora_current,uint64_t root,uint64_t boot,uint32_t epoch,uint64_t origin,uint64_t now,
             bool permission_gap=false,uint32_t quality=0,uint32_t publication=0,uint32_t refusal=0){
  const bool qualified=ble&&root&&boot&&epoch&&origin;
  const bool observed=now>=last_&&now-last_<250000;
  const bool same=root_==root&&boot_==boot&&epoch_==epoch&&origin_==origin&&quality_==quality&&publication_==publication;
  if(last_&&now>=last_&&now-last_>max_gap_)max_gap_=now-last_;
  if(good_&&!qualified)inc(losses_);
  if(!first_loss_&&good_&&lora_&&(!qualified||!same||!observed)){
   first_loss_=now;first_root_=root_;first_boot_=boot_;first_epoch_=epoch_;first_refusal_=refusal;
   first_reason_=!observed?3:qualified&&!same?2:1;
  }
  const bool compatible=(!root||root_==root)&&(!boot||boot_==boot)&&(!epoch||epoch_==epoch)&&
    (!origin||origin_==origin)&&(!quality||quality_==quality)&&(!publication||publication_==publication);
  const bool may_pause=!qualified&&permission_gap&&observed&&compatible&&(good_||pause_);
  if(may_pause&&!pause_)pause_=last_;
  const bool paused=may_pause&&now>=pause_&&now-pause_<PermissionGapUs;
  if(!qualified)good_=false;
  else{
   if(pause_&&same&&observed&&now>=pause_&&now-pause_<PermissionGapUs)since_+=now-pause_;
   else if(!good_||!same||!observed){since_=now;inc(resets_);}
   good_=true;
  }
  if(!paused){pause_=0;root_=root;boot_=boot;epoch_=epoch;origin_=origin;quality_=quality;publication_=publication;}
  last_=now;
  if(qualified&&(!lora_||!lora_current||(now>=since_&&now-since_>=30000000))){lora_=false;return false;}
  if(!lora_current)return !qualified;
  lora_=true;return true;
 }
};
}

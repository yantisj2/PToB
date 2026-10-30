// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include "display_pages.hpp"
namespace w12 {
// One interpreter for the native GPIO0 button, in the local 20 MHz domain.
class MainButton {
public:
 enum Action:uint8_t {None,NextPage,ToggleRelayOnly,ToggleTx,Sleep};
 static constexpr uint64_t DebounceTicks=800000,ToggleTicks=30000000,SleepTicks=80000000;
 explicit MainButton(bool pressed):raw_(pressed),stable_(pressed),armed_(!pressed){}
 Action update(bool pressed,uint64_t now,uint8_t page){
  if(active_&&(page!=page_||now<down_))cancelled_=true;
  if(pressed!=raw_){raw_=pressed;changed_=now;}
  if(now>=changed_&&now-changed_>=DebounceTicks&&raw_!=stable_){
   stable_=raw_;
   if(stable_){
    if(armed_){down_=changed_;page_=page;active_=true;consumed_=powered_=cancelled_=false;}
   }else{
    // Use the physical release edge, not the end of its debounce interval.
    const auto action=active_&&!cancelled_?elapsed(changed_):None;
    const bool tap=active_&&!cancelled_&&!consumed_;
    active_=consumed_=powered_=cancelled_=false;armed_=true;
    return action!=None?action:tap?NextPage:None;
   }
  }
  // A pending release cannot cross a hold threshold during debounce.
  return raw_&&stable_&&active_&&!cancelled_?elapsed(now):None;
 }
private:
 Action elapsed(uint64_t now){
  if(now<down_)return None;
  if(page_!=display_pages::Front&&!powered_&&now-down_>=SleepTicks){powered_=consumed_=true;return Sleep;}
  if(!consumed_&&now-down_>=ToggleTicks){
   if(page_==display_pages::Front){consumed_=true;return ToggleRelayOnly;}
   if(page_==display_pages::LoraRx){consumed_=true;return ToggleTx;}
  }
  return None;
 }
 bool raw_=false,stable_=false,armed_=true,active_=false,consumed_=false,powered_=false,cancelled_=false;
 uint8_t page_=0;uint64_t changed_=0,down_=0;
};
}

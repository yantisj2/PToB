// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include <cstddef>
#include <cstdint>
#include "lora_view.h"
namespace n30 {
constexpr unsigned Width=64,Height=128,FrameBytes=1024;
constexpr unsigned PageCount=4;
constexpr uint8_t PrecisionPage=2,LoRaPage=3;
constexpr uint64_t SnapshotMaxUs=250000,IdleUs=300000000,MoveUs=30000000;
constexpr uint64_t ClockGraceUs=500000;
constexpr uint64_t MinimalAfterUs=900000000;
constexpr uint8_t NormalContrast=0x60,SaverContrast=NormalContrast/2;
enum class State:uint8_t {Off,Search,Join,Acquire,Follow,Hold,Fault};
enum class Source:uint8_t {None,Ble,Rc1,Rc2,LoRa};
enum class Action:uint8_t {None,ToggleRelayOnly,ToggleLoRa};
struct Model {
 char unit[25]="N30",parent[25]="NONE",root[25]="----";
 uint64_t sampled_us=0,utc_at_us=0,utc_us=0,until_us=0;
 uint32_t rms_ns=UINT32_MAX;uint16_t hz=0,delivery=UINT16_MAX;
 bool valid=false,locked=false,serving=false,relay_only=false,mode_pending=false;
 Source source=Source::None;
 struct BleClock {uint64_t at_us=0,utc_us=0,until_us=0,root=0,boot=0;uint32_t epoch=0;bool valid=false,following=false;} ble;
 struct Precision {
  uint64_t until_us=0,scope=0,root_boot=0,lock_since_us=0;
  uint32_t source_epoch=0,quality_scope=0,counter_hz=20000000,rms_ns=UINT32_MAX;
  int32_t relative_ppb=0;
  bool valid=false,relative_known=false,locked=false,lock_age_valid=false;
  Source source=Source::None;
 } precision;
 n30_lora_view lora{};uint8_t lora_lamps[8]{};
 int64_t delta_ns=0;int32_t drift_ppb=0;bool delta_valid=false,drift_valid=false;
 State state=State::Search;uint8_t lamps[8]{}; // 0 inactive,1 RX/event,2 TX/API
};
struct View {
 uint8_t page=0,saver_position=0;bool help=false,saver=false,blank=false;
 bool clock_frozen=false;
 bool minimal=false;
};
// One latched session for both saver layouts, independent of pixel history.
class SaverTimer {
 uint64_t started_=0,last_=0,loss_at_=0;bool active_=false,losing_=false;
 public:
 void update(uint64_t now,View& v,bool synchronized,bool due){
  if(!due||v.help||v.blank||(active_&&now<last_))active_=losing_=false;
  if(due&&!v.help&&!v.blank){
   if(!active_&&v.saver&&synchronized){started_=now;active_=true;losing_=false;}
   if(active_){
    if(synchronized)losing_=false;
    else if(!losing_){loss_at_=now;losing_=true;}
    if(losing_&&now>=loss_at_&&now-loss_at_>=ClockGraceUs)active_=losing_=false;
   }
  }
  v.saver=active_;v.minimal=active_&&now-started_>=MinimalAfterUs;last_=now;
 }
 uint64_t deadline()const{return active_&&losing_&&loss_at_<=UINT64_MAX-ClockGraceUs?loss_at_+ClockGraceUs:UINT64_MAX;}
};
bool current(const Model&,uint64_t now);
bool time(const Model&,uint64_t now,uint64_t& utc);
bool analog_time(const Model&,uint64_t now,uint64_t& utc);
bool saver_time(const Model&,uint64_t now,uint64_t& utc);
bool pps_on(const Model&,uint64_t now);
bool precision_current(const Model&,uint64_t now);
// Duration of the final live, settled BLE lock. Scope, loss, expiry and a
// missing publication reset it; navigation and new fit generations do not.
class LockTimer {
 uint64_t since_=0,last_=0,scope_=0,boot_=0;uint32_t epoch_=0,quality_=0;bool active_=false;
 public:
 void update(Model&,uint64_t now);
};
const char* source_label(const Model&,uint64_t now);
void render(uint8_t frame[FrameBytes],const Model&,const View&,uint64_t now);
// Logical portrait bitmap -> native 128x64 OLED pages. Both mounted rotations.
void rotate(uint8_t native[FrameBytes],const uint8_t portrait[FrameBytes],bool clockwise);
// Display-only grace: remember only a successfully displayed live clock frame.
// Observe every worker turn, not just frames. Never prolong source validity.
class ClockHold {
 bool saved_=false,losing_=false,seen_=false,saver_=false;
 uint8_t page_=0,position_=0;uint64_t observed_=0,loss_at_=0;
 uint8_t image_[FrameBytes]{};
 bool active(uint64_t now)const{return saved_&&losing_&&now>=loss_at_&&now-loss_at_<ClockGraceUs;}
 public:
 void reset(){saved_=losing_=seen_=false;}
 void update(const Model&,View&,uint64_t now,bool saver_due=false);
 void draw(uint8_t frame[FrameBytes],const Model&,const View&,uint64_t now)const;
 void presented(const Model&,const View&,uint64_t now,const uint8_t frame[FrameBytes]);
 uint64_t deadline()const{return saved_&&losing_?loss_at_+ClockGraceUs:UINT64_MAX;}
};
class Input {
 bool raw_=false,stable_=false,armed_=false,consumed_=false,wake_=false,main_press_=false,lora_press_=false,lora_eligible_=false;
 uint64_t changed_=0,down_=0,idle_=0;
 public:
 explicit Input(bool pressed=false):raw_(pressed),stable_(pressed),armed_(!pressed){}
 Action update(bool pressed,uint64_t now,View& view);
 bool saver_due(uint64_t now,const View& view)const;
 void idle(uint64_t now,bool eligible,View& view)const;
 bool polling()const{return raw_||stable_;} // Debounce/holds still use10ms.
 void activity(uint64_t now){idle_=now;}
};
// The actual worker/test pipeline: both layouts share entry, loss and residence.
void update_saver(Input&,ClockHold&,SaverTimer&,const Model&,View&,uint64_t now);
// Caller serializes access. One physical action, never a delayed toggle train.
class ActionMailbox {
 bool pending_=false;uint64_t posted_=0;
 public:
 bool post(uint64_t now){if(pending_)return false;posted_=now;pending_=true;return true;}
 bool take(uint64_t now){const bool fresh=pending_&&now>=posted_&&now-posted_<SnapshotMaxUs;pending_=false;return fresh;}
};
class Activity {
 uint32_t counts_[8]{};uint64_t until_[8]{},root_=0,boot_=0,last_=0;
 bool seen_=false;
 public:
 void update(Model&,const uint32_t counts[8],uint64_t root,uint64_t boot,uint64_t now,bool enabled,bool tx);
};
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include <cstddef>
#include <cstdint>
namespace n30 {
constexpr unsigned Width=64,Height=128,FrameBytes=1024;
constexpr uint64_t SnapshotMaxUs=250000,IdleUs=300000000,MoveUs=30000000;
constexpr uint64_t ClockGraceUs=500000;
enum class State:uint8_t {Off,Search,Join,Acquire,Follow,Hold,Fault};
enum class Source:uint8_t {None,Ble,Rc1,Rc2};
enum class Action:uint8_t {None,ToggleRelayOnly};
struct Model {
 char unit[25]="N30",parent[25]="NONE";
 uint64_t sampled_us=0,utc_at_us=0,utc_us=0,until_us=0;
 uint32_t rms_ns=UINT32_MAX;uint16_t hz=0,delivery=UINT16_MAX;
 bool valid=false,locked=false,serving=false,relay_only=false,mode_pending=false;
 Source source=Source::None;
 State state=State::Search;uint8_t lamps[8]{}; // 0 inactive,1 RX/event,2 TX/API
};
struct View {
 uint8_t page=0,saver_position=0;bool help=false,saver=false,blank=false;
 bool clock_frozen=false;
};
bool current(const Model&,uint64_t now);
bool time(const Model&,uint64_t now,uint64_t& utc);
bool analog_time(const Model&,uint64_t now,uint64_t& utc);
bool pps_on(const Model&,uint64_t now);
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
};
class Input {
 bool raw_=false,stable_=false,armed_=false,consumed_=false,wake_=false,main_press_=false;
 uint64_t changed_=0,down_=0,idle_=0;
 public:
 explicit Input(bool pressed=false):raw_(pressed),stable_(pressed),armed_(!pressed){}
 Action update(bool pressed,uint64_t now,View& view);
 bool saver_due(uint64_t now,const View& view)const;
 void idle(uint64_t now,bool eligible,View& view)const;
};
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

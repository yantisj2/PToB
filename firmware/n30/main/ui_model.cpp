// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "ui.hpp"
#include <cstring>
namespace n30 {
bool current(const Model& m,uint64_t now){return now>=m.sampled_us&&now-m.sampled_us<SnapshotMaxUs;}
bool time(const Model& m,uint64_t now,uint64_t& utc){
 if(!current(m,now)||!m.valid||now<m.utc_at_us||now>=m.until_us||m.state==State::Fault||m.state==State::Off||now-m.utc_at_us>UINT64_MAX-m.utc_us)return false;
 utc=m.utc_us+(now-m.utc_at_us);return true;
}
bool analog_time(const Model& m,uint64_t now,uint64_t& utc){return time(m,now,utc)&&m.locked&&m.state==State::Follow&&m.source!=Source::None;}
namespace {bool main_dial(const View& v){return v.page%2==0&&!v.help&&!v.blank&&!v.saver;}}
void DialHold::update(const Model& m,View& v,uint64_t now){
 v.dial_frozen=false;
 if(!main_dial(v)||(seen_&&now<observed_)){reset();return;}
 seen_=true;observed_=now;uint64_t utc;
 if(analog_time(m,now,utc)){losing_=false;return;}
 if(!saved_)return;
 if(!losing_){losing_=true;loss_at_=now;}
 if(now-loss_at_<DialGraceUs){v.dial_frozen=true;v.dial_utc=utc_;v.dial_hz=hz_;}
 else saved_=false; // Cannot rearm until another live dial reaches the screen.
}
void DialHold::presented(const Model& m,const View& v,uint64_t now){
 uint64_t utc;if(main_dial(v)&&!v.dial_frozen&&analog_time(m,now,utc)){saved_=true;losing_=false;utc_=utc;hz_=m.hz;}
}
const char* source_label(const Model& m,uint64_t now){
 uint64_t utc;if(m.mode_pending||!time(m,now,utc))return "WAIT";
 if(m.state==State::Hold)return "HOLD";
 switch(m.source){case Source::Ble:return "BLE";case Source::Rc1:return "RC1";case Source::Rc2:return "RC2";case Source::None:return "--";}
 return "--";
}
void rotate(uint8_t out[FrameBytes],const uint8_t in[FrameBytes],bool cw){
 std::memset(out,0,FrameBytes);
 for(unsigned y=0;y<Height;y++)for(unsigned x=0;x<Width;x++)if(in[y*8+x/8]&(0x80u>>(x%8))){
  const unsigned nx=cw?127-y:y,ny=cw?x:63-x;
  out[(ny/8)*128+nx]|=uint8_t(1u<<(ny%8));
 }
}
Action Input::update(bool pressed,uint64_t now,View& v){
 Action action=Action::None;
 if(pressed!=raw_){raw_=pressed;changed_=now;}
 if(now>=changed_&&now-changed_>=40000&&raw_!=stable_){
  stable_=raw_;
  if(stable_){
   if(armed_){down_=changed_;idle_=now;wake_=v.saver||v.blank;main_press_=!v.page&&!v.help;consumed_=wake_;v.saver=v.blank=false;}
  }else{
   if(armed_&&!consumed_){
    if(main_press_&&now>=down_&&now-down_>=1500000)action=Action::ToggleRelayOnly;
    else if(v.help)v.help=false;else v.page=(v.page+1)%2;
   }
   armed_=true;consumed_=wake_=false;down_=0;idle_=now;
  }
 }
 if(stable_&&armed_){
  idle_=now;
  if(!consumed_&&now>=down_&&now-down_>=1500000){
   if(main_press_){action=Action::ToggleRelayOnly;consumed_=true;}
   else{v.help=true;consumed_=true;}
  }
  // Main-screen toggles consume the extended hold, just like W12. RF keeps
  // its existing4s display blank; a wake press cannot turn into another action.
  if(!main_press_&&!wake_&&now>=down_&&now-down_>=4000000){v.blank=true;consumed_=true;}
 }
 return action;
}
void Input::idle(uint64_t now,bool eligible,View& v)const{
 v.saver=!v.blank&&!stable_&&!v.help&&eligible&&now>=idle_&&now-idle_>=IdleUs;
 if(v.saver)v.saver_position=uint8_t(((now-idle_-IdleUs)/MoveUs)%3);
}
void Activity::update(Model& m,const uint32_t c[8],uint64_t root,uint64_t boot,uint64_t now,bool enabled,bool tx){
 if(!seen_||root!=root_||boot!=boot_||now<last_||!enabled){
  std::memcpy(counts_,c,sizeof counts_);std::memset(until_,0,sizeof until_);seen_=true;
 }
 for(unsigned i=0;i<8;i++){
  // Baseline/reset never animates old history. A snapshot cannot create RF.
  if(enabled&&c[i]>counts_[i])until_[i]=now;
  counts_[i]=c[i];
  m.lamps[i]=enabled&&until_[i]&&now>=until_[i]&&now-until_[i]<250000&&!(now/125000%2)?uint8_t(i==5&&tx?2:1):0;
 }
 root_=root;boot_=boot;last_=now;
}
}

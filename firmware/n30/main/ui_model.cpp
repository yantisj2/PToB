// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "ui.hpp"
#include "dual_time.hpp"
#include <cstring>
namespace n30 {
bool current(const Model& m,uint64_t now){return now>=m.sampled_us&&now-m.sampled_us<SnapshotMaxUs;}
bool time(const Model& m,uint64_t now,uint64_t& utc){
 if(!current(m,now)||!m.valid||now<m.utc_at_us||now>=m.until_us||m.state==State::Fault||m.state==State::Off||now-m.utc_at_us>UINT64_MAX-m.utc_us)return false;
 utc=m.utc_us+(now-m.utc_at_us);return true;
}
bool saver_time(const Model& m,uint64_t now,uint64_t& utc){return time(m,now,utc)&&m.state==State::Follow&&m.source!=Source::None;}
bool analog_time(const Model& m,uint64_t now,uint64_t& utc){return saver_time(m,now,utc)&&m.locked;}
bool pps_on(const Model& m,uint64_t now){uint64_t utc;return (!m.mode_pending||m.source==Source::LoRa)&&analog_time(m,now,utc)&&utc%1000000<250000;}
bool precision_current(const Model& m,uint64_t now){return current(m,now)&&m.precision.valid&&now<m.precision.until_us;}
void LockTimer::update(Model& m,uint64_t now){
 auto& p=m.precision;
 const bool live=precision_current(m,now)&&p.locked&&p.scope;
 if(!live){active_=false;since_=scope_=0;}
 if(!live||(active_&&(now<last_||now-last_>=SnapshotMaxUs||scope_!=p.scope||boot_!=p.root_boot||epoch_!=p.source_epoch||quality_!=p.quality_scope)))active_=false;
 if(live&&!active_){since_=now;scope_=p.scope;boot_=p.root_boot;epoch_=p.source_epoch;quality_=p.quality_scope;active_=true;}
 p.lock_since_us=since_;p.lock_age_valid=active_;last_=now;
}
void ClockHold::update(const Model& m,View& v,uint64_t now,bool saver_due){
 v.clock_frozen=false;
 if(v.help||v.blank||(seen_&&now<observed_)){reset();return;}
 // Real user navigation/wake wins immediately. A quality dip may retain an
 // already displayed saver only while the original idle timer still wants it.
 if(saved_&&(v.page!=page_||(saver_&&!saver_due)||(!saver_&&v.saver)))reset();
 if(v.page!=0&&!v.saver&&!(saved_&&saver_&&saver_due)){reset();return;}
 seen_=true;observed_=now;uint64_t utc;
 const bool saver_surface=v.saver||(saved_&&saver_&&saver_due);
 if(saver_surface?saver_time(m,now,utc):analog_time(m,now,utc)){losing_=false;return;}
 if(!saved_)return;
 if(!losing_){losing_=true;loss_at_=now;}
 if(!active(now)){saved_=false;return;}
 v.clock_frozen=true;
 if(saver_){v.saver=true;v.saver_position=position_;}
}
void ClockHold::presented(const Model& m,const View& v,uint64_t now,const uint8_t frame[FrameBytes]){
 uint64_t utc;if(!v.help&&!v.blank&&(v.saver||v.page==0)&&!v.clock_frozen&&(v.saver?saver_time(m,now,utc):analog_time(m,now,utc))){
  std::memcpy(image_,frame,FrameBytes);saved_=true;losing_=false;saver_=v.saver;page_=v.page;position_=v.saver_position;
 }
}
const char* source_label(const Model& m,uint64_t now){
 uint64_t utc;if((m.mode_pending&&m.source!=Source::LoRa)||!time(m,now,utc))return "WAIT";
 if(m.state==State::Hold)return "HOLD";
 switch(m.source){case Source::Ble:return "BLE";case Source::Rc1:return "RC1";case Source::Rc2:return "RC2";case Source::LoRa:return "LOR";case Source::None:return "--";}
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
 if(stable_&&lora_press_&&(v.page!=2||v.help||v.blank||v.saver))lora_eligible_=false;
 if(pressed!=raw_){raw_=pressed;changed_=now;}
 if(now>=changed_&&now-changed_>=40000&&raw_!=stable_){
  stable_=raw_;
  if(stable_){
   if(armed_){down_=changed_;idle_=now;wake_=v.saver||v.blank;main_press_=!v.page&&!v.help;lora_press_=v.page==2&&!v.help;lora_eligible_=lora_press_;consumed_=wake_;v.saver=v.blank=false;}
  }else{
   if(armed_&&!consumed_){
    if((main_press_||lora_press_)&&now>=down_&&now-down_>=1500000)action=main_press_?Action::ToggleRelayOnly:lora_eligible_?Action::ToggleLoRa:Action::None;
    else if(v.help)v.help=false;else v.page=(v.page+1)%PageCount;
   }
   armed_=true;consumed_=wake_=false;down_=0;idle_=now;
  }
 }
 if(stable_&&armed_){
  idle_=now;
  if(!consumed_&&now>=down_&&now-down_>=1500000){
   if(main_press_||lora_press_){action=main_press_?Action::ToggleRelayOnly:lora_eligible_?Action::ToggleLoRa:Action::None;consumed_=true;}
   else{v.help=true;consumed_=true;}
  }
  // Main-screen toggles consume the extended hold, just like W12. RF keeps
  // its existing4s display blank; a wake press cannot turn into another action.
  if(!main_press_&&!lora_press_&&!wake_&&now>=down_&&now-down_>=4000000){v.blank=true;consumed_=true;}
 }
 return action;
}
bool Input::saver_due(uint64_t now,const View& v)const{return !v.blank&&!stable_&&!v.help&&now>=idle_&&now-idle_>=IdleUs;}
void Input::idle(uint64_t now,bool eligible,View& v)const{
 v.saver=eligible&&saver_due(now,v);
 if(saver_due(now,v))v.saver_position=uint8_t(((now-idle_-IdleUs)/MoveUs)%3);
}
void update_saver(Input& input,ClockHold& hold,SaverTimer& timer,const Model& m,View& v,uint64_t now){
 uint64_t utc;input.idle(now,analog_time(m,now,utc),v);
 const bool due=input.saver_due(now,v);
 hold.update(m,v,now,due);
 // A precision dip is not total sync loss. Either live following input keeps
 // this one session alive; raw local HOLD never counts as synchronization.
 const bool synchronized=saver_time(m,now,utc)||(m.ble.following&&ble_time(m,now,utc))||lora_time(m.lora,now,utc);
 timer.update(now,v,synchronized,due);
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

// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace watch {
constexpr int Width=410,Height=502;
enum class Screen:uint8_t {Face,Launcher,Settings,Faces};
enum class Source:uint8_t {None,BLE,LOR,RC1,RC2,RC9};
struct Settings {uint8_t version=2,face=0,brightness=40,timeout_s=90;};
inline bool valid(const Settings& s){return s.version==2&&s.face<=1&&s.brightness>=10&&s.brightness<=100&&(s.timeout_s==90||s.timeout_s==120||s.timeout_s==180);}
struct Model {
 uint64_t utc_us=0,sampled_us=0,until_us=0,uptime_s=0;
 uint32_t rms_ns=UINT32_MAX,hold_ms=0,hold_trials=0;
 int16_t tz_minutes=0,battery=-1;
 Source source=Source::None;
 bool local_time=false,time_valid=false,locked=false,holding=false,hold_valid=false,hold_lower=false,lora_on=false,charging=false;
};
inline Source receiver_source(const Model& m,bool current){
 if(!current||m.holding)return Source::None;
 switch(m.source){case Source::BLE:case Source::RC1:case Source::RC2:return Source::BLE;
 case Source::LOR:return Source::LOR;case Source::RC9:return Source::RC9;default:return Source::None;}
}
// Presentation-only wall-clock continuity. Never feeds a timing source, PLL,
// admission, watchdog or precision metric. Offline time is explicitly HOLD.
struct PresentationClock {
 Model anchor{};bool have=false;
 Model present(Model input,uint64_t now){
  if(have&&now<anchor.sampled_us)have=false;
  if(input.time_valid&&now>=input.sampled_us&&now-input.sampled_us<250000&&now<input.until_us){anchor=input;have=true;return input;}
  if(!have||now-anchor.sampled_us>=86400000000ULL)return input;
  input.utc_us=anchor.utc_us+(now-anchor.sampled_us);input.sampled_us=now;input.until_us=now+250000;
  input.tz_minutes=anchor.tz_minutes;input.time_valid=true;input.local_time=true;input.holding=true;
  input.locked=false;input.source=Source::None;input.rms_ns=UINT32_MAX;input.hold_valid=false;input.hold_trials=0;
  return input;
 }
};
struct View {
 Settings settings{};Screen screen=Screen::Face;bool asleep=false,dirty=false,save_failed=false;
 uint64_t activity_us=0;
 void touch(int x,int y,uint64_t now);
 void button(uint64_t now);
 void tick(uint64_t now);
};
// Resolve touch only on release, so a swipe never first activates a tap target.
struct Gesture {
 bool down=false,wake=false;int start_x=0,start_y=0,last_x=0,last_y=0;uint64_t started=0;Screen screen=Screen::Face;
 bool update(View& v,bool contact,int x,int y,uint64_t now){
  if(contact){
   const bool began=!down;
   if(!down){down=true;wake=v.asleep;start_x=x;start_y=y;started=now;screen=v.screen;if(wake)v.asleep=false;}
   last_x=x;last_y=y;v.activity_us=now;return began&&wake;
  }
  if(!down)return false;
  down=false;const int dx=last_x-start_x,dy=last_y-start_y,ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
  if(wake||now<started||now-started>1500000||screen!=v.screen)return false;
  if(screen==Screen::Face&&ax>=64&&ax>2*ay){v.settings.face^=1;v.dirty=true;v.activity_us=now;return true;}
  if(ax<20&&ay<20){v.touch(start_x,start_y,now);return true;}
  return false;
 }
};
void render(uint16_t* pixels,const Model&,const View&,uint64_t now);
}

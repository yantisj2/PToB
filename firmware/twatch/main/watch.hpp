// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <algorithm>
#include <cmath>
#include "calendar.hpp"
#include "solar_geometry.hpp"
namespace watch {
constexpr int Width=410,Height=502;
enum class Screen:uint8_t {Face,Launcher,Settings,Faces};
enum class RadioState:uint8_t {Off,On,Changing,Fault};
enum class Source:uint8_t {None,BLE,LOR,RC1,RC2,RC9};
struct Settings {uint8_t version=3,face=0,brightness=40,timeout_s=30;};
inline bool valid(const Settings& s){return s.version==3&&s.face<=2&&s.brightness>=10&&s.brightness<=100&&(s.timeout_s==15||s.timeout_s==30||s.timeout_s==60||s.timeout_s==90);}
inline bool migrate(Settings& s){
 if((s.version==1||s.version==2)&&s.face<=1&&s.brightness>=10&&s.brightness<=100){s.version=3;s.timeout_s=30;return true;}
 return false;
}
const char* zone_name(uint16_t id,int16_t offset);
void hold_text(char* out,unsigned size,uint64_t duration_us);
// Keep retained panel RAM hidden until the new frame has fully transferred.
template<class Flush,class Reveal> bool commit_frame(bool waking,Flush flush,Reveal reveal){return flush()&&(!waking||reveal());}
struct SolarPosition {double x=0,y=35;};
inline SolarPosition solar_opposition(double angle){const double a=angle*3.14159265358979323846/180;return {-35*std::sin(a),35*std::cos(a)};}
struct Model {
 uint64_t utc_us=0,sampled_us=0,until_us=0,uptime_s=0;
 uint32_t rms_ns=UINT32_MAX,hold_ms=0,hold_trials=0;
 calendar::Snapshot calendar{};
 int16_t source_rssi=-127;
 double signal_position=0,tide_position=0;
 SolarPosition solar_clock{};
 int16_t tz_minutes=0,battery=-1;uint16_t tz_id=0;
 Source source=Source::None;
 double rms_position=0,hold_position=0,reserve_position=0,phase_position=0;
 RadioState ble_state=RadioState::Changing,lora_state=RadioState::Changing;
 bool motion=false,slewing=false,lora_receiving=false;
 bool local_time=false,time_valid=false,locked=false,holding=false,hold_valid=false,hold_lower=false,lora_on=false,charging=false;
};
inline bool sampled_hold(const Model& m){
 return m.time_valid&&!m.local_time&&!m.holding&&m.hold_valid&&m.hold_trials>=100&&
  (m.source==Source::BLE||m.source==Source::RC1||m.source==Source::RC2);
}
inline Source receiver_source(const Model& m,bool current){
 if(!current||m.holding)return Source::None;
 switch(m.source){case Source::BLE:case Source::RC1:case Source::RC2:return Source::BLE;
 case Source::LOR:return Source::LOR;case Source::RC9:return Source::RC9;default:return Source::None;}
}
// Presentation-only wall-clock continuity. Never feeds a timing source, PLL,
// admission, watchdog or precision metric. Offline time is explicitly HOLD.
struct PresentationClock {
 Model anchor{};bool have=false;uint64_t last=0,utc=0;
 Model present(Model input,uint64_t now){
  if(have&&now<last){have=false;anchor={};}
  const bool fresh=input.time_valid&&input.utc_us&&now>=input.sampled_us&&now-input.sampled_us<250000&&now<input.until_us;
  if(!have){if(!fresh)return input;utc=input.utc_us+now-input.sampled_us;last=now;have=true;anchor=input;}
  const uint64_t dt=now-last;
  if(dt>UINT64_MAX-utc){have=false;return {};}
  utc+=dt;last=now;
  if(fresh){
   const uint64_t target=input.utc_us+now-input.sampled_us;
   const uint64_t error=target>=utc?target-utc:utc-target,limit=dt/20;
   const uint64_t correction=std::min(error,limit);
   utc=target>=utc?utc+correction:utc-correction;
   input.slewing=error-correction>50000;
   if(input.slewing){input.locked=false;input.hold_valid=false;}
   anchor=input;
  }else{
   input.tz_minutes=anchor.tz_minutes;input.tz_id=anchor.tz_id;input.local_time=true;input.holding=true;
   input.locked=false;input.source=Source::None;input.rms_ns=UINT32_MAX;input.hold_valid=false;input.hold_trials=0;
  }
  input.utc_us=utc;input.sampled_us=now;input.until_us=now>UINT64_MAX-250000?UINT64_MAX:now+250000;input.time_valid=true;
  return input;
 }
};

struct View {
 Settings settings{};Screen screen=Screen::Face;bool asleep=false,dirty=false,save_failed=false;
 uint8_t radio_request=0; // one-shot UI requests; radio owner executes them
 uint64_t activity_us=0,glance_until_us=0;
 bool wrist_raise(uint64_t now){
  if(!asleep)return false;
  asleep=false;screen=Screen::Face;activity_us=now;glance_until_us=now+5000000;return true;
 }
 bool lower_arm(uint64_t now){if(!glance_until_us||asleep)return false;glance_until_us=0;activity_us=now;asleep=true;return true;}
 void touch(int x,int y,uint64_t now);
 void button(uint64_t now);
 void tick(uint64_t now,bool usb=false);
};
// Resolve touch only on release, so a swipe never first activates a tap target.
struct Gesture {
 bool down=false,wake=false;int start_x=0,start_y=0,last_x=0,last_y=0;uint64_t started=0;Screen screen=Screen::Face;
 bool update(View& v,bool contact,int x,int y,uint64_t now){
  if(contact){
   const bool began=!down;
   if(!down){down=true;wake=v.asleep;start_x=x;start_y=y;started=now;screen=v.screen;if(wake)v.asleep=false;}
   last_x=x;last_y=y;v.glance_until_us=0;v.activity_us=now;return began&&wake;
  }
  if(!down)return false;
  down=false;const int dx=last_x-start_x,dy=last_y-start_y,ax=dx<0?-dx:dx,ay=dy<0?-dy:dy;
  if(wake||now<started||now-started>1500000||screen!=v.screen)return false;
  if(screen==Screen::Face&&ax>=64&&ax>2*ay){v.settings.face=uint8_t((v.settings.face+(dx<0?1:2))%3);v.dirty=true;v.activity_us=now;return true;}
  if(ax<20&&ay<20){v.touch(start_x,start_y,now);return true;}
  return false;
 }
};
// Dynamic Instrument damage: old and new tiles are restored/transferred together.
struct RenderCache {
 static constexpr int Tile=32,Cols=(Width+Tile-1)/Tile,Rows=(Height+Tile-1)/Tile;
 uint8_t used[Cols*Rows]{},changed[Cols*Rows]{};uint16_t* buffer=nullptr;bool valid=false;uint8_t face=255;uint32_t generation=0;
 void mark(int x,int y){const auto i=(y/Tile)*Cols+x/Tile;used[i]=changed[i]=1;}
};
void render(uint16_t* pixels,const Model&,const View&,uint64_t now,RenderCache* cache=nullptr);
}

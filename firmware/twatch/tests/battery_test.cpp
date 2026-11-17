// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "watch.hpp"
#include "power_key.hpp"
#include <array>
#include <cassert>
using namespace watch;
int main(){
 static BatteryHistory h;
 h.observe(0,false);h.sample(0,90,false);
 h.observe(10000000,true);h.observe(25000000,false);h.observe(60000000,false);h.sample(60000000,89,false);
 auto w=h.window(60000000,1);assert(w.awake_us==15000000&&w.observed_us==60000000);
 assert(w.columns[29].level==89); // The most recent sample wins across minutes.
 h.observe(BatteryHistory::Hour,false);w=h.window(BatteryHistory::Hour,1);
 assert(w.awake_us==15000000&&w.observed_us==BatteryHistory::Hour);
 assert(w.columns[0].level==89&&w.columns[1].level<0); // Never bridge missing gauge reads.
 h.sample(BatteryHistory::Hour,101,false);w=h.window(BatteryHistory::Hour,1);assert(w.columns[29].level<0);
 // More than a day, many ring wraps, a constant display duty cycle, rising/falling gauge.
 h={};h.observe(0,false);
 for(uint64_t minute=0;minute<3000;++minute){const auto t=minute*BatteryHistory::Minute;
  h.observe(t,true);h.sample(t,int(minute%101),minute%2);h.observe(t+10000000,false);h.observe(t+BatteryHistory::Minute,false);
 }
 for(uint8_t scale:{1,6,12,24}){w=h.window(3000*BatteryHistory::Minute,scale);assert(w.observed_us==scale*BatteryHistory::Hour);assert(w.awake_us==uint64_t(scale)*600000000);}
 w=h.window(3000*BatteryHistory::Minute,24);assert(w.columns[29].level==2999%101);
 // The production PSRAM workspace matches the value API without copying a
 // chart-sized temporary onto the UI task stack. Bounds survive scale changes.
 static_assert(sizeof(BatteryHistory::Column)<=24&&sizeof(BatteryHistory::Window)<=800);
 struct GuardedWindow {uint64_t before=0x12345678;BatteryHistory::Window value;uint64_t after=0x87654321;} guarded;
 for(uint8_t scale:{24,1,12,6,1}){
  h.window(3000*BatteryHistory::Minute,scale,guarded.value);
  const auto& cached=h.snapshot(3000*BatteryHistory::Minute,scale);
  assert(guarded.before==0x12345678&&guarded.after==0x87654321);
  assert(cached.span_us==guarded.value.span_us&&cached.awake_us==guarded.value.awake_us&&cached.observed_us==guarded.value.observed_us);
  for(unsigned i=0;i<BatteryHistory::Columns;++i)assert(cached.columns[i].level==guarded.value.columns[i].level&&cached.columns[i].awake_us==guarded.value.columns[i].awake_us);
 }
 // Unknown first interval stays unknown; reboot/monotonic rollback clears history.
 h={};h.observe(50000000,true);h.observe(60000000,false);w=h.window(60000000,1);assert(w.observed_us==10000000&&w.awake_us==10000000);
 h.observe(1,false);w=h.window(1,24);assert(!w.observed_us&&!w.awake_us);
 // Long worker stalls have bounded work and keep the physically retained panel state.
 h.observe(100*BatteryHistory::Retention,false);w=h.window(100*BatteryHistory::Retention,24);assert(w.observed_us==BatteryHistory::Retention&&!w.awake_us);

 View v;assert(v.battery_hours==1);v.screen=Screen::Launcher;v.touch(180,370,1);assert(v.screen==Screen::Battery);
 for(auto p:{std::pair<int,int>{82,1},{164,6},{246,12},{328,24}}){v.touch(p.first,125,2);assert(v.battery_hours==p.second&&!v.dirty&&!v.radio_request);}
 v.touch(20,60,3);assert(v.screen==Screen::Launcher);v.button(4);assert(v.screen==Screen::Face);
 // Explicit sleep wins over USB power and consumes the next wake without navigating.
 v.screen=Screen::Battery;v.power_button(100);v.tick(1000,true);assert(v.asleep&&v.manual_sleep&&v.screen==Screen::Battery);
 assert(!v.wrist_raise(1001));v.power_button(2000);assert(!v.asleep&&!v.manual_sleep&&v.screen==Screen::Battery);
 v.power_button(3000);v.touch(246,125,4000);assert(!v.asleep&&v.battery_hours==24);
 v.power_button(5000);v.button(6000);assert(!v.asleep&&v.screen==Screen::Battery);
 v.power_button(7000);Gesture g;assert(g.update(v,true,164,125,8000));g.update(v,false,0,0,9000);assert(!v.asleep&&!v.manual_sleep&&v.battery_hours==24);
 // Startup latch clear and event ack only touch the owned bit, including bus failures.
 std::array<uint8_t,256> regs{};regs[0x41]=0xa5;regs[0x49]=0xf8;bool fail_read=false,fail_write=false;
 auto read=[&](uint8_t r,uint8_t& out){out=regs[r];return !fail_read;};
 auto write=[&](uint8_t r,uint8_t x){if(fail_write)return false;if(r==0x49){assert(x==8);regs[r]&=~x;}else regs[r]=x;return true;};
 assert(PowerKey::init(read,write)&&regs[0x41]==0xad&&regs[0x49]==0xf0);
 bool pressed=true;assert(PowerKey::poll(read,write,pressed)&&!pressed);
 regs[0x49]|=8;fail_write=true;assert(!PowerKey::poll(read,write,pressed)&&!pressed&&regs[0x49]==0xf8);
 fail_write=false;assert(PowerKey::poll(read,write,pressed)&&pressed&&regs[0x49]==0xf0);
 assert(PowerKey::poll(read,write,pressed)&&!pressed);fail_read=true;assert(!PowerKey::poll(read,write,pressed)&&!pressed);
 // All app states preserve framebuffer guards, even unknown and low battery.
 std::array<uint16_t,Width*Height+2> pixels;pixels.fill(0xbeef);Model m;m.battery_history=&h;v={};v.screen=Screen::Battery;
 for(int level:{-1,0,1,100})for(uint8_t scale:{1,6,12,24}){m.battery=level;v.battery_hours=scale;render(pixels.data()+1,m,v,1);assert(pixels.front()==0xbeef&&pixels.back()==0xbeef);}
}

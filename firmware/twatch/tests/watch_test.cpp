// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "watch.hpp"
#include <cassert>
#include <array>
#include <algorithm>
#include <cstring>
int main(){using namespace watch;View v;assert(valid(v.settings));v.touch(200,460,1);assert(v.screen==Screen::Launcher);v.touch(100,250,2);assert(v.screen==Screen::Settings);v.touch(100,150,3);assert(v.settings.brightness==50&&v.dirty);v.touch(100,220,4);assert(v.settings.timeout_s==60);v.tick(1200000004);assert(v.asleep);v.touch(100,150,1200000005);assert(!v.asleep&&v.settings.brightness==50);v.touch(20,35,1200000006);assert(v.screen==Screen::Launcher);v.touch(100,150,1200000007);assert(v.screen==Screen::Faces);v.touch(100,160,1200000008);assert(v.screen==Screen::Face&&v.settings.face==0);
 v.asleep=false;v.screen=Screen::Settings;v.dirty=false;v.touch(100,300,10);assert(v.radio_request==1&&!v.dirty);v.radio_request=0;v.touch(100,380,11);assert(v.radio_request==2&&!v.dirty);v.radio_request=0;v.asleep=true;v.touch(100,300,12);assert(!v.asleep&&v.radio_request==0);
 v.screen=Screen::Face;
 Settings legacy;legacy.version=2;legacy.face=1;legacy.brightness=70;legacy.timeout_s=180;
 assert(migrate(legacy)&&valid(legacy)&&legacy.timeout_s==30&&legacy.face==1&&legacy.brightness==70);
 assert(!migrate(legacy));
 assert(!std::strcmp(zone_name(2,-360),"CST")&&!std::strcmp(zone_name(2,-300),"CDT"));
 char label[32];hold_text(label,sizeof label,58300000);assert(!std::strcmp(label,"58 s"));
 hold_text(label,sizeof label,999999);assert(!std::strcmp(label,"999 ms"));
 hold_text(label,sizeof label,900);assert(!std::strcmp(label,"900 us"));
 int order=0;assert(commit_frame(true,[&](){assert(order==0);order=1;return true;},[&](){assert(order==1);order=2;return true;}));assert(order==2);
 assert(!commit_frame(true,[](){return false;},[](){assert(false);return true;}));
 View powered;powered.asleep=true;powered.tick(1000000000,true);assert(!powered.asleep);
 powered.tick(1029999999);assert(!powered.asleep);powered.tick(1030000000);assert(powered.asleep);
 Settings bad;bad.face=255;assert(!valid(bad));bad={};bad.timeout_s=0;assert(!valid(bad));
 std::array<uint16_t,Width*Height+2> pixels;pixels.fill(0xbeef);Model m;m.time_valid=true;m.utc_us=1794679698000000ULL;m.sampled_us=1;m.until_us=1000000;m.rms_ns=UINT32_MAX-1;m.hold_valid=true;m.hold_trials=100;m.hold_ms=UINT32_MAX;m.source=Source::BLE;
 render(pixels.data()+1,m,v,2);assert(pixels.front()==0xbeef&&pixels.back()==0xbeef);auto live=pixels;
 render(pixels.data()+1,m,v,250001);assert(pixels!=live);m.time_valid=false;auto expired=pixels;render(pixels.data()+1,m,v,2);assert(pixels==expired);
 for(auto screen:{Screen::Face,Screen::Launcher,Screen::Settings,Screen::Faces}){v.screen=screen;render(pixels.data()+1,m,v,2);assert(pixels.front()==0xbeef&&pixels.back()==0xbeef);}
 v.asleep=true;render(pixels.data()+1,m,v,2);assert(std::all_of(pixels.begin()+1,pixels.end()-1,[&](auto p){return p==pixels[1];}));
 // Analog lower bounds move the needle and carry an explicit >= unit mark.
 View analog;analog.settings.face=1;Model bounded;bounded.time_valid=true;bounded.sampled_us=1;bounded.until_us=1000000;bounded.utc_us=1794679698000000ULL;bounded.source=Source::BLE;bounded.hold_valid=true;bounded.hold_trials=100;bounded.hold_ms=58000;bounded.hold_lower=true;
 render(pixels.data()+1,bounded,analog,2);auto lower=pixels;
 bounded.hold_valid=false;render(pixels.data()+1,bounded,analog,2);assert(pixels!=lower);
 bounded.hold_valid=true;bounded.hold_lower=false;render(pixels.data()+1,bounded,analog,2);assert(pixels!=lower);
 // Solar uses its named location even when authenticated mesh metadata is elsewhere.
 View solar;solar.settings.face=2;Model sky;sky.time_valid=true;sky.utc_us=1794769200000000ULL;sky.sampled_us=1;sky.until_us=1000000;
 sky.calendar=calendar::snapshot(sky.utc_us/1000000,true);sky.tz_minutes=540;sky.tz_id=0;
 render(pixels.data()+1,sky,solar,2);auto japan=pixels;sky.tz_minutes=-360;sky.tz_id=2;
 render(pixels.data()+1,sky,solar,2);assert(pixels==japan);
 sky.hold_valid=true;sky.hold_trials=100;sky.hold_ms=800;sky.source=Source::BLE;
 render(pixels.data()+1,sky,solar,2);assert(pixels.front()==0xbeef&&pixels.back()==0xbeef);

 // Every backend morning/evening crossing is actually visible on the orbit.
 // At solar noon the opposed clock leaves both twilight clusters unobstructed.
 const auto day=calendar::solar({2026,11,15});sky.utc_us=day.noon*1000000;sky.calendar=calendar::snapshot(day.noon,true);
 render(pixels.data()+1,sky,solar,2);
 const uint16_t marker=uint16_t(((211>>3)<<11)|((235>>2)<<5)|(250>>3));
 for(uint64_t t:{day.sunrise,day.sunset,day.dawn,day.dusk,day.nautical_dawn,day.nautical_dusk,day.astronomical_dawn,day.astronomical_dusk}){
  assert(t);const double a=watch::solar::civil_angle(t,calendar::central_offset(t))*3.14159265358979323846/180;
  const int x=205+int(std::lround(124*std::sin(a))),y=251-int(std::lround(124*std::cos(a)));
  assert(pixels[1+y*Width+x]==marker);
 }

}

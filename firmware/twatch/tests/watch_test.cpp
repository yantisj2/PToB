// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "watch.hpp"
#include <cassert>
#include <array>
#include <algorithm>
int main(){using namespace watch;View v;assert(valid(v.settings));v.touch(200,460,1);assert(v.screen==Screen::Launcher);v.touch(100,250,2);assert(v.screen==Screen::Settings);v.touch(100,150,3);assert(v.settings.brightness==50&&v.dirty);v.touch(100,260,4);assert(v.settings.timeout_s==120);v.tick(1200000004);assert(v.asleep);v.touch(100,150,1200000005);assert(!v.asleep&&v.settings.brightness==50);v.touch(20,35,1200000006);assert(v.screen==Screen::Launcher);v.touch(100,150,1200000007);assert(v.screen==Screen::Faces);v.touch(100,160,1200000008);assert(v.screen==Screen::Face&&v.settings.face==0);
 Settings bad;bad.face=255;assert(!valid(bad));bad={};bad.timeout_s=0;assert(!valid(bad));
 std::array<uint16_t,Width*Height+2> pixels;pixels.fill(0xbeef);Model m;m.time_valid=true;m.utc_us=1794679698000000ULL;m.sampled_us=1;m.until_us=1000000;m.rms_ns=UINT32_MAX-1;m.hold_valid=true;m.hold_trials=100;m.hold_ms=UINT32_MAX;m.source=Source::BLE;
 render(pixels.data()+1,m,v,2);assert(pixels.front()==0xbeef&&pixels.back()==0xbeef);auto live=pixels;
 render(pixels.data()+1,m,v,250001);assert(pixels!=live);m.time_valid=false;auto expired=pixels;render(pixels.data()+1,m,v,2);assert(pixels==expired);
 for(auto screen:{Screen::Face,Screen::Launcher,Screen::Settings,Screen::Faces}){v.screen=screen;render(pixels.data()+1,m,v,2);assert(pixels.front()==0xbeef&&pixels.back()==0xbeef);}
 v.asleep=true;render(pixels.data()+1,m,v,2);assert(std::all_of(pixels.begin()+1,pixels.end()-1,[&](auto p){return p==pixels[1];}));
 // Analog lower-bound-only evidence is unavailable, never an exact dial value.
 View analog;analog.settings.face=1;Model bounded;bounded.time_valid=true;bounded.sampled_us=1;bounded.until_us=1000000;bounded.utc_us=1794679698000000ULL;bounded.source=Source::BLE;bounded.hold_valid=true;bounded.hold_trials=100;bounded.hold_ms=58000;bounded.hold_lower=true;
 render(pixels.data()+1,bounded,analog,2);auto lower=pixels;
 bounded.hold_valid=false;render(pixels.data()+1,bounded,analog,2);assert(pixels==lower);
 bounded.hold_valid=true;bounded.hold_lower=false;render(pixels.data()+1,bounded,analog,2);assert(pixels!=lower);
}

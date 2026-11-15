// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "watch.hpp"
#include <cassert>
int main(){
 watch::PresentationClock clock;watch::Model input;assert(!clock.present(input,1000).time_valid);
 input.time_valid=true;input.utc_us=100000000;input.sampled_us=1000;input.until_us=200000;input.tz_minutes=-360;input.source=watch::Source::BLE;input.locked=true;input.hold_valid=true;input.hold_trials=100;input.rms_ns=87;
 auto output=clock.present(input,1001);assert(!output.local_time&&output.locked);
 input.time_valid=false;input.utc_us=0;input.tz_minutes=0;
 output=clock.present(input,300000);assert(output.time_valid&&output.local_time&&output.holding&&!output.locked&&output.source==watch::Source::None);
 assert(output.utc_us==100299000&&output.tz_minutes==-360&&output.rms_ns==UINT32_MAX&&!output.hold_valid&&output.hold_trials==0);
 assert(!clock.present(input,86400001000ULL).time_valid); // finite display-only fallback
 assert(!clock.present(input,999).time_valid); // monotonic reset cannot retain an old anchor
 watch::View v;assert(v.settings.timeout_s==90);v.tick(89999999);assert(!v.asleep);v.tick(90000000);assert(v.asleep);
 v.asleep=false;v.screen=watch::Screen::Faces;v.touch(200,300,90000001);assert(v.settings.face==1&&v.dirty&&v.screen==watch::Screen::Face);
 assert(watch::valid(v.settings));v.screen=watch::Screen::Settings;v.touch(200,260,90000002);assert(v.settings.timeout_s==120);v.touch(200,260,90000003);assert(v.settings.timeout_s==180);v.touch(200,260,90000004);assert(v.settings.timeout_s==90);
 // A drag over MODE changes face only on release; it must not open the launcher.
 watch::Model radio;radio.source=watch::Source::RC9;assert(watch::receiver_source(radio,true)==watch::Source::RC9);
 radio.source=watch::Source::RC1;assert(watch::receiver_source(radio,true)==watch::Source::BLE);
 radio.source=watch::Source::RC2;assert(watch::receiver_source(radio,true)==watch::Source::BLE);
 radio.source=watch::Source::LOR;assert(watch::receiver_source(radio,true)==watch::Source::LOR);
 radio.holding=true;assert(watch::receiver_source(radio,true)==watch::Source::None);
 radio.holding=false;assert(watch::receiver_source(radio,false)==watch::Source::None);
 watch::Gesture g;v={};assert(!g.update(v,true,300,455,1));assert(v.screen==watch::Screen::Face);
 g.update(v,true,100,455,50000);assert(g.update(v,false,0,0,60000));assert(v.settings.face==1&&v.dirty&&v.screen==watch::Screen::Face);
 v.dirty=false;assert(!g.update(v,false,0,0,70000));assert(!v.dirty);
 g.update(v,true,80,200,80000);g.update(v,true,280,210,100000);g.update(v,false,0,0,110000);assert(v.settings.face==0);
 // Wake consumes the entire gesture, including a large swipe.
 v.asleep=true;assert(g.update(v,true,300,200,120000));assert(!g.update(v,true,80,200,130000));g.update(v,false,0,0,140000);assert(!v.asleep&&v.settings.face==0);
 // Vertical scroll, long hold and navigation during contact cannot select a face.
 g.update(v,true,300,100,150000);g.update(v,true,100,400,160000);g.update(v,false,0,0,170000);assert(v.settings.face==0);
 g.update(v,true,300,200,180000);g.update(v,true,80,200,190000);g.update(v,false,0,0,2000000);assert(v.settings.face==0);
 g.update(v,true,200,455,2100000);assert(g.update(v,false,0,0,2110000));assert(v.screen==watch::Screen::Launcher);
 v.screen=watch::Screen::Settings;auto brightness=v.settings.brightness;
 g.update(v,true,300,150,2200000);g.update(v,true,80,150,2210000);g.update(v,false,0,0,2220000);assert(v.settings.brightness==brightness&&v.settings.face==0);
}

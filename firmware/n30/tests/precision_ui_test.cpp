// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ui.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <initializer_list>
int main(){
 n30::Model m;n30::LockTimer timer;uint64_t now=1000000;
 auto sample=[&](uint64_t at){now=at;m.sampled_us=at;m.precision.until_us=at+n30::SnapshotMaxUs;m.precision.valid=true;};
 sample(now);m.precision.scope=1;m.precision.locked=true;timer.update(m,now);
 assert(m.precision.lock_age_valid&&m.precision.lock_since_us==now);
 sample(now+100000);timer.update(m,now);assert(m.precision.lock_since_us==1000000);
 m.precision.scope=2;timer.update(m,now);assert(m.precision.lock_since_us==now);
 sample(now+1000);m.precision.root_boot=9;timer.update(m,now);assert(m.precision.lock_since_us==now);
 sample(now+1000);m.precision.source_epoch=2;timer.update(m,now);assert(m.precision.lock_since_us==now);
 sample(now+1000);m.precision.quality_scope=3;timer.update(m,now);assert(m.precision.lock_since_us==now);
 m.precision.locked=false;timer.update(m,now);assert(!m.precision.lock_age_valid&&m.precision.lock_since_us==0);
 m.precision.locked=true;sample(now+100000);timer.update(m,now);const auto restarted=now;
 sample(now+100000);timer.update(m,now);assert(m.precision.lock_since_us==restarted);
 sample(now+n30::SnapshotMaxUs);timer.update(m,now);assert(m.precision.lock_since_us==now);
 sample(now-1000);timer.update(m,now);assert(m.precision.lock_since_us==now);
 timer.update(m,now+n30::SnapshotMaxUs);assert(!m.precision.lock_age_valid);
 struct Guard {uint8_t before[16],frame[n30::FrameBytes],after[16];} g;
 std::memset(&g,0xa5,sizeof g);n30::View v;v.page=n30::PrecisionPage;
 for(auto rms:{0u,1u,99u,999u,1000u,9999u,10000u,99999u,100000u,999999u,1000000u,999999999u,1000000000u,UINT32_MAX-1,UINT32_MAX})
 for(int32_t rate:{-50000,-5487,0,50000})for(bool locked:{false,true})for(uint32_t counter:{20000000u,80000000u}){
  sample(5000000000);m.precision.rms_ns=rms;m.precision.relative_ppb=rate;m.precision.relative_known=true;
  m.precision.locked=locked;m.precision.lock_age_valid=locked;m.precision.lock_since_us=now-754000000;
  m.precision.counter_hz=counter;
  n30::render(g.frame,m,v,now);for(auto x:g.before)assert(x==0xa5);for(auto x:g.after)assert(x==0xa5);
 }
 // The tick label follows the selected clock, and live residuals have a sign.
 uint8_t counter20[n30::FrameBytes],counter80[n30::FrameBytes];sample(5000000000);
 m.precision.counter_hz=20000000;n30::render(counter20,m,v,now);
 m.precision.counter_hz=80000000;n30::render(counter80,m,v,now);
 assert(std::memcmp(counter20+34*8,counter80+34*8,5*8));
 m.precision.relative_ppb=123;n30::render(counter20,m,v,now);
 m.precision.relative_ppb=-123;n30::render(counter80,m,v,now);
 assert(std::memcmp(counter20+86*8,counter80+86*8,7*8));
 // A24-character registry alias cannot write into the source/PPS field.
 std::snprintf(m.root,sizeof m.root,"ABCDEFGHIJKLMNOPQRSTUVWX");m.precision.source=n30::Source::Rc2;
 n30::render(counter20,m,v,now);std::snprintf(m.root,sizeof m.root,"ABCDEF");n30::render(counter80,m,v,now);
 for(unsigned row=18;row<25;row++)for(unsigned x=32;x<64;x++)assert((counter20[row*8+x/8]&(0x80>>(x%8)))==(counter80[row*8+x/8]&(0x80>>(x%8))));
 // Expired models cannot keep a previous number, ppb or lock duration live.
 uint8_t a[n30::FrameBytes],b[n30::FrameBytes];sample(6000000000);m.precision.rms_ns=170;
 n30::render(a,m,v,now+n30::SnapshotMaxUs);m.precision.rms_ns=85;m.precision.relative_ppb=1234;m.precision.lock_since_us=1;
 n30::render(b,m,v,now+n30::SnapshotMaxUs);assert(!std::memcmp(a,b,sizeof a));
 std::puts("PASS precision page: continuous lock, scope/loss/gap/expiry resets, live metrics and bounded rendering");
}

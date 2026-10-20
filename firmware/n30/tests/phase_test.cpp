// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "display_phase.hpp"
#include <cassert>
#include <array>
#include <initializer_list>
namespace {
n30::Model good(uint64_t now,uint64_t utc){n30::Model m;m.sampled_us=m.utc_at_us=now;m.until_us=now+n30::SnapshotMaxUs;m.utc_us=utc;m.valid=m.locked=true;m.state=n30::State::Follow;m.source=n30::Source::Ble;return m;}
bool pixel(const uint8_t* f,unsigned x,unsigned y){return f[y*8+x/8]&(0x80u>>(x%8));}
}
int main(){
 // Arbitrary boot phase, exact edges, new models, long misses and backwards UTC.
 for(unsigned page=0;page<3;page++)for(uint64_t phase:{0ULL,1ULL,124999ULL,125000ULL,249999ULL,999999ULL}){
  n30::View v;v.page=page;const uint64_t start=1234567;auto m=good(start,10000000+phase);n30::FrameCadence c;
  auto p=n30::frame_phase(m,v,start);assert(c.deadline(p,start)==start);c.rendered(p);
  assert(p.period==(page==1?125000:250000));assert(p.next-start==p.period-m.utc_us%p.period);const auto edge=p.next;
  auto before=good(edge-1,m.utc_us+edge-1-start);assert(c.deadline(n30::frame_phase(before,v,edge-1),edge-1)==edge);
  auto at=good(edge,m.utc_us+edge-start);auto b=n30::frame_phase(at,v,edge);assert(c.deadline(b,edge)==edge);c.rendered(b);assert(c.deadline(b,edge)==edge+p.period);
  const auto late=edge+5*p.period+731;auto delayed=good(late,at.utc_us+late-edge);auto d=n30::frame_phase(delayed,v,late);
  assert(c.deadline(d,late)==late-731);c.rendered(d);assert(c.deadline(d,late)==late+p.period-731);
  delayed.utc_us+=321;assert(c.deadline(n30::frame_phase(delayed,v,late),late)==d.next-321);
  delayed.utc_us-=2*p.period;auto back=n30::frame_phase(delayed,v,late);assert(c.deadline(back,late)<=late);c.rendered(back);assert(c.deadline(back,late)>late);
  delayed.valid=false;auto off=n30::frame_phase(delayed,v,late);assert(!off.utc&&c.deadline(off,late)==late);c.rendered(off);assert(c.deadline(off,late)>late);
  v.saver=true;assert(n30::frame_phase(at,v,edge).period==250000);
 }
 // A minute of irregular boot phase produces only the intended4/8Hz slots.
 for(unsigned page=0;page<3;page++){
  n30::View v;v.page=page;n30::FrameCadence c;unsigned frames=0;const uint64_t offset=777777,start=234567;
  for(uint64_t now=start;now<start+60000000;now+=1000){auto m=good(now,now+offset);auto p=n30::frame_phase(m,v,now);if(c.deadline(p,now)<=now){if(frames)assert((now+offset)%p.period<1000);c.rendered(p);frames++;}}
  assert(frames==(page==1?481:241));
 }
 // Page/wake transfer coalesces with a nearby edge; no catch-up after an
 // occupied slot. Test all starting phases and both dashboard/RF cadences.
 for(unsigned page=0;page<3;page++)for(uint64_t phase=0;phase<1000000;phase+=997){
  n30::View v;v.page=page;n30::FrameCadence c;auto m=good(2000000,10000000+phase);auto p=n30::frame_phase(m,v,2000000);
  auto due=c.forced_deadline(p,2000000);assert(due>=2000000&&due-2000000<n30::FrameWorkUs);
  if(p.next-2000000<n30::FrameWorkUs)assert(due==p.next);else assert(due==2000000);
  auto late=good(2000000+49999,m.utc_us+49999);auto end=n30::frame_phase(late,v,2000000+49999);c.completed(p,end);
  assert(c.deadline(end,2000000+49999)>2000000+49999);
 }
 // One shared UTC pulse, all admitted paths/positions; dark on expiry/HOLD.
 for(auto src:{n30::Source::Ble,n30::Source::Rc1,n30::Source::Rc2})for(unsigned pos=0;pos<3;pos++)for(uint64_t phase:{999999ULL,0ULL,1ULL,249999ULL,250000ULL}){
  auto m=good(1000000,10000000+phase);m.source=src;const bool on=phase<250000;assert(n30::pps_on(m,1000000)==on);
  for(bool saver:{false,true}){n30::View v;v.saver=saver;v.saver_position=pos;uint8_t f[1024];n30::render(f,m,v,1000000);assert(pixel(f,57,saver?9+22*pos:18)==on);}
  m.locked=false;assert(!n30::pps_on(m,1000000));m.locked=true;m.state=n30::State::Hold;assert(!n30::pps_on(m,1000000));m.state=n30::State::Follow;m.mode_pending=true;assert(!n30::pps_on(m,1000000));m.mode_pending=false;assert(!n30::pps_on(m,1250000));
 }
 // Grace holds every clock pixel except current RMS/lock and a stale PPS lamp.
 for(bool saver:{false,true})for(unsigned pos=0;pos<3;pos++){
  n30::ClockHold h;n30::View v;v.saver=saver;v.saver_position=pos;auto m=good(1000000,10000000);uint8_t f[1024],old[1024];
  h.update(m,v,1000000,saver);h.draw(old,m,v,1000000);h.presented(m,v,1000000,old);m.valid=m.locked=false;v.saver=false;
  h.update(m,v,1100000,saver);assert(v.clock_frozen&&v.saver==saver);h.draw(f,m,v,1100000);const unsigned top=saver?7+22*pos:16;
  for(unsigned y=0;y<128;y++)for(unsigned x=0;x<64;x++)if(x>=56&&x<59&&y>=top&&y<top+5)assert(!pixel(f,x,y));else if(saver||y<103||y>=116)assert(pixel(f,x,y)==pixel(old,x,y));
 }
 // Real changed-span driver: PPS page first, no duplicate writes, all-or-none
 // shadow history on failure; next frame repairs every page in either rotation.
 for(bool cw:{false,true}){
  std::array<uint8_t,1024> f{},shadow{};bool valid=false;unsigned calls=0;
  auto send=[&](unsigned page,unsigned first,const uint8_t* data,unsigned size){assert(page==(cw?7-calls:calls)&&first==0&&size==128&&data==f.data()+128*page);calls++;return true;};
  assert(n30::flush_clock_first(f.data(),shadow.data(),valid,cw,send)&&valid&&calls==8);calls=0;assert(n30::flush_clock_first(f.data(),shadow.data(),valid,cw,send)&&calls==0);
  const unsigned page=cw?7:0;f[page*128+101]=42;
  assert(n30::flush_clock_first(f.data(),shadow.data(),valid,cw,[&](unsigned p,unsigned first,const uint8_t*,unsigned size){calls++;return p==page&&first==101&&size==1;}));assert(calls==1&&shadow==f);
  f[page*128+102]=43;const auto old=shadow;assert(!n30::flush_clock_first(f.data(),shadow.data(),valid,cw,[](auto...){return false;})&&!valid&&shadow==old);
  calls=0;assert(n30::flush_clock_first(f.data(),shadow.data(),valid,cw,send)&&calls==8&&shadow==f);
 }
}

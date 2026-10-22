// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "display_phase.hpp"
#include <cassert>
#include <cstring>
#include <initializer_list>
namespace {
n30::Model good(uint64_t now,uint64_t utc){n30::Model m;m.sampled_us=m.utc_at_us=now;m.until_us=now+n30::SnapshotMaxUs;m.utc_us=utc;m.valid=m.locked=true;m.state=n30::State::Follow;m.source=n30::Source::Ble;return m;}
bool pixel(const uint8_t* f,unsigned x,unsigned y){return f[y*8+x/8]&(0x80u>>(x%8));}
}
int main(){
 static_assert(n30::SaverContrast*2==n30::NormalContrast);
 n30::View v;n30::Input input;n30::SaverTimer timer;
 input.idle(n30::IdleUs-1,true,v);timer.update(n30::IdleUs-1,v,true,false);assert(!v.saver&&!v.minimal);
 const auto start=n30::IdleUs+60000000; // Delayed first lock is not saver time.
 input.idle(start,true,v);timer.update(start,v,true,true);assert(v.saver&&!v.minimal);
 timer.update(start+n30::MinimalAfterUs-1,v,true,true);assert(!v.minimal);
 timer.update(start+n30::MinimalAfterUs,v,true,true);assert(v.minimal);
 // Physical wake is consumed, restarts residence, and retains held-button polling.
 input.update(true,start+n30::MinimalAfterUs+1,v);assert(input.polling());
 input.update(true,start+n30::MinimalAfterUs+40001,v);timer.update(start+n30::MinimalAfterUs+40001,v,true,false);assert(!v.saver&&!v.minimal);
 input.update(false,start+n30::MinimalAfterUs+50001,v);assert(input.polling());
 input.update(false,start+n30::MinimalAfterUs+90001,v);assert(!input.polling()&&v.page==0);
 input.idle(start+n30::MinimalAfterUs+n30::IdleUs+90001,true,v);timer.update(start+n30::MinimalAfterUs+n30::IdleUs+90001,v,true,true);assert(v.saver&&!v.minimal);
 // Disappearance, hidden surfaces and monotonic reversal reset actual residence.
 for(unsigned reason=0;reason<4;reason++){
  n30::SaverTimer t;n30::View w;w.saver=true;t.update(1000000,w,true,true);t.update(1000000+n30::MinimalAfterUs,w,true,true);assert(w.minimal);
  if(reason==0)w.saver=false;if(reason==1)w.help=true;if(reason==2)w.blank=true;
  t.update(reason==3?1:1000001+n30::MinimalAfterUs,w,true,reason!=0);assert(!w.minimal);
  w={};w.saver=true;t.update(reason==3?2:2000000+n30::MinimalAfterUs,w,true,true);assert(!w.minimal);
 }
 for(auto source:{n30::Source::Ble,n30::Source::Rc1,n30::Source::Rc2,n30::Source::LoRa})for(unsigned pos=0;pos<3;pos++){
  n30::View w;w.saver=w.minimal=true;w.saver_position=pos;const unsigned top=4+22*pos;
  auto m=good(1000000,10000000);m.source=source;uint8_t f[1024],on[1024],normal[1024];
  n30::render(on,m,w,1000000);assert(pixel(on,57,top+5));
  for(unsigned y=0;y<128;y++)if(y<top||y>=top+11)for(unsigned x=0;x<64;x++)assert(!pixel(on,x,y));
  w.minimal=false;n30::render(normal,m,w,1000000);assert(std::memcmp(normal,on,1024));
  for(unsigned y=top;y<top+11;y++)assert(!std::memcmp(normal+y*8,on+y*8,8));
  w.minimal=true;m=good(1250000,10250000);m.source=source;n30::render(f,m,w,1250000);assert(!pixel(f,57,top+5));
  for(unsigned y=0;y<128;y++)for(unsigned x=0;x<64;x++)if(!(x>=56&&x<59&&y>=top+3&&y<top+8))assert(pixel(f,x,y)==pixel(on,x,y));
  // Loss right at minimal entry cannot resurrect cached dial/footer or PPS.
  n30::ClockHold hold;w.minimal=false;m=good(1000000,10000000);hold.update(m,w,1000000,true);hold.presented(m,w,1000000,normal);
  m.valid=m.locked=false;w.saver=false;hold.update(m,w,1100000,true);w.minimal=true;hold.draw(f,m,w,1100000);
  assert(w.clock_frozen&&w.saver&&hold.deadline()==1600000);
  for(unsigned y=top+11;y<128;y++)for(unsigned x=0;x<64;x++)assert(!pixel(f,x,y));
  assert(!pixel(f,57,top+5));
 }
 // Arbitrary phase/model corrections, missed slots, pulse boundaries. Exactly
 // two routine frames per second; zero renders during the750ms dark interval.
 for(unsigned page=0;page<n30::PageCount;page++){
  n30::View w;w.page=page;w.saver=w.minimal=true;n30::FrameCadence cadence;unsigned frames=0;
  for(uint64_t now=1000000;now<61000000;now+=1000){
   auto m=good(now,now+777000);auto phase=n30::frame_phase(m,w,now);
   if(cadence.deadline(phase,now)<=now){if(frames)assert((now+777000)%1000000==0||(now+777000)%1000000==250000);cadence.completed(phase,phase);frames++;}
  }
  assert(frames==121);
  for(uint64_t us:{0ULL,1ULL,249999ULL,250000ULL,250001ULL,999999ULL}){
   auto m=good(2000000,10000000+us);auto p=n30::frame_phase(m,w,2000000);assert(p.period==1000000);
   cadence.rendered(p);assert(p.next-2000000==(us<250000?250000-us:1000000-us));
   m.utc_us+=37;auto changed=n30::frame_phase(m,w,2000000);assert(cadence.deadline(changed,2000000)<=changed.next);
   m.utc_us+=9000000;assert(cadence.deadline(n30::frame_phase(m,w,2000000),2000000)<=2000000);
  }
 }
}

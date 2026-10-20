// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "ui.hpp"
#include <cassert>
#include <cstring>
#include <initializer_list>
namespace {
n30::Model good(uint64_t now){
 n30::Model m;m.sampled_us=m.utc_at_us=now;m.until_us=now+n30::SnapshotMaxUs;
 m.utc_us=36642900000;m.hz=16;m.rms_ns=65;m.delivery=950;
 m.valid=m.locked=true;m.state=n30::State::Follow;m.source=n30::Source::Ble;return m;
}
void present(n30::ClockHold& hold,n30::Model& m,n30::View& v,uint64_t now,uint8_t* frame,bool saver_due=false){
 hold.update(m,v,now,saver_due);hold.draw(frame,m,v,now);hold.presented(m,v,now,frame);
}
}
int main(){
 const uint64_t start=1000000,loss=start+100000;
 n30::View v;n30::ClockHold hold;auto m=good(start);
 uint8_t original[1024],frozen[1024],current[1024],failed[1024];present(hold,m,v,start,original);
 m.valid=m.locked=false;m.state=n30::State::Acquire;m.source=n30::Source::None;m.delivery=0;
 hold.update(m,v,loss);assert(v.clock_frozen);hold.draw(frozen,m,v,loss);n30::render(current,m,v,loss);
 // Only current RMS/lock rows may differ: UTC digits, analog, source/header
 // and footer all retain the same actually displayed frame for this grace.
 for(unsigned y=0;y<128;y++)assert(!std::memcmp(frozen+y*8,(y>=103&&y<116?current:original)+y*8,8));
 assert(std::memcmp(original+103*8,frozen+103*8,13*8));
 uint64_t utc;assert(!m.valid&&!m.locked&&!n30::time(m,loss,utc));assert(!std::strcmp(n30::source_label(m,loss),"WAIT"));
 for(uint64_t dt:{uint64_t(1),uint64_t(249999),uint64_t(499999)}){
  hold.update(m,v,loss+dt);assert(v.clock_frozen);hold.draw(frozen,m,v,loss+dt);
  assert(!std::memcmp(original,frozen,103*8));hold.presented(m,v,loss+dt,frozen);
 }
 // Even a delayed draw cannot display cached time at/after the hard deadline.
 hold.draw(failed,m,v,loss+n30::ClockGraceUs);assert(std::memcmp(original,failed,103*8));
 hold.update(m,v,loss+n30::ClockGraceUs);assert(!v.clock_frozen);
 hold.update(m,v,loss+2*n30::ClockGraceUs);assert(!v.clock_frozen);
 m=good(3000000);present(hold,m,v,3000000,original);m.locked=false;hold.update(m,v,3100000);assert(v.clock_frozen);
 m=good(3200000);present(hold,m,v,3200000,original);assert(!v.clock_frozen);
 m.state=n30::State::Fault;hold.update(m,v,3300000);assert(v.clock_frozen);hold.update(m,v,3800000);assert(!v.clock_frozen);
 // No initial clock, failed transfer, RF/help/blank or a new saver surface
 // can seed an old clock on return. Frame history is committed only on success.
 for(unsigned which=0;which<6;which++){
  n30::ClockHold h;n30::View w;auto live=good(4000000);h.update(live,w,4000000);h.draw(original,live,w,4000000);
  if(which!=0)h.presented(live,w,4000000,original);
  if(which==1)h.reset();if(which==2)w.page=1;if(which==3)w.help=true;if(which==4)w.blank=true;if(which==5)w.saver=true;
  h.update(live,w,4000010,true);w.page=0;w.help=w.blank=w.saver=false;
  live.locked=false;h.update(live,w,4100000);assert(!w.clock_frozen);
 }
 n30::ClockHold backwards;n30::View b;m=good(5000000);present(backwards,m,b,5000000,original);
 m.locked=false;backwards.update(m,b,5100000);assert(b.clock_frozen);backwards.update(m,b,5099999);assert(!b.clock_frozen);backwards.update(m,b,5100001);assert(!b.clock_frozen);
 // Exercise the real idle eligibility path at every moving saver position,
 // on either underlying page. A bad snapshot must never flash the top page.
 for(unsigned page=0;page<2;page++)for(unsigned pos=0;pos<3;pos++){
  n30::ClockHold h;n30::Input input;n30::View s;s.page=page;
  const uint64_t at=n30::IdleUs+pos*n30::MoveUs+1000000;auto live=good(at);
  input.idle(at,true,s);assert(s.saver&&s.saver_position==pos);present(h,live,s,at,original,input.saver_due(at,s));
  live.locked=live.valid=false;const auto lost=at+100000;
  for(uint64_t dt:{uint64_t(0),uint64_t(100000),uint64_t(499999)}){
   input.idle(lost+dt,false,s);assert(!s.saver); // Original immediate-exit bug.
   h.update(live,s,lost+dt,input.saver_due(lost+dt,s));assert(s.saver&&s.clock_frozen&&s.saver_position==pos);
   h.draw(frozen,live,s,lost+dt);assert(!std::memcmp(original,frozen,1024));h.presented(live,s,lost+dt,frozen);
  }
  live=good(lost+499999);input.idle(lost+499999,true,s);h.update(live,s,lost+499999,input.saver_due(lost+499999,s));
  assert(s.saver&&!s.clock_frozen&&s.saver_position==pos);h.draw(frozen,live,s,lost+499999);h.presented(live,s,lost+499999,frozen);
  // Another brief dip starts a fresh bound only after that live presentation.
  live.valid=live.locked=false;const auto again=lost+600000;input.idle(again,false,s);h.update(live,s,again,input.saver_due(again,s));assert(s.saver&&s.clock_frozen);
  input.idle(again+500000,false,s);h.update(live,s,again+500000,input.saver_due(again+500000,s));assert(!s.saver&&!s.clock_frozen);
 }
 // Wake immediately consumes the saver grace and the long wake cannot toggle.
 n30::ClockHold h;n30::Input input;n30::View s;const auto at=n30::IdleUs+2*n30::MoveUs+1000000;m=good(at);input.idle(at,true,s);present(h,m,s,at,original,true);
 m.valid=m.locked=false;input.idle(at+100000,false,s);h.update(m,s,at+100000,true);assert(s.saver);
 input.update(true,at+110000,s);input.update(true,at+150000,s);input.idle(at+150000,false,s);h.update(m,s,at+150000,input.saver_due(at+150000,s));assert(!s.saver&&!s.clock_frozen);
 assert(input.update(true,at+2000000,s)==n30::Action::None);
 // An ordinary30s movement boundary does not become a normal-page roundtrip.
 n30::ClockHold moving;n30::Input idle;n30::View mv;const auto edge=n30::IdleUs+2*n30::MoveUs;
 m=good(edge-100000);idle.idle(edge-100000,true,mv);assert(mv.saver_position==1);present(moving,m,mv,edge-100000,original,true);
 m.locked=false;idle.idle(edge-50000,false,mv);moving.update(m,mv,edge-50000,true);idle.idle(edge+50000,false,mv);moving.update(m,mv,edge+50000,true);assert(mv.saver&&mv.saver_position==1);
 m=good(edge+100000);idle.idle(edge+100000,true,mv);moving.update(m,mv,edge+100000,true);assert(mv.saver&&!mv.clock_frozen&&mv.saver_position==2);
}

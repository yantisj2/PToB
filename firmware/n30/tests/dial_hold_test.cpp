// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "ui.hpp"
#include <cassert>
#include <cstring>
#include <initializer_list>
namespace {
n30::Model good(uint64_t now){
 n30::Model m;m.sampled_us=m.utc_at_us=now;m.until_us=now+n30::SnapshotMaxUs;
 m.utc_us=36642000000;m.hz=16;m.valid=m.locked=true;m.state=n30::State::Follow;m.source=n30::Source::Ble;return m;
}
}
int main(){
 const uint64_t start=1000000,loss=start+100000;
 n30::View v;n30::DialHold hold;auto m=good(start);
 uint8_t original[1024],frozen[1024],failed[1024];n30::render(original,m,v,start);
 hold.update(m,v,start);assert(!v.dial_frozen);hold.presented(m,v,start);
 const auto original_model=m;m.valid=m.locked=false;m.state=n30::State::Acquire;
 hold.update(m,v,loss);assert(v.dial_frozen&&v.dial_utc==original_model.utc_us&&v.dial_hz==16);
 n30::render(frozen,m,v,loss);
 // Freeze precisely the old dial pixels, with truthful invalid digital/status
 // and no retained lock. Do not freeze or extrapolate the whole clock model.
 assert(!std::memcmp(original+42*8,frozen+42*8,49*8));
 assert(std::memcmp(original,frozen,42*8));assert(std::memcmp(original+93*8,frozen+93*8,35*8));
 assert(!m.valid&&!m.locked&&m.state==n30::State::Acquire);
 for(uint64_t dt:{uint64_t(1),uint64_t(249999),uint64_t(499999)}){
  hold.update(m,v,loss+dt);assert(v.dial_frozen&&v.dial_utc==original_model.utc_us);
  hold.presented(m,v,loss+dt); // Frozen redraws never rearm/extend the grace.
 }
 hold.update(m,v,loss+n30::DialGraceUs);assert(!v.dial_frozen);
 n30::render(failed,m,v,loss+n30::DialGraceUs);
 assert(std::memcmp(original+42*8,failed+42*8,49*8));
 hold.update(m,v,loss+2*n30::DialGraceUs);assert(!v.dial_frozen);
 // Reacquisition can display immediately, then starts a new grace on loss.
 m=good(3000000);hold.update(m,v,3000000);assert(!v.dial_frozen);hold.presented(m,v,3000000);
 m.locked=false;hold.update(m,v,3100000);assert(v.dial_frozen);
 m=good(3200000);m.utc_us+=1000000;hold.update(m,v,3200000);assert(!v.dial_frozen);hold.presented(m,v,3200000);
 m.state=n30::State::Fault;hold.update(m,v,3300000);assert(v.dial_frozen&&v.dial_utc==m.utc_us);
 hold.update(m,v,3800000);assert(!v.dial_frozen);
 // No initial dial, failed transfer, hidden page/help/blank or saver can seed
 // a grace on return. A monotonic clock reversal also discards cached state.
 for(unsigned which=0;which<6;which++){
  n30::DialHold h;n30::View w;auto live=good(4000000);h.update(live,w,4000000);
  if(which!=0)h.presented(live,w,4000000);
  if(which==1)h.reset(); // Worker discards history after any transfer failure.
  if(which==2)w.page=1;if(which==3)w.help=true;if(which==4)w.blank=true;if(which==5)w.saver=true;
  h.update(live,w,4000010);w.page=0;w.help=w.blank=w.saver=false;
  live.locked=false;h.update(live,w,4100000);assert(!w.dial_frozen);
 }
 n30::DialHold backwards;n30::View b;m=good(5000000);backwards.update(m,b,5000000);backwards.presented(m,b,5000000);
 m.locked=false;backwards.update(m,b,5100000);assert(b.dial_frozen);
 backwards.update(m,b,5099999);assert(!b.dial_frozen);backwards.update(m,b,5100001);assert(!b.dial_frozen);
 // A stale/expired producer snapshot can retain only frozen pixels, never time.
 n30::DialHold expired;n30::View e;m=good(6000000);expired.update(m,e,6000000);expired.presented(m,e,6000000);
 uint64_t utc;expired.update(m,e,6250000);assert(e.dial_frozen&&!n30::time(m,6250000,utc));
 expired.update(m,e,6750000);assert(!e.dial_frozen);
}

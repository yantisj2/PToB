// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "ui.hpp"
#include <cassert>
#include <cstring>
#include <initializer_list>
int main(){
 n30::SignalStrength owner;constexpr uint64_t at=1000000;
 assert(!owner.snapshot(1,2,at).fresh(at));
 owner.observe(1,2,-45,at,at+1);auto s=owner.snapshot(1,2,at+1);assert(n30::signal_bars(s,at+1)==4);
 for(auto dbm:{127,-128,21}){owner.observe(1,2,dbm,at+1000,at+1001);assert(owner.snapshot(1,2,at+1001).at==at);}
 owner.observe(1,2,-100,at-1,at);owner.observe(1,2,-100,at,at);assert(owner.snapshot(1,2,at).dbm==-45);
 owner.observe(1,2,-100,at+1000,at+51000);assert(owner.snapshot(1,2,at+1001).at==at);
 assert(owner.snapshot(1,2,at+n30::SignalMaxUs-1).fresh(at+n30::SignalMaxUs-1));assert(!owner.snapshot(1,2,at+n30::SignalMaxUs).fresh(at+n30::SignalMaxUs));
 assert(!owner.snapshot(9,2,at+1).fresh(at+1)&&!owner.snapshot(1,3,at+1).fresh(at+1));
 owner.observe(9,3,-80,at+2000,at+2001);assert(owner.snapshot(9,3,at+2001).dbm==-80);
 owner.observe(9,3,-40,at+3000,at+3001);assert(owner.snapshot(9,3,at+3001).dbm==-70);
 // Test the actual four filled bars, including the formerly ambiguous first
 // bar. RF remains visible in HOLD, without UTC or a precision lock.
 for(int dbm:{-127,-86,-85,-71,-70,-56,-55,0,20}){
  n30::Model m;m.sampled_us=at;m.state=n30::State::Hold;m.signal={1,at,2,int16_t(dbm)};
  const unsigned expected=dbm>=-55?4:dbm>=-70?3:dbm>=-85?2:1;
  assert(n30::rf_bars(m,at)==expected);n30::View v;uint8_t f[n30::FrameBytes];n30::render(f,m,v,at);
  for(unsigned i=0;i<4;i++){const unsigned x=36+i*6,y=i?128-(2+i*2):125;const bool pixel=f[y*8+x/8]&(0x80u>>(x%8));assert(pixel==(i<expected));}
  assert(n30::rf_bars(m,at+n30::SnapshotMaxUs)==0);m.source=n30::Source::LoRa;assert(n30::rf_bars(m,at)==0);
 }
}

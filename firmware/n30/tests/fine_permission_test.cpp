// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "fine_receiver.hpp"
#include "g1_corrected_clock.hpp"
#include <cassert>
#include <cstdio>
using namespace v12;
static bool pair(G1CorrectedClock& clock,uint32_t guard){
 constexpr uint64_t epoch=UINT64_C(1750000000000000000);
 G1Anchor anchor{true,111,55,epoch/1000,1000000,2500000,2,3,0};
 for(unsigned publication=1;publication<=2;++publication){
  const uint64_t now=1000000+uint64_t(publication-1)*62500;
  r2_relay_packet p{};p.origin={0,1,55,2,1000000000,1066000000,0,int64_t(epoch),0,0};
  p.route={0,0,0,0,55,3,publication,9,3,UINT32_MAX};
  if(publication==2){p.count=1;p.correction[0]={1,1,4,UINT64_C(1000000000)*2/125,3};}
  periodic_rx::Stamp s{};s.actual=s.association_observed=true;s.tick20=now*20;s.available20=s.tick20+200;
  s.epoch=1;s.sequence=publication;s.event=uint16_t(publication);s.channel=4;s.map_enclosure_ns=guard;
  const bool accepted=clock.receive(p,s,s.available20,now+10,anchor);
  if(publication==1)assert(!accepted);else return accepted;
 }
 return false;
}
int main(){
 uint32_t widened=0;assert(n30::fine::Receiver::lifetime_guard(1200000,1280000,1000,widened)&&widened==4001025);
 G1CorrectedClock legacy_guard,fine_guard;
 assert(pair(legacy_guard,1000)&&legacy_guard.until());
 assert(!pair(fine_guard,widened)&&!fine_guard.until());
 std::puts("PASS actual G1 corrected source expiry: widened fine-coordinate guard refuses near-expiry pair accepted by obsolete small guard");
}

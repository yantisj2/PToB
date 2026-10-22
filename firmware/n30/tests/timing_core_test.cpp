// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Causal observer equivalence at the production authenticated-pair boundary.
#define PTOB_N30 1
#include "g1_corrected_clock.hpp"
#include <cassert>
#include <bit>
#include <cstdio>
#include <vector>
using namespace v12;
std::vector<int64_t> frozen;unsigned observed=0;
void observer(const n30_probe::Pair& p){
 assert(p.publication<frozen.size());assert(p.predicted_ns==frozen[p.publication]);
 assert(p.local20<=p.available20&&p.created_ns<=p.remote_ns&&p.remote_ns<p.expires_ns);
 assert(p.root==111&&p.boot==55&&p.source==2&&p.train==3&&p.epoch==1);
 assert(p.event==uint16_t(p.publication)&&p.channel==p.publication%37);
 ++observed;
}
uint64_t run(bool diagnostic){
 G1CorrectedClock clock;G1Anchor a{true,111,55,UINT64_C(1750000000000000),1000000,2500000,2,3,40000};
 n30_probe::pair_sink=diagnostic?observer:nullptr;frozen.assign(1,0);uint64_t remote_previous=0;uint64_t hash=0;
 auto fold=[&](uint64_t x){hash=(hash^x)*UINT64_C(1099511628211);};
 for(unsigned i=1;i<=1920;i++){
  const auto now=UINT64_C(1000000)+uint64_t(i)*62500;const double seconds=double(now)/1000000;
  const auto remote=uint64_t(int64_t(now*1000)+int64_t(std::llround(5000*seconds+50*seconds*seconds)));
  r2_relay_packet p{};p.origin={0,1,55,2,(remote/1000000000)*1000000000,(remote/1000000000)*1000000000+1650000000,0,INT64_C(1750000000000000000),0,0};
  p.route={0,0,0,0,55,3,i,9,3,UINT32_MAX};
  if(i>1&&!(i>=700&&i<712)){p.count=1;p.correction[0]={i-1,uint16_t(i-1),uint8_t((i-1)%37),remote_previous*2/125,3};}
  periodic_rx::Stamp c{};c.actual=c.association_observed=true;c.tick20=now*20;c.available20=c.tick20+200;c.epoch=1;c.sequence=i;c.event=uint16_t(i);c.channel=uint8_t(i%37);c.map_enclosure_ns=1000;
  a.at=now;a.until=now+1500000;frozen.push_back(clock.prediction().relative_at(c.tick20));
  clock.receive(p,c,c.available20,now+10,a);remote_previous=remote;
  const auto m=clock.prediction();const auto q=clock.quality(now+10,c.available20);
  for(auto x:{m.tick,uint64_t(m.epoch),uint64_t(m.phase),std::bit_cast<uint64_t>(m.rate),uint64_t(m.utc_offset),uint64_t(clock.accepted()),uint64_t(clock.refused()),clock.causal_squares(),uint64_t(q.samples),uint64_t(q.rms_ns),uint64_t(q.max_ns),clock.until()})fold(x);
 }
 if(diagnostic)assert(observed==clock.accepted()&&observed>1700);
 n30_probe::pair_sink=nullptr;return hash;
}
int main(){const auto off=run(false),on=run(true);assert(off==on);std::printf("PASS1920 original/correction opportunities: observer ON/OFF exact model/quality/permission equivalence, frozen prior predictions, missing corrections; pairs=%u\n",observed);}

// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual clock/capture association with synthetic oscillator drift and channel bias.
#include "g1_corrected_clock.hpp"
#include <cassert>
#include <cstdio>
using namespace v12;
constexpr uint64_t Utc=UINT64_C(1750000000000000000),Start=1000000;
struct Source {
 G1CorrectedClock clock;G1Anchor anchor{true,111,55,Utc/1000,Start,Start+1500000,2,3,40000};
 uint32_t sequence=1;uint64_t previous_remote=0;uint16_t previous_event=0;uint8_t previous_channel=0;
 bool feed(uint64_t now,uint8_t channel,int direction,unsigned acceleration){
  const double seconds=double(now-Start)/1000000;
  const auto offset=int64_t(std::llround(direction*(5000*seconds+.5*acceleration*seconds*seconds)))+channel*3;
  const uint64_t remote=uint64_t(int64_t(now*1000)+offset);
  r2_relay_packet p{};p.origin={0,1,55,2,(remote/1000000000)*1000000000,(remote/1000000000)*1000000000+1650000000,0,int64_t(Utc),0,0};
  p.route={0,0,0,0,55,3,sequence,9,3,UINT32_MAX};
  if(previous_remote){p.count=1;p.correction[0]={sequence-1,previous_event,previous_channel,previous_remote*2/125,3};}
  periodic_rx::Stamp c{};c.actual=c.association_observed=true;c.tick20=now*20;c.available20=c.tick20+200;c.epoch=1;c.sequence=sequence;c.event=uint16_t(sequence);c.channel=channel;c.map_enclosure_ns=1000;
  anchor.at=now;anchor.until=now+1500000;
  const bool accepted=clock.receive(p,c,c.available20,now+10,anchor);
  previous_remote=remote;previous_event=c.event;previous_channel=channel;++sequence;return accepted;
 }
};
int main(){
 // Delayed corrections must train after a newer model publication without
 // being scored against that future model. Every original keeps its own map.
 Source consecutive;
 for(unsigned i=0;i<160;i++)consecutive.feed(Start+uint64_t(i)*62500,0,1,0);
 std::printf("STREAM accepted=%u/159 missing=%u\n",consecutive.clock.accepted(),consecutive.clock.quality_missing());std::fflush(stdout);
 // Nine source-envelope transitions are correctly refused: a follow-up
 // cannot extend the original's signed expiry. All150 eligible pairs survive.
 assert(consecutive.clock.accepted()==150&&consecutive.clock.refused()==9&&consecutive.clock.quality_missing()==2);

 for(const int direction:{-1,1}){
  Source s;uint64_t first_lock=0;uint32_t maximum=0,steady=0;unsigned locked=0;
  for(unsigned i=0;i<1920;i++){
   const auto now=Start+uint64_t(i)*62500;s.feed(now,uint8_t((i*11+i/17)%37),direction,100);
   const auto q=s.clock.quality(now+10);const auto m=s.clock.prediction();
   if(q.locked(m.tick,m)&&!first_lock)first_lock=now-Start;
   if(i>=640){maximum=std::max(maximum,q.max_ns);steady=std::max(steady,q.estimate(m.tick,m));locked+=q.locked(m.tick,m);}
  }
  std::printf("DRIFT direction=%d first_lock_us=%llu steady_causal_max_ns=%u forecast_max_ns=%u locked=%u/1280\n",direction,(unsigned long long)first_lock,maximum,steady,locked);std::fflush(stdout);
  assert(first_lock&&first_lock<35000000&&maximum<1000&&steady<1000&&locked==1280);
  assert(s.clock.accepted()>=1800&&s.clock.quality_missing()<=2);
  assert(s.clock.causal_max_ns()>1000); // Acquisition tails remain counted.

 }
}

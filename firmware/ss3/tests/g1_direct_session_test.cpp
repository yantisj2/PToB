// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "g1_corrected_clock.hpp"
#include "g1_direct_session.hpp"
#include <cassert>
#include <cstdio>
using namespace v12;
constexpr uint64_t Epoch=UINT64_C(1750000000000000000);
struct Fixture {
  G1CorrectedClock clock;G1Anchor anchor{true,111,55,Epoch/1000,1000000,2500000,2,3,40000};
  uint32_t publication=1;uint64_t old_local=0,old_remote=0;uint16_t old_event=0;uint8_t old_channel=0;
  int64_t offset_jitter=0;double physical_rate_ppb=2000;
  int64_t relative_step=0;uint32_t remote_epoch=9;
  bool feed(uint64_t now,uint8_t channel,unsigned offset_ns=0,bool correction=true){
    const uint64_t local=now*20;const uint64_t remote=uint64_t(int64_t(now*1000+uint64_t(std::llround(double(now)*physical_rate_ppb/1000000))+offset_ns)+relative_step);
    r2_relay_packet p{};p.origin={0,1,55,2,(now/1000000)*1000000000,(now/1000000)*1000000000+1650000000,0,int64_t(Epoch)+offset_jitter,0,0};
    p.origin.created_ns=uint64_t(int64_t(p.origin.created_ns)+relative_step);p.origin.expires_ns=uint64_t(int64_t(p.origin.expires_ns)+relative_step);
    p.route={0,0,0,0,55,3,publication,remote_epoch,3,UINT32_MAX};
    if(old_local&&correction){p.count=1;p.correction[0]={publication-1,old_event,old_channel,old_remote*2/125,3};}
    periodic_rx::Stamp c{};c.actual=c.association_observed=true;c.tick20=local;c.available20=local+200;c.epoch=1;c.sequence=publication;
    c.event=uint16_t(publication);c.channel=channel;c.map_enclosure_ns=1000;
    const bool accepted=clock.receive(p,c,c.available20,now+10,anchor);
    old_local=local;old_remote=remote;old_event=c.event;old_channel=channel;++publication;return accepted;
  }
};
int main(){
  // The actual correction matcher/clock maintains replay within one immutable
  // direct-root session. Thousands of physical updates must not extend its
  // authentication deadline, renew source without a pair, or request 30s JOINs.
  for(const uint32_t random:{0u,15000000u,UINT32_MAX}){
    Fixture stream;stream.anchor.utc+=stream.anchor.at;
    G1DirectSession session;const auto start=stream.anchor.at;
    auto legacy=stream.anchor;uint64_t legacy_due=0;
    assert(session.begin(stream.anchor,start,random));
    const auto hard=stream.anchor.context_until;uint64_t first_due=0;
    unsigned maintained=0;
    for(uint64_t now=start;now+20<hard;now+=62500){
      if(!stream.feed(now,uint8_t((now/62500)%37)))continue;
      const auto delivered=now+20,completion=delivered*20;uint64_t utc=0;
      assert(stream.clock.finalize(completion)&&stream.clock.deliver(completion,delivered,utc));
      stream.anchor.until=stream.clock.until();legacy.until=stream.anchor.until;
      if(legacy.refresh(delivered)&&!legacy_due)legacy_due=delivered;
      assert(session.reanchor(stream.anchor,stream.clock.original20(),completion,
        delivered,utc,stream.anchor.until,stream.clock.coarse_enclosure_ns()));++maintained;
      assert(stream.anchor.context_until==hard&&stream.anchor.drift_ppm==2000);
      if(session.due(stream.anchor,delivered)&&!first_due)first_due=delivered;
      const auto saved=stream.anchor;
      // Reusing the same completed tuple or merely polling cannot reanchor.
      assert(!session.reanchor(stream.anchor,stream.clock.original20(),completion,
        delivered+1,utc,stream.anchor.until,1000));
      assert(stream.anchor.at==saved.at&&stream.anchor.until==saved.until);
    }
    assert(maintained>2500&&first_due>=start+225000000&&first_due<=start+240125000);
    assert(legacy_due&&legacy_due<=start+50000000); // Old cadence control.
    assert(!stream.anchor.valid(hard)&&session.due(stream.anchor,hard));
    const auto last=stream.anchor;
    assert(!session.reanchor(stream.anchor,stream.clock.original20()+1,hard*20,
      hard,last.utc+hard-last.at,last.until,1000));
    assert(stream.anchor.at==last.at&&stream.anchor.context_until==hard);
    for(unsigned field=0;field<4;++field){auto foreign=last;
      if(field==0)++foreign.root;if(field==1)++foreign.boot;
      if(field==2)++foreign.source_epoch;if(field==3)++foreign.publication;
      assert(!session.matches(foreign));
    }
    // Source loss is immediate even with a live authentication session.
    auto lost=last;lost.until=lost.at;
    assert(!lost.live(lost.at)&&session.due(lost,lost.at+1500000));
  }
  // Rejected observations cannot mutate a live replay anchor or its session.
  G1Anchor a{true,11,22,Epoch/1000,1000000,2500000,3,4,40000};
  G1DirectSession session;assert(session.begin(a,a.at,0));
  for(unsigned fault=0;fault<8;++fault){
    auto copy=a;uint64_t original=20000000,available=20000200,at=1000010;
    uint64_t utc=a.utc+10,permission=a.until;uint32_t enclosure=1000;
    if(fault==0)original=0;if(fault==1)available=original-1;
    if(fault==2)available=original+5000001;if(fault==3)permission=at;
    if(fault==4)++permission;if(fault==5)utc+=300000;
    if(fault==6)enclosure=UINT32_MAX;if(fault==7)++copy.boot;
    const auto saved=copy;
    assert(!session.reanchor(copy,original,available,at,utc,permission,enclosure));
    assert(copy.at==saved.at&&copy.utc==saved.utc&&copy.until==saved.until&&copy.context_until==saved.context_until);
  }
  auto overflow=a;overflow.at=UINT64_MAX-100;overflow.until=UINT64_MAX;
  assert(!session.begin(overflow,overflow.at,0));
  puts("PASS direct-root session: actual matched maps, 4min renewal, immutable 6min expiry, legacy cadence and refusal controls");
}

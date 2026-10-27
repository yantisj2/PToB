// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "g1_corrected_clock.hpp"
#include <cassert>
#include <cstdio>
using namespace v12;
constexpr int64_t UTC=INT64_C(1750000000000000000);
struct Fixture {
  G1CorrectedClock clock;
  G1Anchor anchor{true,222,66,0,0,0,3,4,1000};
  G1RetainedEstimate held;
  uint32_t publication=1;uint64_t old_remote=0;uint16_t old_event=0;uint8_t old_channel=0;
  int64_t offset=UTC-INT64_C(9000000000),phase=500;double rate=5000;
  Fixture(){
    const G1ClockPrediction old{200000000,INT64_C(10000050000),0,rate,UTC};
    assert(held.retain(old,111,55,2,old.tick,10000000,true));
  }
  bool feed(uint64_t us,bool correction=true,bool actual=true){
    const auto tick=us*20;
    const auto absolute=held.clock.at(tick)+phase;
    const auto remote=uint64_t(absolute-offset);
    r2_relay_packet p{};
    const auto created=(remote/1000000000)*1000000000;
    p.origin={0,1,66,3,created,created+1650000000,0,offset,0,0};
    p.route={0,0,0,0,66,4,publication,10,4,UINT32_MAX};
    if(old_remote&&correction){p.count=1;p.correction[0]={publication-1,old_event,old_channel,old_remote*2/125,4};}
    periodic_rx::Stamp s{};s.actual=s.association_observed=actual;s.tick20=tick;s.available20=tick+200;
    s.epoch=5;s.sequence=publication;s.event=uint16_t(publication);s.channel=uint8_t(publication%4);s.map_enclosure_ns=1000;
    anchor.at=us;anchor.until=us+1500000;anchor.utc=uint64_t(absolute)/1000;
    const auto accepted=clock.receive(p,s,s.available20,us+10,anchor,nullptr,&held);
    old_remote=remote;old_event=s.event;old_channel=s.channel;++publication;
    return accepted;
  }
};
int main(){
  for(const int direction:{-1,1}){
    Fixture f;f.phase=direction*500;
    assert(!f.feed(13000000));
    uint64_t utc=0;
    assert(!f.clock.prediction().tick&&!f.clock.deliver(260000200,13000010,utc));
    assert(!f.clock.covers(13000010,13000010)); // JOIN/original alone never installs the carry.
    assert(f.feed(13062500));
    auto model=f.clock.prediction();
    assert(model.tick&&model.utc_offset==f.offset&&model.rate==5000);
    assert(model.at(model.tick)==f.held.clock.at(model.tick)); // No clock step, even across a9s remote-counter rebase.
    assert(std::abs(model.phase)<=600&&model.phase_slew_ns_s()==1000);
    assert(!f.clock.frequency_trained()&&f.clock.fit_points()==1&&f.clock.quality(13062510,model.tick).samples==1);
    assert(!f.clock.quality(13062510,model.tick).locked(model.tick,model));
    assert(f.clock.covers(13062510,13062510)); // Only the new matched pair owns this permission.
    const auto end=model.tick+1250000;
    assert(f.clock.finalize(end)&&f.clock.prediction().at(end)==model.at(end));
    for(unsigned i=2;i<160;i++)f.feed(13000000+i*62500);
    assert(f.clock.frequency_trained()&&std::abs(f.clock.prediction().rate-5000)<10);
  }
  Fixture expired;assert(expired.held.handoff_ready(800000000));assert(!expired.held.handoff_ready(800000001));
  assert(!expired.feed(41000000));assert(expired.feed(41062500));
  assert(expired.clock.prediction().rate==0); // Original30s boundary is never slid by polling/admission.
  Fixture untrained;untrained.held.trained=false;assert(!untrained.feed(13000000));assert(untrained.feed(13062500));assert(untrained.clock.prediction().rate==0);
  Fixture backwards;assert(!backwards.held.handoff_ready(backwards.held.clock.tick-1));
  Fixture invalid;assert(!invalid.feed(13000000,true,false));assert(!invalid.feed(13062500,false));assert(!invalid.clock.prediction().tick);
  Fixture step;step.phase=INT64_C(2000000000);assert(!step.feed(13000000));assert(!step.feed(13062500));
  assert(step.clock.discontinuities()==1&&!step.clock.prediction().tick&&!step.clock.covers(13062510,13062510));
  std::puts("PASS N30 signed fine-phase handoff, learned rate, UTC counter rebase, new physical permission/quality, expiry, invalid capture and gross-step refusal");
}

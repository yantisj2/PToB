// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#define V12_RC7_CLOCK_TEST_INSPECT 1
#include "g1_corrected_clock.hpp"
#include "rc7_tx_reference.hpp"
#include "g1_retained_estimate.hpp"
#include <cassert>
#include <cstdio>
namespace v12 {struct Rc7PermissionInspect {
 static G1ClockPrediction raw(const G1CorrectedClock& c){return c.clock_;}
};}
using namespace v12;
int main(){
  G1CorrectedClock c;G1Anchor a{true,111,55,1700000000000000,1000000,2500000,2,3,40000};
  assert(!c.prediction().receiver_phase_ns);
  uint64_t old_remote=0;uint16_t old_event=0;uint8_t old_channel=0;
  for(unsigned i=0;i<640;i++){
    const auto us=UINT64_C(1000000)+i*62500;const uint64_t remote=us*1000-7671;
    a.at=us;a.until=us+1500000;
    r2_relay_packet p{};p.origin={0,1,55,2,(us/1000000)*1000000000,(us/1000000)*1000000000+1650000000,0,INT64_C(1700000000000000000),0,0};
    p.route={0,0,0,0,55,3,i+1,9,3,UINT32_MAX};
    if(i){p.count=1;p.correction[0]={i,old_event,old_channel,old_remote*2/125,3};}
    periodic_rx::Stamp s{};s.actual=s.association_observed=true;s.tick20=us*20;s.available20=s.tick20+200;
    s.epoch=1;s.sequence=i+1;s.event=uint16_t(i+1);s.channel=uint8_t(i%37);s.map_enclosure_ns=1000;
    const bool mapped=c.receive(p,s,s.available20,us+10,a);
    old_remote=remote;old_event=s.event;old_channel=s.channel;
    if(!mapped)continue;
    const auto raw=Rc7PermissionInspect::raw(c);const auto delivered=c.prediction();
    assert(raw.receiver_phase_ns==0&&delivered.receiver_phase_ns==7671);
    assert(delivered.relative_at(s.available20)==raw.relative_at(s.available20)+7671);
    assert(delivered.at(s.available20)==raw.at(s.available20)+7671);
    assert(W12PhaseOffsets::delivered(delivered).relative_at(s.available20)==delivered.relative_at(s.available20));
    const auto until=c.until();const auto accepted=c.accepted(),fit=c.fit_points();
    uint64_t utc=0;assert(c.deliver(s.available20,us+10,utc)&&utc==uint64_t(delivered.at(s.available20))/1000);
    assert(c.finalize(s.available20+100));
    assert(Rc7PermissionInspect::raw(c).receiver_phase_ns==0&&c.prediction().receiver_phase_ns==7671);
    assert(c.until()==until&&c.accepted()==accepted&&c.fit_points()==fit);
    const auto m=c.prediction();Rc7TxReference::Projection tx;
    assert(Rc7TxReference::project(m,m.tick,m.tick+800,1,false,0,tx));
    assert(tx.emitted_ns==m.relative_at(m.tick+800)+3030&&tx.residual_ns==3030);
    assert(Rc7TxReference::project(m,m.tick,m.tick+800,1,true,47000,tx));
    assert(tx.total_ns==47000); // Existing explicit total supersedes default relay residual.
  }
  std::fprintf(stderr,"accepted=%u trained=%u pairs=%u reason=%u\n",c.accepted(),unsigned(c.frequency_trained()),c.rate_pairs(),unsigned(c.fit_reason()));
  // Legacy matching refuses originals older than the just-finalized map;
  // dense fine matching retains its frozen prediction. Do not require identical
  // acceptance counts from these distinct backends.
  assert(c.accepted()>200&&c.frequency_trained());
  c.withdraw();assert(!c.prediction().tick&&!c.prediction().receiver_phase_ns);
  G1ClockPrediction top{20,INT64_MAX-100};top.receiver_phase_ns=7671;
  assert(!top.relative_at(20));
  G1ClockPrediction low{20,100};low.receiver_phase_ns=-100;assert(!low.relative_at(20));
  std::puts("PASS W12 receive phase once on delivery/relay, raw fit/finalize unchanged, provisional total override, missing/withdrawn/overflow refusals");
}

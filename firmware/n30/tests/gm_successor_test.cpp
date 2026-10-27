// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "g1_gm_successor.hpp"
#include <cassert>
#include <cstdio>
constexpr uint64_t Now=100000000,Utc=1700000000000000;
v12::G1Anchor anchor(){return {true,501,601,Utc+Now,Now,Now+1500000,2,3,1000};}
gm7_packet announce(){
  gm7_packet p{};p.group=17;p.key_epoch=9;p.kind=GM7_ANNOUNCE;p.flags=GM7_ELIGIBLE;
  auto& c=p.body.control;c.sender=c.active=501;c.boot=c.active_boot=601;c.sequence=1;
  c.sent_utc_us=Utc+Now;c.sent_mono_us=5000000;c.max_age_us=2750000;c.source_valid_us=1500000;
  c.source_gen=4;c.capture_gen=5;c.train_gen=3;c.timing_epoch=2;c.birth_us=Utc;c.founder=501;c.term=2;
  c.hot=777;c.hot_boot=877;return p;
}
gm7_packet withdraw(){auto p=announce();p.kind=GM7_WITHDRAW;p.flags=0;auto& c=p.body.control;
  c.sequence=2;c.source_valid_us=0;c.max_age_us=1000000;c.sent_utc_us=0;return p;}
int main(){
  const auto a=anchor();const auto ann=announce();const auto negative=withdraw();
  assert(!gm7_validate(&ann)&&!gm7_validate(&negative));
  v12::GmSuccessor s;assert(!s.receive(negative,a,Now,Now));
  assert(s.receive(ann,a,Now,Now));assert(!s.receive(ann,a,Now,Now));
  auto copy=s;assert(copy.lost(a,Now)==777&&copy.lost(a,Now)==0);
  for(unsigned field=0;field<8;++field){auto p=negative;auto& c=p.body.control;
    switch(field){case 0:++c.boot;++c.active_boot;break;case 1:++c.term;break;case 2:++c.birth_us;break;
      case 3:++c.founder;break;case 4:++c.train_gen;break;case 5:++c.hot_boot;break;case 6:--c.sequence;break;case 7:--c.sent_mono_us;break;}
    assert(!s.receive(p,a,Now,Now));
  }
  assert(s.receive(negative,a,Now,Now));assert(s.due(Now));
  auto replay=ann;replay.body.control.sequence=100;assert(!s.receive(replay,a,Now,Now));
  assert(s.blocks(501,601,3)&&!s.blocks(501,602,3));
  assert(s.lost(a,Now)==777);assert(!s.expected(777,878,Now)&&s.expected(777,877,Now));
  s.admitted(a);assert(s.blocks(501,601,3));auto next=a;next.publication=4;s.admitted(next);assert(!s.blocks(501,601,3));
  // Missing nominee in WITHDRAW keeps the last designation; it cannot invent one.
  s.clear();assert(s.receive(ann,a,Now,Now));auto absent=negative;absent.body.control.hot=absent.body.control.hot_boot=0;
  assert(s.receive(absent,a,Now,Now)&&s.lost(a,Now)==777);
  s.clear();assert(s.receive(ann,a,Now,Now));auto removed=ann;removed.body.control.sequence=2;removed.body.control.hot=removed.body.control.hot_boot=0;
  assert(s.receive(removed,a,Now,Now)&&s.lost(a,Now)==0);
  s.clear();auto stale=ann;stale.body.control.sent_utc_us-=1500000;assert(!s.receive(stale,a,Now,Now));
  auto future=ann;future.body.control.sent_utc_us+=1101;assert(!s.receive(future,a,Now,Now));
  assert(!s.receive(ann,a,Now,Now+250000));
  // A UTC-less negative uses the accepted paired monotonic sample, never receipt TTL.
  assert(s.receive(ann,a,Now,Now));assert(!s.receive(negative,a,Now+1000000,Now+1000000));
  auto moved=negative;moved.body.control.sent_mono_us+=1000000;assert(s.receive(moved,a,Now+1000000,Now+1000000));
  // The fixed YIELD uses the conservative latest boundary. Copies cannot slide it.
  s.clear();auto yield=ann;yield.flags|=GM7_YIELD;yield.body.control.yield_utc_us=yield.body.control.sent_utc_us+750000;
  assert(s.receive(yield,a,Now,Now));assert(!s.receive(yield,a,Now+250000,Now+250000));
  assert(!s.due(Now+751099)&&s.due(Now+751100));
  auto later=ann;later.body.control.sequence=2;later.body.control.sent_utc_us+=100000;later.body.control.sent_mono_us+=100000;
  assert(s.receive(later,a,Now+100000,Now+100000));assert(s.due(Now+751100));
  assert(s.lost(a,Now+751100)==777&&s.blocks(501,601,3));
  s.clear();assert(s.receive(ann,a,Now,Now));copy=s;
  assert(copy.lost(a,Now+5748899)==777);copy=s;assert(copy.lost(a,Now+5748900)==0);
  // A next-term ANN must come from the exact named successor incarnation.
  auto promoted=ann;auto& c=promoted.body.control;c.sender=c.active=777;c.boot=c.active_boot=877;
  c.hot=c.hot_boot=0;c.term=3;c.train_gen=9;
  assert(s.receive(promoted,a,Now,Now)&&s.due(Now));
  s.clear();assert(s.receive(ann,a,Now,Now));++c.boot;++c.active_boot;assert(!s.receive(promoted,a,Now,Now));
  v12::GmControlScan scan;
  assert(!scan.poll(Now,true,1,0,2500000));assert(!scan.allows(Now+31999));assert(scan.allows(Now+32000));
  assert(scan.poll(Now+32000,true,1,32000,2532000));assert(!scan.allows(Now+50000));
  assert(!scan.poll(Now+250000,false,2,0,2750000)&&!scan.allows(Now+282000));
  assert(!scan.poll(Now+250000,true,2,25000,2750000));
  std::puts("PASS GM7 successor scope, original freshness, replay, yield, withdrawal, bounded hint and scan guards");
}

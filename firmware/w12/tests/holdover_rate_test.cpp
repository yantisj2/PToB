// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "w12_holdover_rate.hpp"
#include <cassert>
#include <cstdio>
using namespace v12;
static W12HoldoverRate history;
static void feed(double rate=100, double acceleration=0){
  history.clear();
  for(unsigned i=1;i<=160;++i){
    const double t=i*.25;const auto local=uint64_t(i)*5000000;
    const unsigned ch=(i*13)%37;
    assert(history.observe(local,uint64_t(1000000000000LL+int64_t(local)*50+std::llround(t*rate+t*t*acceleration+ch*30)),local+1000,ch,1));
  }
  history.refresh(800001000);
}
int main(){
  feed();assert(history.points()==129);
  assert(history.fit16().valid&&history.fit32().valid);
  assert(std::abs(history.fit32().rate-100)<.001);
  const auto choice=history.choose(800002000,108,20);
  assert(choice.selected&&choice.delta==-5&&choice.rate==103);
  assert(!history.choose(900002000,108,20).selected); // Stale history.
  assert(!history.choose(800002000,108,1).selected); // No guard improvement.
  assert(!history.choose(800002000,130,20).selected); // Reject a large disagreement.
  feed(100,2);assert(!history.choose(800002000,200,20).selected); // Changing rate.
  history.clear();
  for(unsigned i=1;i<=128;++i){const uint64_t tick=uint64_t(i)*5000000;
    history.observe(tick,1000000000000+tick*50,tick+100000000,0,2);}
  history.refresh(640000000);
  assert(history.fit32().span_ms<28000); // Unavailable future pairs cannot help.
  assert(!history.choose(640000000,0,100).selected);
  history.observe(650000000,1032500000000,650001000,0,3);
  assert(history.points()==1&&!history.choose(650001000,0,100).selected);
  assert(!history.observe(650000000,1032500000000,650001000,0,3));
  assert(!history.observe(660000000,1033000000000,659999999,0,3));
  G1ClockPrediction c;c.tick=40000000;c.epoch=1000000000000;c.rate=108;c.phase=-301;
  c.receiver_phase_ns=7671;c.utc_offset=1700000000000000000;
  const uint64_t at=44000000;const auto before=c.at(at),remaining=c.phase_remaining(at);
  assert(W12HoldoverRate::reanchor(c,at,103));
  assert(c.at(at)==before&&c.phase==remaining&&c.rate==103&&c.receiver_phase_ns==7671);
  assert(!W12HoldoverRate::reanchor(c,at-1,103));
  std::puts("PASS causal channel HOLD rate, fallback, disagreement, cap and continuous calibrated entry");
}

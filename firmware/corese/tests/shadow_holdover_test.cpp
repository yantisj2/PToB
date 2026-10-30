// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "shadow_holdover.hpp"
#include <cassert>
#include <cstdio>
using namespace corese;
int main(){
 constexpr uint64_t Hz=20000000,start=10*Hz;const int64_t epoch=1000000000000ll;
 v12::G1ClockPrediction clock;clock.tick=start;clock.epoch=epoch;clock.rate=3;
 static ShadowHoldover servo;
 servo.pair(start-1,epoch-50,start,0,1);servo.tick(clock,start,true,20);
 for(unsigned i=1;i<=128;++i){const auto t=start+i*Hz/16;const auto predicted=clock.relative_at(t);
  servo.tick(clock,t,false,20); // Later production-quality loss cannot censor this trial.
  servo.pair(t,uint64_t(predicted+i),t+100,0,1);}
 auto v=servo.view(start+8*Hz);assert(v.holding&&!v.aborted&&v.samples==128&&v.p95_ns==122&&v.p99_ns==127&&v.max_ns==128);
 // Later witnesses cannot correct the frozen candidate. All tails survive.
 const auto t=start+9*Hz;servo.pair(t,uint64_t(clock.relative_at(t)+9000),t,0,1);
 v=servo.view(t);assert(v.max_ns==9000&&v.run_max_ns==9000&&v.gaps==1);
 assert(servo.view(t+5*Hz+1).max_ns==UINT64_MAX);
 servo.pair(t+Hz,uint64_t(clock.relative_at(t+Hz)),t+Hz,0,2);
 v=servo.view(t+Hz);assert(v.samples==0&&!v.ready&&v.p99_ns==UINT64_MAX);
 // Empirical duration needs >=100 fully witnessed trials, never one good run.
 servo.clear();clock.rate=0;
 for(unsigned i=0;i<6400;++i){const auto at=start+i*Hz/16;
  servo.pair(at,uint64_t(clock.relative_at(at)),at,0,3);servo.tick(clock,at,true,20);}
 v=servo.view(start+6399*Hz/16);assert(v.ready&&v.duration_ms>=2000&&v.trials>=100);
 const auto previous=servo.view(start+6399*Hz/16);servo.tick(clock,start+6400*Hz/16,false,20);
 v=servo.view(start+6400*Hz/16);assert(v.holding&&v.started==previous.started&&v.aborted==previous.aborted);
 servo.clear();assert(!servo.view(start+6400*Hz/16).holding);
 std::puts("PASS CoreSE frozen candidate, P95/P99/MAX, retained tails/gaps, expiry, scope isolation and empirical duration population");
}

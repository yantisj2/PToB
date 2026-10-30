// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "../main/w12_holdover_horizon.hpp"
#include <cassert>
#include <cstdio>
#include <string>
using namespace v12;
static constexpr uint64_t Hz=W12HoldoverHorizon::Hz;
static W12HoldoverHorizon h;
static W12HoldoverHorizon::End saved;
static FILE* replay=nullptr;static uint64_t sequence=0;
static void began(const W12HoldoverHorizon::Start& s){if(replay)std::fprintf(replay,"W12_HORIZON_START boot=1 trial=%u start20=%llu probe_ms=%u epoch_ns=%lld phase_ns=%d rate=%.17g temperature=0 production=0\n",unsigned(s.id),(unsigned long long)s.at20,unsigned(s.probe_ms),(long long)s.epoch,int(s.phase),s.rate);}

static void ended(const W12HoldoverHorizon::End& e){saved=e;if(replay)std::fprintf(replay,"W12_HORIZON_END boot=1 trial=%u start20=%llu probe_ms=%u last20=%llu outcomes=%llu peak_ns=%u gaps=%u temperature=0 production=0\n",unsigned(e.id),(unsigned long long)e.start20,unsigned(e.probe_ms),(unsigned long long)e.last20,(unsigned long long)e.outcomes,unsigned(e.peak_ns),unsigned(e.gaps));}
static G1ClockPrediction clock_at(uint64_t at,double rate=0){return {at,1000000000000LL+int64_t(at)*50,0,rate,0};}
static void simulate(unsigned seconds,double rate=0){
  for(uint64_t at=Hz;at<=(seconds+1)*Hz;at+=Hz/8){
    if(replay)std::fprintf(replay,"W12_HOLDOVER_PAIR boot=1 seq=%llu local20=%llu reference_ns=%llu available20=%llu channel=0 generation=1\n",(unsigned long long)++sequence,(unsigned long long)at,(unsigned long long)(1000000000000ULL+at*50),(unsigned long long)(at+1000));
    h.observe(at,1000000000000ULL+at*50,at+1000);h.tick(at+1000);h.start(clock_at(at+1000,rate),at+1000);
    if(replay&&at%Hz==0){const auto v=h.view(at);std::fprintf(replay,"W12_HORIZON_STATUS at_us=%llu duration_ms=%u probe_ms=%u ready=%u lower_bound=%u n=%u bad=%u unknown=%u policy=SAMPLED_PREFIX_Q99_1US_V1 temperature=0 production=0\n",(unsigned long long)(at/20),unsigned(v.duration_ms),unsigned(v.probe_ms),unsigned(v.ready),unsigned(v.lower_bound),unsigned(v.trials),unsigned(v.bad),unsigned(v.unknown));}
  }
}
int main(int argc,char** argv){
  assert(argc==2);
  h.sinks(began,ended);assert(!h.view().ready);
  assert(h.start(clock_at(Hz),Hz));
  for(unsigned i=1;i<=96;++i){const auto at=Hz+i*Hz/8;
    const auto reference=1000000000000ULL+at*50+(i==4?1000:0);
    h.observe(at,reference,at+1000);
  }
  assert(saved.peak_ns==1000&&((saved.outcomes>>2)&3)==W12HoldoverHorizon::Fail);
  assert((saved.outcomes&3)==W12HoldoverHorizon::Pass);
  assert(((saved.outcomes>>(2*18))&3)==W12HoldoverHorizon::Fail); // Never forget an early crossing.
  h.clear();simulate(150,500);auto v=h.view();
  assert(v.ready&&v.duration_ms==1750&&!v.lower_bound&&v.trials>=100);
  assert(v.probe_ms==12000); // A failed horizon cannot advance the probe.
  assert(!h.view(v.last20+6*Hz).ready);
  h.clear();assert(!h.view().ready);replay=std::fopen((std::string(argv[1])+"/horizon-synthetic.log").c_str(),"w");assert(replay);
  simulate(330);v=h.view();
  assert(v.ready&&v.duration_ms>=24000&&v.lower_bound&&v.probe_ms>=48000);
  h.cancel();std::fclose(replay);replay=nullptr;
  h.clear();assert(h.start(clock_at(Hz),Hz));
  h.observe(Hz+Hz/2,1000000000000ULL+(Hz+Hz/2)*50,Hz+Hz/2+1000);
  h.tick(15*Hz);
  assert(saved.gaps==1&&((saved.outcomes&3)==W12HoldoverHorizon::Pass)); // At250ms overshoot: conservative witness.
  assert(((saved.outcomes>>(2*18))&3)==W12HoldoverHorizon::Unknown);
  auto thermal=clock_at(20*Hz);thermal.thermal.beta=1;
  assert(!h.start(thermal,20*Hz)); // Temperature cannot contaminate the duration population.
  std::puts("PASS empirical hold seconds, first-crossing memory, sparse evidence, censoring, adaptive cap, reset and temperature exclusion");
}

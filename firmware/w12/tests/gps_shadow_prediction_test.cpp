// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ptob_gps_shadow_clock.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>

template<uint32_t Hz> void oscillator(int sign){
 ptob_gps::Affine<Hz> clock;
 unsigned measured=0;int64_t maximum=0;uint64_t last=0;
 for(unsigned n=1;n<=160;++n){
  // Independent analytic oscillator: +/-3ppm rate and +/-4ppb/s drift.
  const uint64_t tick=UINT64_C(4000000000)+uint64_t(n)*Hz+
   int64_t(std::llround(sign*(double(Hz)*3e-6*n+double(Hz)*2e-9*n*n)));
  const uint64_t edge20=uint64_t(n)*20000000;
  int64_t predicted=0;
  if(n>32){
   assert(clock.model.predict(tick,edge20,predicted));
   const auto error=std::llabs(predicted-int64_t(n)*1000000000);
   maximum=std::max(maximum,int64_t(error));++measured;
#ifdef PTOB_W12_FINE_CLOCK
   assert(error<100); // All held-out points, no de-biasing or tail exclusion.
#endif
   assert(!clock.model.predict(tick,clock.model.available,predicted));
   assert(!clock.model.predict(clock.model.last_tick+uint64_t(Hz)*3+1,edge20,predicted));
  }
  clock.observe(tick,n,edge20+100);clock.release(edge20+200);
  assert(tick>last);last=tick;
 }
 assert(measured==128&&clock.samples==32&&clock.model.valid);
#ifdef PTOB_W12_FINE_CLOCK
 assert(clock.model.curve_q16!=0);
 assert(std::abs(clock.rate_ppb()-sign*3640)<10);
#else
 assert(clock.model.curve_q16==0&&maximum>200);
#endif
 const auto generation=clock.model.generation;
 assert(!clock.observe(last+Hz,163,UINT64_C(163)*20000000+100));
 assert(clock.samples==1&&!clock.model.valid&&clock.model.generation==generation);
 std::printf("PASS %uHz sign%d causal analytic drift heldout=%u max_ns=%lld\n",Hz,sign,measured,(long long)maximum);
}
int main(){
 for(const int sign:{-1,1}){oscillator<80000000>(sign);oscillator<20000000>(sign);}
#ifdef PTOB_W12_FINE_CLOCK
 // Implausible curvature cannot silently publish an extrapolating clock.
 ptob_gps::Affine<80000000> bad;
 for(unsigned n=1;n<=32;++n)bad.observe(UINT64_C(1000000000)+uint64_t(n)*80000000+uint64_t(n)*n*8,n,uint64_t(n)*20000000+100);
 assert(!bad.model.valid&&bad.samples==0);
 ptob_gps::Shadow shadow;
 for(unsigned n=1;n<=40;++n){
  const uint64_t tick=uint64_t(n)*20000000;
  const auto score=shadow.edge(uint32_t(uint64_t(n)*80000000),n,tick,438,tick+100,true);
  shadow.release(tick+200);
  if(n==32)assert(!std::strcmp(score.raw_predictor,"LINEAR_WARMUP"));
  if(n==33)assert(!std::strcmp(score.raw_predictor,"QUADRATIC32")&&score.raw_valid&&score.mapped_valid);
 }
#endif
}

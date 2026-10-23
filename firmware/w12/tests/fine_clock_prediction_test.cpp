// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "g1_clock_prediction.hpp"
#include <cassert>
#include <initializer_list>
int main(){
 for(const int sign:{-1,1})for(const int ns:{0,999,1000,1001,1999,2000,2001,50000}){
  v12::G1ClockPrediction m{20000000,INT64_C(40000000000),sign*ns,0,0};
#ifdef PTOB_W12_FINE_CLOCK
  const bool fine=ns<2000;
#else
  const bool fine=ns<=1000;
#endif
  const double speed=fine?1000:std::min(50000.0,4.0*ns);
  assert(m.phase_slew_ns_s()==speed);
  assert(m.relative_at(m.tick)==m.epoch); // No step at either mode boundary.
  int64_t previous=m.epoch;
  for(unsigned i=1;i<=4000;i++){
   const uint64_t now=m.tick+uint64_t(i)*20000;
   const double seconds=double(i)/1000;
   const auto expected=m.epoch+int64_t(i)*1000000+
    int64_t(std::llround(sign*std::min(double(ns),seconds*speed)));
   assert(m.relative_at(now)==expected&&expected>previous);
   assert(std::abs(m.phase_remaining(now))<=ns);previous=expected;
  }
  assert(m.phase_remaining(m.tick+80000000)==0);
 }
}

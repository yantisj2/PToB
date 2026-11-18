// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Synthetic rate transition based on the observed counter-rate excursion.
// This is a controller regression, not hardware precision evidence.
#include "dual_clock.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace watch::dual;
constexpr uint64_t Epoch=UINT64_C(1790000000000000000);
static double truth_rate(double t){return t<=60?100:t<120?100-(t-60)*700/60:-600;}
static double truth_adjust(double t){
 if(t<=60)return 100*t;
 if(t<120){const auto d=t-60;return 6000+100*d-350*d*d/60;}
 return -9000-600*(t-120);
}
static Observation sample(uint64_t us,bool coarse){
 const double t=double(us)/1000000;Observation o;
 o.tick=us*20;o.id=1+us/(coarse?30000000:62500);o.scope=coarse?2:1;o.domain=1;
 o.utc_ns=Epoch+us*1000+int64_t(std::llround(truth_adjust(t)))+(coarse?int64_t(o.id%3)*750-750:0);
 o.until=coarse?((us/30000000)*30000000+120000000)*20:o.tick+10000000;
 o.rate_ppb=truth_rate(t);o.enclosure_ns=coarse?10000:50;o.eligible=o.calibrated=true;return o;
}
static double run(uint64_t step){
 Clock c(41,42,true);uint64_t previous=0;double squares=0,peak=0;unsigned n=0;
 for(uint64_t us=1000000;us<=360000000;us+=step){
  const bool ble=us<60000000||us>=300000000,lora=us<240000000;
  const auto v=c.update(us*20,42,ble?sample(us,false):Observation{},lora?sample(us,true):Observation{});
  assert(v.valid&&v.instance==41&&v.domain==42&&v.utc_ns>previous);previous=v.utc_ns;
  const auto truth=Epoch+us*1000+int64_t(std::llround(truth_adjust(double(us)/1000000)));
  const auto error=int64_t(v.utc_ns)-int64_t(truth);
  if(us>=60000000&&us<180000000){squares+=double(error)*error;peak=std::max(peak,std::abs(double(error)));++n;assert(v.mode==Mode::LoRa&&!v.fine);}
  if(us>=240000000&&us<300000000){assert(v.mode==Mode::Hold&&!v.authoritative);assert(std::abs(error)<5000);}
  if(us>=300250000)assert(v.mode==Mode::Ble&&std::abs(error)<1000);
 }
 const double rms=std::sqrt(squares/n);
 std::printf("synthetic controller period_us=%llu coarse_RMS_ns=%.3f peak_ns=%.3f\n",(unsigned long long)step,rms,peak);
 assert(rms<10000&&peak<20000);return rms;
}
int main(){
 const auto a=run(50000),b=run(10000);assert(std::abs(a-b)<200);
 std::puts("PASS drift tracking, bounded source return, monotonic single clock, poll-rate invariance");
}

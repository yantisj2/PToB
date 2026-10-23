// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "relative_rate.hpp"
#include <cassert>
#include <iostream>
using n30::RelativeRateEstimator;using n30::RelativeRatePoint;
static N30RelativeRateView series(RelativeRateEstimator& e,int drift,int64_t intercept=0,uint64_t sec_origin=100){
 N30RelativeRateView v;
 for(uint64_t i=0;i<=256;i++){
  const auto tick=(sec_origin+i/16)*20000000+(i%16)*1250000;
  const auto remote=UINT64_C(500000000000)+i*62500000;
  const auto error=intercept+int64_t(i)*drift/16;
  v=e.observe({1,2,tick,tick+100,remote,int64_t(remote)+error,3,true},1000000+i*62500);
  if(i<128)assert(!v.valid);
 }
 return v;
}
int main(){
 RelativeRateEstimator e;auto v=series(e,120,900);assert(v.valid&&v.ppb==120&&v.samples>=240);
 e.reset();v=series(e,-80,6000);assert(v.valid&&v.ppb==-80);
 e.reset();v=series(e,0,-5400);assert(v.valid&&v.ppb==0); // A constant phase/trim-looking number is not frequency.
 e.reset();v=series(e,7,0,UINT64_C(1000000000));assert(v.valid&&v.ppb==7); // Centered coordinates at long uptime.
 auto p=RelativeRatePoint{1,2,100*20000000ULL,100*20000000ULL+100,500000000000,500000000000,3,true};
 v=e.observe(p,17062500);assert(!v.valid); // Counter reversal/reset.
 p.local20=116*20000000ULL+1250000;p.available20=p.local20+100;
 e.reset();series(e,10);p.root=4;v=e.observe(p,17062500);assert(!v.valid&&v.root==4);
 e.reset();series(e,10);p.root=1;p.boot=9;v=e.observe(p,17062500);assert(!v.valid&&v.boot==9);
 e.reset();series(e,10);p.boot=2;p.source_epoch=4;v=e.observe(p,17062500);assert(!v.valid&&v.source_epoch==4);
 e.reset();series(e,10);p.source_epoch=3;p.causal=false;v=e.observe(p,17062500);assert(!v.valid&&v.samples==0);
 e.reset();series(e,10);p.causal=true;p.local20=120*20000000ULL;p.available20=p.local20+100;
 v=e.observe(p,18000000);assert(!v.valid&&v.samples==1); // Original500ms gap cannot bridge stale history.
 std::cout<<"PASS causal residual-trend sign/baseline/constant phase/long uptime/scope/gap/reset\n";
}

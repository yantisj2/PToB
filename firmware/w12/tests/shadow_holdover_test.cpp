// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "w12_shadow_holdover.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using namespace v12;
using Shadow=W12ShadowHoldover;
constexpr auto Hz=Shadow::Hz;
std::vector<Shadow::Sample> rows;
void save(const Shadow::Sample& s){rows.push_back(s);}
G1ClockPrediction model(uint64_t at,double rate=123){return {at-1000,INT64_C(1000000000000)+int64_t(at-1000)*50,100,rate,0};}
void trial(Shadow& shadow,uint64_t start,int64_t error,double temperature=25){
  shadow.temperature(start,temperature);const auto active=model(start);
  shadow.observe(start-Hz/16,uint64_t(active.relative_at(start-Hz/16)),start,active,true);
  assert(shadow.view().holding);
  // Deliberately poison live feedback during the holdoff. It is a witness only.
  for(unsigned i=1;i<4;++i){auto changed=model(start+Hz*i/4,49000);
    shadow.observe(start+Hz*i/4,1,start+Hz*i/4+100,changed,true);}
  shadow.temperature(start+Hz,temperature);
  shadow.observe(start+Hz,uint64_t(active.relative_at(start+Hz)-error),start+Hz+100,model(start+Hz,-49000),true);
}
int main(){
  Shadow shadow;shadow.sink(save);
  std::vector<uint64_t> magnitudes;
  for(unsigned i=0;i<140;++i){
    const int64_t error=i==138?INT64_C(9000000000):(i%2?-1:1)*int64_t(i+1);
    trial(shadow,(2+uint64_t(i)*3)*Hz,error);
    const auto& row=rows.back();assert(row.error_ns==error&&row.baseline_error_ns==error);
    assert(row.prediction_ns==row.baseline_ns&&row.local20-row.start20==Hz);
    const auto q=shadow.view();assert(q.completed==i+1&&!q.thermal_ready);
    if(i<99)assert(q.p99_ns==UINT64_MAX);else assert(q.p99_ns!=UINT64_MAX);
    magnitudes.push_back(uint64_t(std::llabs(error)));
  }
  const auto q=shadow.view();assert(q.samples==128&&q.started==140&&!q.missed&&!q.aborted);
  std::vector<uint64_t> last(magnitudes.end()-128,magnitudes.end());std::sort(last.begin(),last.end());
  assert(q.p95_ns==last[(95*128+99)/100-1]&&q.p99_ns==last[(99*128+99)/100-1]);
  assert(q.max_ns==9000000000ULL&&q.baseline_max_ns==q.max_ns);
  assert(shadow.view(q.last20+5*Hz+1).p99_ns==UINT64_MAX);
  // Late/no reference produces one missing endpoint, without zero filling.
  auto at=450*Hz;shadow.temperature(at,25);shadow.observe(at-Hz/16,1,at,model(at),true);
  shadow.temperature(at+2*Hz,25);shadow.observe(at+2*Hz,1,at+2*Hz+100,model(at+2*Hz),true);
  assert(shadow.view().missed==1&&shadow.view().samples==128);
  at=455*Hz;shadow.temperature(at,25);shadow.observe(at-Hz/16,1,at,model(at),true);
  assert(shadow.view().holding);shadow.cancel();assert(shadow.view().aborted==1);
  const auto sequence=shadow.view().sequence;shadow.clear();assert(shadow.view().samples==0&&shadow.view().sequence==sequence&&shadow.view().missed==1&&shadow.view().aborted==1);
  // A future temperature sample must never change an earlier physical score.
  Shadow a,b;
  for(unsigned i=0;i<180;++i){const auto start=(2+uint64_t(i)*3)*Hz;
    const double temperature=20+.02*i;
    a.temperature(start,temperature);b.temperature(start,temperature);
    const auto active=model(start,100+100*temperature);
    a.observe(start-Hz/16,1,start,active,true);b.observe(start-Hz/16,1,start,active,true);
    if(i==179){b.temperature(start+Hz+Hz/16,70);}
    const auto remote=uint64_t(active.relative_at(start+Hz));
    a.observe(start+Hz,remote,start+Hz+Hz/8,active,true);b.observe(start+Hz,remote,start+Hz+Hz/8,active,true);
  }
  assert(a.view().thermal_ready&&b.view().thermal_ready);
  assert(a.view().p99_ns==b.view().p99_ns&&a.view().max_ns==b.view().max_ns);
  const auto before=a.view();a.temperature(UINT64_MAX,NAN);a.observe(1,UINT64_MAX,0,{},false);
  assert(a.view().samples==before.samples&&a.view().completed==before.completed);
  std::puts("PASS shadow: isolated feedback, causal temperature, fixed horizon, exact rolling P95/P99, 9s tail, missing/abort/scope/freshness");
}

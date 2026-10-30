// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "../main/w12_shadow_holdover.hpp"
#include <cassert>
#include <cstdio>
#include <vector>
using Shadow=v12::W12ShadowHoldover;
constexpr uint64_t Hz=Shadow::Hz;
std::vector<Shadow::Sample> rows;
void save(const Shadow::Sample& s){rows.push_back(s);}
v12::G1ClockPrediction model(uint64_t at,double rate=123){return {at-1000,INT64_C(1000000000000)+int64_t(at-1000)*50,100,rate,0};}
void begin(Shadow& s,uint64_t at,double temperature=25){s.temperature(at,temperature);const auto active=model(at);
  s.observe(at-Hz/16,uint64_t(active.relative_at(at-Hz/16)),at,active,true);assert(s.view().holding);}
void score(Shadow& s,uint64_t start,unsigned eighths,int64_t error){const auto at=start+eighths*Hz/8;
  if(eighths%8==0)s.temperature(at,25);
  // Live phase/rate can change arbitrarily; they cannot correct the held model.
  s.observe(at,uint64_t(model(start).relative_at(at)-error),at+100,model(at,-49000),true);}
int main(){
  Shadow shadow;shadow.sink(save);const auto start=2*Hz;begin(shadow,start);
  std::vector<uint64_t> errors;
  for(unsigned i=1;i<=960;++i){const int64_t error=i==138?INT64_C(9000000000):int64_t(i%160);
    score(shadow,start,i,error);errors.push_back(uint64_t(error));
    assert(rows.back().error_ns==error&&rows.back().baseline_error_ns==error);
    assert(rows.back().prediction_ns==rows.back().baseline_ns);
    if(i<100)assert(shadow.view().p99_ns==UINT64_MAX);
    if(i==800)assert(shadow.view().cross20==138*Hz/8&&shadow.view().safe20==137*Hz/8);
  }
  const auto q=shadow.view();assert(q.samples==128&&q.started==1&&q.completed==1&&!q.holding&&!q.missed&&!q.aborted);
  std::vector<uint64_t> last(errors.end()-128,errors.end());std::sort(last.begin(),last.end());
  assert(q.p95_ns==last[(95*128+99)/100-1]&&q.p99_ns==last[(99*128+99)/100-1]);
  assert(q.run_max_ns==9000000000ULL&&q.baseline_run_max_ns==q.run_max_ns&&q.max_ns==159);
  assert(shadow.view(q.last20+5*Hz+1).p99_ns==UINT64_MAX);
  // Reaching 1us at 100s is measured directly, with no one-second reset.
  Shadow long_run;begin(long_run,start);
  for(unsigned i=1;i<=960;++i)score(long_run,start,i,int64_t(i*10/8));
  assert(long_run.view().cross20==100*Hz&&long_run.view().safe20==100*Hz-Hz/8);
  assert(long_run.view().baseline_cross20==100*Hz&&long_run.view().run_max_ns==1200);
  // A full stable run is censored at the actual tested cap, not a failure.
  Shadow stable;begin(stable,start);
  for(unsigned i=1;i<=960;++i)score(stable,start,i,99);
  assert(stable.view().safe20==120*Hz&&!stable.view().cross20&&stable.view().completed==1);
  // No interpolation across absent BLE or temperature; no zero-filled scores.
  Shadow missing;begin(missing,start);score(missing,start,1,99);
  score(missing,start,5,99);assert(missing.view().missed==1&&missing.view().aborted==1&&!missing.view().holding);
  assert(missing.view().samples==1&&missing.view().p99_ns==UINT64_MAX);
  const auto sequence=missing.view().sequence;missing.clear();assert(!missing.view().samples&&missing.view().sequence==sequence&&missing.view().missed==1);
  Shadow no_temperature;begin(no_temperature,start);
  for(unsigned i=1;i<=25;++i){const auto at=start+i*Hz/8;no_temperature.observe(at,1,at+100,model(at),true);}
  assert(no_temperature.view().missed==1&&no_temperature.view().aborted==1);
  // Train future coefficients while the first run stays frozen, then exercise
  // nonzero thermal response over >8s of evicted history and a late follow-up.
  Shadow a,b;a.sink(save);b.sink(save);
  for(unsigned i=0;i<=320;++i){const auto at=start+i*Hz/8;const double temp=20+.01*i;
    if(i%8==0){a.temperature(at,temp);b.temperature(at,temp);}
    const auto active=model(at,100+100*temp);
    a.observe(at,uint64_t(active.relative_at(at)),at+100,active,true);
    b.observe(at,uint64_t(active.relative_at(at)),at+100,active,true);
  }
  assert(a.view().thermal_ready&&b.view().thermal_ready);a.cancel();b.cancel();
  const auto second=start+41*Hz;const auto active=model(second,100+100*23.2);
  a.temperature(second,23.2);b.temperature(second,23.2);
  a.observe(second,1,second,active,true);b.observe(second,1,second,active,true);
  for(unsigned i=1;i<=160;++i){const auto at=second+i*Hz/8;
    if(i%8==0){a.temperature(at,23.2+.01*i);b.temperature(at,23.2+.01*i);}
    if(i==160)b.temperature(at+Hz/16,70); // Available after physical RX.
    const auto remote=uint64_t(active.relative_at(at));
    a.observe(at,remote,at+Hz/8,model(at,49000),true);const auto ar=rows.back();
    b.observe(at,remote,at+Hz/8,model(at,-49000),true);const auto br=rows.back();
    assert(ar.prediction_ns==br.prediction_ns&&ar.error_ns==br.error_ns);
  }
  assert(a.view().holding&&a.view().samples==128&&a.view().age20==20*Hz);
  assert(rows.back().prediction_ns!=rows.back().baseline_ns);
  a.temperature(second+30*Hz,25);assert(!a.view().holding); // Broken thermal history.
  const auto before=b.view();b.temperature(UINT64_MAX,NAN);b.observe(1,UINT64_MAX,0,{},false);
  assert(b.view().samples==before.samples&&b.view().completed==before.completed);
  std::puts("PASS 120s frozen runs, 100s crossing, censored duration, coverage gaps, exact rolling tails, 9s outlier retained, cumulative causal thermal history, frozen learning and lifecycle");
}

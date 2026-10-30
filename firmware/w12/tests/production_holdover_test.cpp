// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "../main/w12_shadow_holdover.hpp"
#include <cassert>
#include <cstdio>
using v12::G1ClockPrediction;using v12::W12ShadowHoldover;
constexpr uint64_t Hz=W12ShadowHoldover::Hz;
W12ShadowHoldover::Sample saved;
void save(const W12ShadowHoldover::Sample& s){saved=s;}
G1ClockPrediction model(uint64_t at,double rate){return {at,INT64_C(1000000000000)+int64_t(at)*50,0,rate,INT64_C(1700000000000000000)};}
int main(){
 W12ShadowHoldover shadow;shadow.sink(save);
 for(unsigned i=0;i<360;++i){const auto at=(2*Hz)+i*Hz/8;const auto c=20+.01*i;
  if(i%8==0)shadow.temperature(at,c);
  auto raw=model(at,100+100*c);shadow.observe(at,uint64_t(raw.relative_at(at)),at,raw,true);}
 assert(shadow.view().thermal_ready);shadow.cancel();
 const auto start=48*Hz;shadow.temperature(start,23.6);auto raw=model(start,2460);
 shadow.observe(start,1,start,raw,true);auto production=raw;
 assert(shadow.arm(production,start)&&production.thermal.enabled&&production.thermal.beta!=0);
 const double coefficient=production.thermal.beta;
 for(unsigned i=1;i<960;++i){const auto at=start+i*Hz/8;const double c=23.6+.001*i;
  if(i%8==0){shadow.temperature(at,c);production.thermal.temperature(at,c);}
  const auto reference=uint64_t(raw.relative_at(at));shadow.observe(at,reference,at+Hz/16,model(at,-49000),true);
  assert(saved.local20==at&&std::llabs(production.relative_at(at)-saved.prediction_ns)<=1);
  assert(production.thermal.beta==coefficient); // Future learning cannot alter held coefficients.
 }
 // The selected delivery clock supplies the recorded output, including UTC
 // and the existing receiver adjustment. A later sensor point is not used.
 shadow.cancel();const auto at=start+121*Hz;shadow.temperature(at,24.6);
 production=model(at,100);production.receiver_phase_ns=7671;assert(shadow.arm(production,at));
 auto baseline=production;baseline.thermal={};baseline.receiver_phase_ns=0;
 shadow.audit_start(at,baseline);
 for(unsigned i=1;i<100;++i){const auto edge=at+i*Hz/8;
  if(i%8==0){shadow.temperature(edge,25);production.thermal.temperature(edge,25);}
  if(i==99){shadow.temperature(edge+Hz/16,60);production.thermal.temperature(edge+Hz/16,60);}
  const auto reference=uint64_t(baseline.relative_at(edge));shadow.observe(edge,reference,edge+Hz/8,model(edge,49000),true,&production);
  assert(saved.production&&saved.output_ns==production.at(edge));
  assert(saved.output_ns==saved.prediction_ns+saved.utc_offset_ns+saved.receiver_ns+saved.output_correction_ns);
  assert(saved.error_ns==saved.prediction_ns-int64_t(reference));
 }
 shadow.audit_end(true,at+13*Hz);assert(!shadow.view().holding);
 production.thermal.temperature(at+20*Hz,25);assert(!production.at(at+20*Hz));
 // Returning to the authenticated servo changes phase by slew, not a step.
 auto target=model(400*Hz,200);auto held=target;held.receiver_phase_ns=7671;held.epoch-=5000;
 const auto edge=401*Hz;const auto delivered=held.at(edge);
 assert(target.resume_from(held,edge,7671));auto output=target;output.receiver_phase_ns=7671;
 assert(output.at(edge)==delivered&&target.phase==5000);
 assert(output.at(edge+Hz/8)>delivered+125000000); // Bounded correction progresses.
 auto gross=model(400*Hz,200);held.epoch-=2000000;assert(!gross.resume_from(held,edge,7671));
 std::puts("PASS promoted projection agrees with shadow over120s, frozen coefficients, actual UTC output audit, causal future temperature and missing-sensor fail-closed");
}

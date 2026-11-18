// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "dual_clock.hpp"
#include <cassert>
#include <cstdio>
using namespace watch::dual;
constexpr uint64_t UTC=UINT64_C(1790000000000000000);
static Observation observation(uint64_t tick,uint64_t id,uint64_t scope=1,double rate=100){
 Observation o;o.tick=tick;o.utc_ns=UTC+tick*50;o.until=tick+40000000;o.scope=scope;o.id=id;o.domain=1;
 o.enclosure_ns=50;o.rate_ppb=rate;o.eligible=o.calibrated=true;return o;
}
int main(){
 Bridge b;b.before20=20000000;b.after20=20000004;b.xtal20=21000000;b.available20=20000010;
 b.radio={20999990,20999994,16000000,1000000,1};uint64_t native=0;uint32_t bound=0;
 assert(b.convert(40000000,native,bound)&&bound>=163&&bound<1000);
 assert(!b.convert(20000010,native,bound));assert(!b.convert(70000000,native,bound));
 b.radio.after+=401;assert(!b.convert(40000000,native,bound));b.radio.after-=401;
 Bridge reverse=b;reverse.before20=reverse.after20=20000000;reverse.xtal20=0;reverse.xtal_lower20=20999998;reverse.xtal_upper20=21000002;
 assert(reverse.convert(40000000,native,bound)&&bound<1000);reverse.xtal_upper20=reverse.xtal_lower20+401;assert(!reverse.convert(40000000,native,bound));
 n30_lora_view l{};l.model_valid=true;l.model_kind=1;l.capture_epoch=l.model_epoch=1;
 l.edge_tick=l.model_tick16=16000000;l.model_ns=UTC;l.model_rate_milli_ppb=100000;
 l.model_slow_accel_micro_ppb_s=2000000;l.model_fast_accel_micro_ppb_s=-1000000;l.model_phase_ns=-500;
 uint64_t ns;assert(lora_project(l,32000000,ns));
 const auto expected=UTC+1000000000+uint64_t(int64_t(std::llround(100+2*120*(1-120*(1-n30_lora_decay(1)))-30*(1-30*(1-n30_lora_decay_fast(1)))-500)));
 assert(ns==expected);assert(!lora_project(l,15999999,ns));l.model_epoch=2;assert(!lora_project(l,32000000,ns));l.model_epoch=1;
 double predicted_rate=0;assert(lora_project(l,32000000,ns,&predicted_rate));
 const auto expected_rate=100+2*120*(1-n30_lora_decay(1))-30*(1-n30_lora_decay_fast(1));
 assert(std::abs(predicted_rate-expected_rate)<1e-9);
 l.model_phase_ns=500;assert(lora_project(l,32000000,ns,&predicted_rate));assert(std::abs(predicted_rate-expected_rate)<1e-9);
 l.model_phase_ns=-500; // Phase slew must not masquerade as oscillator rate.
 assert(!lora_project(l,16000000+UINT64_C(121)*16000000,ns));
 l.watch_valid=true;l.requested=l.active=true;l.state=2;l.sampled_us=1;l.until_us=1000000;
 assert(!strict_lora(l,100));l.time_valid=l.metadata_known=true;l.request_generation=1;l.emitter=l.emitter_boot=l.root=l.root_boot=1;
 assert(strict_lora(l,100));assert(!strict_lora(l,250001));assert(!strict_lora(l,1000000));
 Clock off(1,1,false);auto ble=observation(20000000,1);assert(!off.update(20000000,1,ble,{}).valid);
 Clock c(7,8,true);auto v=c.update(20000000,8,ble,{});assert(v.valid&&v.mode==Mode::Ble&&v.instance==7&&v.fine);
 auto first=v.utc_ns;auto updates=v.updates;
 v=c.update(20000001,8,ble,{});assert(v.updates==updates&&v.utc_ns>=first); // repeated physical observation
 auto bad=ble;bad.eligible=false;v=c.update(40000000,8,bad,{});assert(v.mode==Mode::Hold&&!v.authoritative&&!v.fine);
 // A coarse pair cannot replace the learned fine rate; one clock survives loss.
 auto lor=observation(60000000,1,2,-1000);lor.utc_ns+=1000000;
 v=c.update(60000000,8,{},lor);assert(v.mode==Mode::LoRa&&!v.fine&&v.rate_ppb==100&&v.instance==7);
 const auto before=v.utc_ns;v=c.update(80000000,8,{},lor);assert(v.utc_ns>before&&v.updates==updates+1);
 // Both disappear: same learned oscillator, pending coarse phase is cancelled.
 v=c.update(100000000,8,{},{});assert(v.mode==Mode::Hold&&!v.authoritative&&v.rate_ppb==100);
 auto held=v.utc_ns;v=c.update(120000000,8,{},{});assert(v.utc_ns-held==1000000100&&v.holdover_age_us>=1000000);
 // BLE return needs100ms with genuinely advancing observations. No UTC step.
 uint64_t previous=v.utc_ns;
 for(unsigned i=0;i<133;++i){const auto t=140000000+uint64_t(i)*5000000;ble=observation(t,100+i);ble.utc_ns-=1000000;
  v=c.update(t,8,ble,{});assert(v.utc_ns>previous&&v.instance==7&&v.domain==8);previous=v.utc_ns;
  if(i<1)assert(v.mode==Mode::Hold);else assert(v.mode==Mode::Ble);}
 // Sub-nanosecond steering survives frequent owner polls in holdover.
 Clock fractional(9,10,true);auto seed=observation(20000000,1,1,17.25);
 auto start=fractional.update(20000000,10,seed,{}).utc_ns;
 for(unsigned i=1;i<=1000;++i)v=fractional.update(20000000+uint64_t(i)*20000,10,{},{});
 assert(v.utc_ns-start==1000000017&&v.rate_ppb==17.25);
 // A short observer refusal cancels steering during HOLD. Resuming the same
 // model restores bounded phase steering without a new training observation.
 Clock resume(19,20,true);auto rb=observation(20000000,1,1,0);resume.update(20000000,20,rb,{});
 auto rl=observation(40000000,1,2,0);rl.utc_ns+=100000;
 v=resume.update(40000000,20,{},rl);const auto resumed_updates=v.updates;
 resume.update(42000000,20,{},rl);v=resume.update(44000000,20,{},{});assert(v.mode==Mode::Hold);
 const auto old_phase=int64_t(v.utc_ns)-int64_t(UTC+UINT64_C(2200000000));
 rl.rate_ppb=9999;resume.update(46000000,20,{},rl);v=resume.update(48000000,20,{},rl);
 assert(v.mode==Mode::LoRa&&v.updates==resumed_updates&&v.rate_ppb==0);
 assert(int64_t(v.utc_ns)-int64_t(UTC+UINT64_C(2400000000))>old_phase);
 v=resume.update(82000000,20,{},rl);assert(v.mode==Mode::Hold&&!v.authoritative); // Original expiry, no poll renewal.
 // A brief mapping refusal withdraws authority but preserves already-qualified
 // BLE progress. A fresh authenticated projection can return immediately;
 // a real absence or changed context still requires the full return dwell.
 Clock qualified(31,32,true);uint64_t qt=0;
 for(unsigned i=0;i<=310;++i){qt=20000000+uint64_t(i)*2000000;qualified.update(qt,32,observation(qt,i+1,1,0),{});}
 auto coarse_gap=qualified;
 v=qualified.update(qt+2000000,32,{},{});assert(v.mode==Mode::Hold&&!v.authoritative);
 v=qualified.update(qt+4000000,32,observation(qt+4000000,400,1,0),{});
 assert(v.mode==Mode::Ble&&v.fine&&v.instance==31); // Same fresh context, 200ms gap.
 auto gl=observation(qt+2000000,1,2,0);
 assert(coarse_gap.update(qt+2000000,32,{},gl).mode==Mode::LoRa);
 assert(coarse_gap.update(qt+4000000,32,observation(qt+4000000,400,1,0),gl).mode==Mode::Ble);
 auto changed=qualified;changed.update(qt+6000000,32,{},{});
 assert(changed.update(qt+8000000,32,observation(qt+8000000,401,2,0),{}).mode==Mode::Hold);
 auto changed_domain=qualified;changed_domain.update(qt+6000000,32,{},{});
 auto other_domain=observation(qt+8000000,401,1,0);other_domain.domain=2;
 assert(changed_domain.update(qt+8000000,32,other_domain,{}).mode==Mode::Hold);
 // More than 500ms without advancing physical BLE progress revokes qualification.
 qualified.update(qt+16000000,32,{},{});
 for(unsigned i=0;i<=300;++i){const auto t=qt+18000000+uint64_t(i)*2000000;
  v=qualified.update(t,32,observation(t,500+i,1,0),{});
  assert(v.mode==(i<1?Mode::Hold:Mode::Ble));}
 // An expired projection cannot recover authority during a short gap.
 auto expiring=observation(qt+620000000,900,1,0);expiring.until=qt+622000000;
 qualified.update(qt+620000000,32,expiring,{});
 v=qualified.update(qt+624000000,32,expiring,{});assert(v.mode==Mode::Hold&&!v.authoritative);
 // A source without an applicable measured correction remains usable; its
 // calibration precision is explicitly unknown, rather than blocking roaming.
 Clock roaming(51,52,true);auto uncal=observation(20000000,1,99,0);uncal.calibrated=false;
 v=roaming.update(20000000,52,uncal,{});assert(v.mode==Mode::Ble&&v.authoritative&&!v.calibrated&&v.uncertainty_floor_ns==UINT32_MAX);
 // Stale, future, unauthenticated, nonfinite and wrong-domain inputs refuse.
 auto invalid=observation(900000000,999);invalid.eligible=false;
 v=c.update(900000000,8,invalid,{});assert(v.mode==Mode::Hold);
 invalid.eligible=true;invalid.tick=1000000000;assert(!invalid.live(900000000));
 invalid.tick=900000000;invalid.rate_ppb=std::numeric_limits<double>::quiet_NaN();assert(!invalid.live(900000000));
 assert(!c.update(920000000,99,ble,{}).authoritative);
 // Counter reversal invalidates the actuator; no false monotonic success.
 assert(!c.update(1,8,ble,{}).valid);
 std::puts("PASS dual model/bridge/controller: expiry, rate hold, dwell, monotonic return, calibration and domain guards");
}

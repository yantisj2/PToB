// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "watch_holdover.hpp"
#include "radio_candidates.hpp"
#include <cstdio>
#include <cstdlib>
#define CHECK(condition) do {if(!(condition)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#condition);std::exit(1);}} while(0)
using namespace watch;
uint64_t fake_now=0;
namespace {
constexpr uint64_t Hz=EmpiricalHoldover::Hz,Base=UINT64_C(1000000000000);
unsigned diagnostic_pairs=0;
void diagnostic(const v12::n30_probe::Pair&){++diagnostic_pairs;}
v12::G1ClockPrediction model(uint64_t at,double rate=0){return {at,int64_t(Base+at*50),0,rate,INT64_C(1700000000000000000)};}
v12::R2Status state(uint64_t at,double rate=0){
 v12::R2Status s{};const auto now=at/20;
 s.requested=s.following=s.clock_valid=s.g1_frequency_trained=true;
 s.root_identity=111;s.root_boot=55;s.source_epoch=2;s.publication=3;
 s.g1_clock_origin=111;
 s.rx_capture.epoch=1;
 s.follower_snapshot_us=now;s.follower_utc_us=UINT64_C(1700000000000000)+now;
 s.g1_anchor_deadline_us=s.service_until_us=s.g1_corrected_until_us=now+1500000;
 s.g1_clock_model=model(at,rate);s.g1_relative_quality.evaluated20=at;
 s.g1_fit_points=64;s.g1_fit_span_ms=3937;s.g1_corrected_accepted=500;s.g1_corrected_refused=3;
 s.g1_corrected_original20=at-Hz/16;
 s.g1_relative_quality.scope_generation=1;s.g1_relative_quality.samples=256;
 s.g1_relative_quality.rms_ns=60;s.g1_relative_quality.max_ns=150;
 s.g1_relative_quality.original20=at;s.g1_relative_quality.rate_guard_ppb=5;
 return s;
}
v12::n30_probe::Pair pair(uint64_t at,int64_t error=0){
 v12::n30_probe::Pair p{};p.root=111;p.boot=55;p.source=2;p.train=3;p.epoch=1;
 p.local20=at;p.available20=at+200;p.remote_ns=uint64_t(int64_t(Base+at*50)+error);
 p.created_ns=p.remote_ns-1000000;p.expires_ns=p.remote_ns+1000000000;
 p.publication=uint32_t(at/1250000);p.event=uint16_t(p.publication);p.channel=4;p.causal=true;
 return p;
}
void simulate(EmpiricalHoldover& h,unsigned seconds,double rate=0,bool witness=true){
 for(uint64_t at=Hz;at<=(seconds+1)*Hz;at+=Hz/16){
  if(witness)h.observe(at,Base+at*50,at+200);
  h.tick(at+200);h.start(model(at+200,rate),at+200);
 }
}
}
int main(){
 EmpiricalHoldover horizon;
 simulate(horizon,20);auto result=horizon.view();
 CHECK(result.ready&&result.duration_ms>=2000&&result.lower_bound&&result.trials>=100);
 CHECK(!horizon.view(result.last20+6*Hz).ready);
 horizon.clear();simulate(horizon,150,500);result=horizon.view();
 CHECK(result.ready&&result.duration_ms==1750&&!result.lower_bound&&result.trials>=100);
 // An early crossing remains in every later prefix, even after later fits
 // could hide it. Exactly1000ns is outside the strict sub1us criterion.
 horizon.clear();
 for(uint64_t at=Hz;at<=20*Hz;at+=Hz/16){
  horizon.observe(at,Base+at*50+(at%(Hz/2)==0?1000:0),at+200);
  horizon.tick(at+200);horizon.start(model(at+200),at+200);
 }
 result=horizon.view();CHECK(result.ready&&result.duration_ms<=250&&!result.lower_bound);
 EmpiricalHoldover missing;simulate(missing,25,0,false);result=missing.view();
 CHECK(!result.ready&&result.unknown>0&&result.completed==0);
 // The runtime adapter chains the existing diagnostics hook once, preserving
 // every pair and leaving source models/permissions untouched.
 v12::n30_probe::pair_sink=diagnostic;holdover_init();const auto hook=v12::n30_probe::pair_sink;
 holdover_init();CHECK(v12::n30_probe::pair_sink==hook&&hook!=diagnostic);
 uint64_t at=Hz;v12::R2Status s;
 for(;at<=151*Hz;at+=Hz/16){
  v12::n30_probe::pair_sink(pair(at));s=state(at+200,500);
  const auto copy=s.g1_clock_model;
  result=holdover_update(s,s.follower_snapshot_us,false);
  CHECK(s.g1_clock_model.tick==copy.tick&&s.g1_clock_model.epoch==copy.epoch&&s.g1_clock_model.rate==copy.rate);
 }
 CHECK(diagnostic_pairs==2401&&result.ready&&result.duration_ms==1750&&!result.lower_bound);
 const auto servo=holdover_status().servo;
 CHECK(servo.trained&&servo.rate_mppb==500000&&servo.rms_ns==60&&servo.max_ns==150&&servo.samples==256);
 CHECK(servo.accepted==500&&servo.refused==3&&servo.fit_points==64&&servo.fit_span_ms==3937&&servo.original_age_us==62500);
 const auto before_overflow=holdover_status();
 for(unsigned i=0;i<20;++i)v12::n30_probe::pair_sink(pair(at+i*Hz/16));
 CHECK(holdover_status().queued==16&&holdover_status().dropped==4);
 s=state(at+20*Hz/16+200,500);holdover_update(s,s.follower_snapshot_us,false);
 CHECK(holdover_status().drained==before_overflow.drained&&holdover_status().queued==0);
 DisplayState display{};display.r2_relative_lock=true;display.rms_ns=60;
 // This reproduces the former wiring bug: strict calibration fields remain
 // zero while the functional G1 clock now supplies actual measured trials.
 CHECK(!s.model_available20&&!s.hold_samples&&!s.hold_ms);
 auto ble=ble_candidate(s,display,s.follower_snapshot_us,false,result);
 CHECK(ble.clock.hold_valid&&ble.clock.hold_ms==1750&&ble.clock.hold_trials>=100&&!ble.clock.hold_lower);
 auto lower=ble.clock;lower.hold_lower=true;lower.hold_ms=58000;
 const auto shown_at=s.follower_snapshot_us;
 CHECK(sampled_hold(lower,shown_at)&&!sampled_hold(lower,shown_at+250000));
 InstrumentMotion motion;motion.apply(lower,shown_at);
 for(unsigned i=1;i<=100;++i){
  const auto frame_at=shown_at+i*50000;lower.hold_sampled_us=frame_at;lower.hold_until_us=frame_at+250000;
  motion.apply(lower,frame_at);
 }
 CHECK(motion.hold>.9&&lower.hold_position>.9);
 // Explicit receiver provenance survives a different delivered-clock mode.
 lower.holding=true;lower.source=Source::LOR;CHECK(sampled_hold(lower,shown_at+5000000));
 CHECK(!sampled_hold(lower,shown_at+5250000));
 // The legacy model has no independent receiver evidence.
 lower.hold_until_us=0;CHECK(!sampled_hold(lower,shown_at));
 MobileSelection selection;Candidate absent;
 CHECK(selection.select(ble,absent,absent,s.follower_snapshot_us).hold_valid);
 auto lor=ble;lor.clock.source=Source::LOR;lor.clock.hold_valid=false;
 CHECK(!selection.select(absent,lor,absent,s.follower_snapshot_us+100000).hold_valid);
 CHECK(!ble_candidate(s,display,s.follower_snapshot_us+250000,false,result).clock.hold_valid);
 s.requested=false;CHECK(!holdover_update(s,s.follower_snapshot_us,false).ready);
 CHECK(holdover_status().view.trials==0&&holdover_status().view.started>100);
 // Every context field that can change source coordinates invalidates prior
 // trials. BLE OFF/ON cannot restore the earlier result before new witnesses.
 HoldoverMonitor monitor;
 for(unsigned change=0;change<11;++change){
  for(uint64_t tick=Hz;tick<=14*Hz;tick+=Hz/16){
   auto p=pair(tick);monitor.pair(p);s=state(tick+200);
   if(change>=8){s.rc7_bound=true;s.rc7_depth=1;s.joined_peer=333;s.joined_boot=444;}
   result=monitor.update(s,s.follower_snapshot_us,false);
  }
  CHECK(result.ready&&result.trials>=100);
  auto stale=pair(15*Hz);stale.epoch=99;
  const auto old_ignored=monitor.status().ignored;const auto old_trials=monitor.status().view.trials;
  monitor.pair(stale);CHECK(monitor.status().ignored==old_ignored+1&&monitor.status().view.trials==old_trials);
  if(change==0)++s.root_identity;
  if(change==1)++s.root_boot;
  if(change==2)++s.source_epoch;
  if(change==3)++s.publication;
  if(change==4)++s.g1_relative_quality.scope_generation;
  if(change==5){s.rc7_bound=true;s.rc7_depth=1;s.joined_peer=333;s.joined_boot=444;}
  if(change==6){++s.rx_capture.epoch;}
  if(change==7){s.requested=false;}
  if(change==8){++s.joined_boot;}
  if(change==9){++s.rc7_depth;}
  if(change==10){++s.joined_peer;}
  CHECK(!monitor.update(s,s.follower_snapshot_us,false).ready&&monitor.status().view.trials==0);
 }
 // Source expiry, a stale snapshot and retained HOLD cannot expose old P99.
 for(unsigned guard=0;guard<5;++guard){
  HoldoverMonitor guarded;
  for(uint64_t tick=Hz;tick<=14*Hz;tick+=Hz/16){guarded.pair(pair(tick));s=state(tick+200);result=guarded.update(s,s.follower_snapshot_us,false);}
  CHECK(result.ready);
  uint64_t now=s.follower_snapshot_us;
  if(guard==0)s.service_until_us=now;
  if(guard==1)s.g1_corrected_until_us=now;
  if(guard==2)s.g1_anchor_deadline_us=now;
  if(guard==3)now+=250000;
  if(guard==4)s.g1_retained_estimate=true;
  CHECK(!guarded.update(s,now,false).ready);
 }
 // An expired short source grant withdraws the displayed statistic, but a
 // same-scope shadow trial can still be scored by a later authenticated pair.
 // Cancelling every62.5ms grant gap used to turn good future evidence into
 // UNKNOWN and make the P99 disappear repeatedly on the actual watch.
 HoldoverMonitor short_gaps;
 for(uint64_t tick=Hz;tick<=90*Hz;tick+=Hz/16){
  const bool pause=tick>25*Hz&&tick%(2*Hz)==0;
  if(!pause)short_gaps.pair(pair(tick));
  s=state(tick+200);
  if(pause){s.following=false;s.root_identity=s.root_boot=s.source_epoch=s.publication=s.service_until_us=0;}
  result=short_gaps.update(s,s.follower_snapshot_us,false);
  if(pause)CHECK(!result.ready);
  if(tick>60*Hz&&!pause)CHECK(result.ready&&result.duration_ms>=2000&&result.trials>=100);
 }
 CHECK(short_gaps.status().view.aborted==0&&short_gaps.status().view.unknown==0);
 // A physical outage beyond500ms ends unfinished ages as UNKNOWN, even if
 // a still-live source lease has not expired. A prior1us crossing stays FAIL.
 EmpiricalHoldover outage;
 for(uint64_t tick=Hz;tick<30*Hz;tick+=Hz/16){
  if(tick%Hz==0)outage.observe(tick,Base+tick*50,tick+200);
  outage.tick(tick+200);outage.start(model(tick+200),tick+200);
 }
 CHECK(!outage.view().ready&&outage.view().unknown>0&&outage.view().aborted>0);
 EmpiricalHoldover crossed;CHECK(crossed.start(model(Hz),Hz));
 crossed.observe(Hz+Hz/4,Base+(Hz+Hz/4)*50+1000,Hz+Hz/4+200);
 crossed.observe(3*Hz,Base+3*Hz*50,3*Hz+200);
 CHECK(crossed.view().bad==1&&crossed.view().unknown==0&&crossed.view().aborted==1);
 // A model-quality dip must not turn a real future error into a censored
 // sample. It only stops starting new trials and withdraws current display.
 HoldoverMonitor tails;
 for(uint64_t tick=Hz;tick<=24*Hz;tick+=Hz/16){
  const bool bad=tick>=21*Hz&&tick<22*Hz;
  tails.pair(pair(tick,bad?1500:0));s=state(tick+200);
  if(bad)s.g1_relative_quality.rms_ns=2000;
  result=tails.update(s,s.follower_snapshot_us,false);
  if(bad)CHECK(!result.ready);
 }
 CHECK(tails.status().view.aborted==0&&tails.status().view.bad>0&&tails.status().view.unknown==0);
 std::puts("PASS sampled P99: physical future witnesses,100-trial warmup,lower bounds,early tails,unknowns,hook chaining,strict-field wiring,source/parent/generation/radio/expiry fences; software only");
}

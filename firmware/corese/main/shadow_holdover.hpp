// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "holdover_view.hpp"
#include "w12_holdover_rate.hpp"
#include "w12_holdover_horizon.hpp"
namespace corese {
// The candidate is frozen before future BLE witnesses arrive. Neither scoring
// nor the slow HOLD-rate fit can steer the delivered CoreSE clock. No sensor or
// historical absolute temperature intercept enters this rate-only servo.
class ShadowHoldover {
 static constexpr uint64_t Hz=20000000;
 v12::W12HoldoverRate rate_{};v12::W12HoldoverHorizon horizon_{};
 v12::G1ClockPrediction held_{};HoldoverView v_{};
 uint64_t errors_[128]{},last_=0,start_=0,next_=0;unsigned head_=0;
 static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
 public:
 void clear(){auto started=v_.started,completed=v_.completed,aborted=v_.aborted;if(start_)inc(aborted);rate_.clear();horizon_.clear();
  held_={};v_={};v_.started=started;v_.completed=completed;v_.aborted=aborted;start_=next_=last_=0;head_=0;}
 void cancel(){if(start_)inc(v_.aborted);start_=0;v_.holding=false;horizon_.cancel();}
 void pair(uint64_t local,uint64_t remote,uint64_t available,uint8_t channel,uint32_t generation){
  if(generation!=v_.generation){clear();v_.generation=generation;}
  if(!local||local<=last_||available<local||remote>uint64_t(INT64_MAX))return;
  last_=local;rate_.observe(local,remote,available,channel,generation);
  horizon_.observe(local,remote,available);
  if(!start_||local<=start_)return;
  const auto predicted=held_.relative_at(local);
  if(predicted<=0){cancel();return;}
  const auto gap=local-(v_.last20?v_.last20:start_);v_.max_gap20=std::max(v_.max_gap20,gap);
  if(gap>Hz/4)inc(v_.gaps);
  const auto error=uint64_t(std::llabs(predicted-int64_t(remote)));
  errors_[head_]=error;head_=(head_+1)%128;v_.samples=std::min<uint32_t>(v_.samples+1,128);
  v_.last20=local;v_.age20=local-start_;v_.run_max_ns=std::max(v_.run_max_ns,error);
  // Bounded scratch is a member of this owner, not its small host stack.
  std::copy(std::begin(errors_),std::begin(errors_)+v_.samples,std::begin(scratch_));
  std::sort(std::begin(scratch_),std::begin(scratch_)+v_.samples);
  v_.max_ns=scratch_[v_.samples-1];
  if(v_.samples>=100){v_.p95_ns=scratch_[(95*v_.samples+99)/100-1];v_.p99_ns=scratch_[(99*v_.samples+99)/100-1];}
  if(local-start_>=120*Hz){inc(v_.completed);start_=0;v_.holding=false;next_=available+Hz;}
 }
 void tick(const v12::G1ClockPrediction& active,uint64_t at,bool eligible,double guard){
  horizon_.tick(at);
  if(start_&&at>=start_&&at-start_>121*Hz){cancel();next_=at+Hz;}
  // Qualification admits new trials only. A later RMS/lock change must not
  // censor a frozen trial's tails. Authenticated witnesses, scope clear and
  // the original finite run/witness deadlines still govern its outcomes.
  if(!eligible)return;
  rate_.refresh(at);
  auto candidate=active;const auto choice=rate_.choose(at,active.rate,guard);
  if(!v12::W12HoldoverRate::reanchor(candidate,at,choice.rate))return;
  if(horizon_.due(at))horizon_.start(candidate,at);
  if(start_||at<next_||!at||at>UINT64_MAX-121*Hz)return;
  held_=candidate;start_=at;head_=0;
  v_.last20=v_.age20=v_.max_gap20=v_.run_max_ns=0;v_.samples=v_.gaps=0;
  v_.p95_ns=v_.p99_ns=v_.max_ns=UINT64_MAX;v_.holding=true;v_.rate_selected=choice.selected;inc(v_.started);
 }
 HoldoverView view(uint64_t at)const {
  auto v=v_;v.witness_age_us=v.last20&&at>=v.last20?(at-v.last20)/20:UINT64_MAX;const auto h=horizon_.view(at);v.ready=h.ready;v.lower_bound=h.lower_bound;
  v.duration_ms=h.duration_ms;v.probe_ms=h.probe_ms;v.trials=h.trials;v.bad=h.bad;v.unknown=h.unknown;
  if(!v.last20||at<v.last20||at-v.last20>5*Hz)v.p95_ns=v.p99_ns=v.max_ns=UINT64_MAX;
  return v;
 }
 private:uint64_t scratch_[128]{};
};
namespace shadow {
using PairSink=void(*)(uint64_t,uint64_t,uint64_t,uint8_t,uint32_t);
inline PairSink pair_sink=nullptr;
}
static_assert(sizeof(ShadowHoldover)<14000,"Bounded CoreSE shadow workspace");
}

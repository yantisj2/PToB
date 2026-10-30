// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "g1_clock_prediction.hpp"
#include <array>
#include <limits>
namespace v12 {
struct W12ShadowView {
  uint64_t last20=0,sequence=0;
  uint64_t p95_ns=UINT64_MAX,p99_ns=UINT64_MAX,max_ns=UINT64_MAX;
  uint64_t baseline_p95_ns=UINT64_MAX,baseline_p99_ns=UINT64_MAX,baseline_max_ns=UINT64_MAX;
  uint64_t age20=0,safe20=0,cross20=0,baseline_cross20=0;
  uint64_t run_max_ns=0,baseline_run_max_ns=0;
  uint32_t samples=0,started=0,completed=0,missed=0,aborted=0,generation=1;
  int32_t temperature_mc=INT32_MIN,slope_mppb_c=0;
  bool holding=false,thermal_ready=false;
};
// Experimental numerical holdover only. BLE supplies every frequency label and
// scoring reference; silicon temperature is a covariate, never a time source.
// A frozen run lasts 120s. Every matched physical BLE event is a witness,
// never feedback to the held clock. Learning continues for FUTURE runs only.
// P99 describes the latest 128 events within this run, not a duration bound.
class W12ShadowHoldover {
 public:
  static constexpr uint64_t Hz=20000000,Horizon=120*Hz,MaximumGap=Hz/4;
  static constexpr unsigned Capacity=128,Minimum=100;
  struct Sample {uint64_t sequence=0,start20=0,local20=0,reference_ns=0;
    int64_t prediction_ns=0,baseline_ns=0,error_ns=0,baseline_error_ns=0;};
  using Sink=void(*)(const Sample&);
 private:
  struct Temperature {uint64_t tick=0;double measured=0,lagged=0,integral=0;};
  std::array<Temperature,8> temperatures_{};unsigned temperature_count_=0;
  std::array<int64_t,Capacity> errors_{},baseline_errors_{};
  std::array<uint64_t,Capacity> scratch_{};
  W12ShadowView view_{};G1ClockPrediction baseline_{};
  uint64_t start_=0,next_=0,last_label_=0,last_pair_=0,last_witness_=0;
  double weight_=0,sx_=0,sy_=0,sxx_=0,sxy_=0,beta_=0,intercept_=0;
  double start_temperature_=0,start_integral_=0,rate_delta_=0,held_beta_=0;
  unsigned head_=0;Sink sink_=nullptr;
  static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
  static uint64_t magnitude(int64_t n){return n>=0?uint64_t(n):uint64_t(-(n+1))+1;}
  bool temperature_at(uint64_t tick,double& value,double* integral=nullptr)const {
    for(unsigned i=temperature_count_;i>0;--i){const auto& t=temperatures_[i-1];
      if(t.tick<=tick){if(tick-t.tick>3*Hz)return false;
        const double dt=double(tick-t.tick)/Hz;
        const double decay=std::exp(-dt/8.0);
        value=t.measured+(t.lagged-t.measured)*decay;
        if(integral)*integral=t.integral+t.measured*dt+(t.lagged-t.measured)*8*(1-decay);
        return true;}}
    return false;
  }
  // The cumulative primitive survives history eviction. Evaluate at physical
  // RX time, never with a later sensor reading or reference correction.
  bool thermal_integral(uint64_t end,double& integral)const {
    double value=0,primitive=0;
    if(!temperature_at(end,value,&primitive))return false;
    integral=primitive-start_integral_-start_temperature_*double(end-start_)/Hz;
    return true;
  }
  void quantiles(const std::array<int64_t,Capacity>& values,uint64_t& p95,uint64_t& p99,uint64_t& maximum){
    for(unsigned i=0;i<view_.samples;++i)scratch_[i]=magnitude(values[i]);
    std::sort(scratch_.begin(),scratch_.begin()+view_.samples);
    maximum=scratch_[view_.samples-1];
    if(view_.samples>=Minimum){p95=scratch_[(95*view_.samples+99)/100-1];p99=scratch_[(99*view_.samples+99)/100-1];}
    else p95=p99=UINT64_MAX;
  }
  void learn(uint64_t available,double temperature,double rate){
    if(last_label_&&available-last_label_<Hz)return;
    last_label_=available;
    constexpr double decay=.995;
    weight_=weight_*decay+1;sx_=sx_*decay+temperature;sy_=sy_*decay+rate;
    sxx_=sxx_*decay+temperature*temperature;sxy_=sxy_*decay+temperature*rate;
    const double variance=sxx_-sx_*sx_/weight_;
    // This first candidate has a bounded linear temperature response and an
    // eight-second thermal proxy. Narrow/unseen temperature coverage does not
    // invent a slope. Held coefficients never change during a run.
    view_.thermal_ready=weight_>=32&&variance/weight_>=.04;
    if(view_.thermal_ready){beta_=std::clamp((sxy_-sx_*sy_/weight_)/variance,-5000.0,5000.0);
      intercept_=(sy_-beta_*sx_)/weight_;view_.slope_mppb_c=int32_t(std::llround(beta_*1000));}
  }
 public:
  void sink(Sink fn){sink_=fn;}
  void clear(){auto old=view_;if(start_)inc(old.aborted);
    // Invalidate histories by count instead of constructing a 4KiB temporary
    // on the NimBLE owner's stack. Old slots cannot enter a new population.
    view_={};baseline_={};temperature_count_=head_=0;start_=next_=last_label_=last_pair_=last_witness_=0;
    weight_=sx_=sy_=sxx_=sxy_=beta_=intercept_=start_temperature_=start_integral_=rate_delta_=held_beta_=0;
    view_.sequence=old.sequence;view_.started=old.started;view_.completed=old.completed;view_.missed=old.missed;view_.aborted=old.aborted;
    view_.generation=old.generation==UINT32_MAX?UINT32_MAX:old.generation+1;}
  void cancel(){if(start_)inc(view_.aborted);start_=0;view_.holding=false;next_=0;
    view_.p95_ns=view_.p99_ns=UINT64_MAX;}
  void temperature(uint64_t tick,double celsius){
    if(!tick||!std::isfinite(celsius)||celsius<-40||celsius>125)return;
    double lagged=celsius,integral=0;
    if(temperature_count_){const auto& old=temperatures_[temperature_count_-1];
      if(tick<=old.tick)return;
      if(tick-old.tick<=3*Hz){double ignored=0;temperature_at(tick,ignored,&integral);
        lagged=ignored;
      }else {if(start_)inc(view_.missed);cancel();temperature_count_=0;}}
    if(temperature_count_==temperatures_.size()){
      for(unsigned i=1;i<temperature_count_;++i){temperatures_[i-1]=temperatures_[i];}
      --temperature_count_;}
    temperatures_[temperature_count_++]={tick,celsius,lagged,integral};view_.temperature_mc=int32_t(std::llround(celsius*1000));
  }
  void observe(uint64_t local,uint64_t reference,uint64_t available,const G1ClockPrediction& active,bool trained){
    if(!local||available<local||reference>uint64_t(INT64_MAX)||local<=last_pair_)return;
    last_pair_=local;
    double temperature=0;
    const bool ready=trained&&active.tick&&active.tick<=available&&std::isfinite(active.rate)&&std::abs(active.rate)<=50000&&
      temperature_at(available,temperature);
    if(ready)learn(available,temperature,active.rate); // Future runs only.
    if(start_&&local>start_){
      // Silence cannot prove survival. A coverage failure aborts the trial and
      // is retained separately from a measured threshold crossing.
      if(local-last_witness_>MaximumGap){inc(view_.missed);cancel();next_=available+Hz;return;}
      last_witness_=local;
      double integral=0;const auto base=baseline_.relative_at(local);
      if(base>0&&thermal_integral(local,integral)){
        const double correction=double(local-start_)/Hz*rate_delta_+held_beta_*integral;
        if(std::isfinite(correction)&&std::abs(correction)<1000000){
          const int64_t adjustment=int64_t(std::llround(correction));
          if((adjustment>0&&base>INT64_MAX-adjustment)||(adjustment<0&&base<=-adjustment)){
            inc(view_.missed);cancel();next_=available+Hz;return;}
          const int64_t predicted=base+adjustment;
          const int64_t error=predicted-int64_t(reference),baseline_error=base-int64_t(reference);
          errors_[head_]=error;baseline_errors_[head_]=baseline_error;head_=(head_+1)%Capacity;
          if(view_.samples<Capacity){++view_.samples;}
          ++view_.sequence;view_.last20=local;view_.age20=local-start_;
          view_.run_max_ns=std::max(view_.run_max_ns,magnitude(error));
          view_.baseline_run_max_ns=std::max(view_.baseline_run_max_ns,magnitude(baseline_error));
          if(!view_.cross20){if(magnitude(error)>=1000)view_.cross20=view_.age20;else view_.safe20=view_.age20;}
          if(!view_.baseline_cross20&&magnitude(baseline_error)>=1000)view_.baseline_cross20=view_.age20;
          quantiles(errors_,view_.p95_ns,view_.p99_ns,view_.max_ns);
          quantiles(baseline_errors_,view_.baseline_p95_ns,view_.baseline_p99_ns,view_.baseline_max_ns);
          if(sink_)sink_({view_.sequence,start_,local,reference,predicted,base,error,baseline_error});
        }else {inc(view_.missed);cancel();next_=available+Hz;return;}
      }else {inc(view_.missed);cancel();next_=available+Hz;return;}
      // Retain tails after the first crossing; never stop on a good percentile.
      if(local-start_>=Horizon){inc(view_.completed);start_=0;view_.holding=false;next_=available+Hz;}
      return;
    }
    if(start_||!ready||available<next_||available>UINT64_MAX-Horizon-MaximumGap)return;
    baseline_=active;start_=last_witness_=available;start_temperature_=temperature;
    temperature_at(available,temperature,&start_integral_);
    view_.holding=true;inc(view_.started);head_=0;view_.samples=0;
    view_.p95_ns=view_.p99_ns=view_.max_ns=UINT64_MAX;
    view_.baseline_p95_ns=view_.baseline_p99_ns=view_.baseline_max_ns=UINT64_MAX;
    view_.age20=view_.safe20=view_.cross20=view_.baseline_cross20=0;
    view_.run_max_ns=view_.baseline_run_max_ns=0;
    held_beta_=view_.thermal_ready?beta_:0;
    // Blend the temperature-conditioned estimate with the recent BLE fit;
    // the initial candidate is deliberately bounded and remains shadow only.
    const double learned=view_.thermal_ready?intercept_+beta_*temperature:active.rate;
    rate_delta_=std::clamp(.25*(learned-active.rate),-1000.0,1000.0);
  }
  W12ShadowView view(uint64_t now=0)const {auto v=view_;
    if(now&&(!v.last20||now<v.last20||now-v.last20>5*Hz))v.p95_ns=v.p99_ns=UINT64_MAX;
    return v;}
};
namespace w12_shadow {
using PairSink=void(*)(uint64_t,uint64_t,uint64_t,const G1ClockPrediction&,bool,uint32_t);
inline PairSink pair_sink=nullptr;
}
static_assert(sizeof(W12ShadowHoldover)<=4096,"Shadow-only RAM budget");
}

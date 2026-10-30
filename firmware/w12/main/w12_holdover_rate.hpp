// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "g1_clock_prediction.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
namespace v12 {
// Separate from the fast actuator. The host owner stores only already accepted
// physical pairs. All scratch/history is bounded member storage, never a large
// temporary on the receive owner's stack. No temperature or future witness.
class W12HoldoverRate {
 public:
  static constexpr uint64_t Hz=20000000;
  struct Fit {double rate=0,guard=INFINITY;uint32_t points=0,pairs=0,span_ms=0;bool valid=false;};
  struct Choice {double rate=0,delta=0;bool selected=false;};
 private:
  struct Point {uint64_t local=0,remote=0,available=0;uint8_t channel=255;};
  Point points_[160]{};unsigned count_=0;
  double sx_[37]{},sy_[37]{};unsigned n_[37]{};
  uint64_t last_=0,refreshed_=0;uint32_t generation_=0;
  Fit short_{},long_{};
  Fit fit(uint64_t at,unsigned seconds){
    Fit r;std::fill(std::begin(sx_),std::end(sx_),0);std::fill(std::begin(sy_),std::end(sy_),0);std::fill(std::begin(n_),std::end(n_),0);
    const uint64_t begin=at>seconds*Hz?at-seconds*Hz:0;const Point* origin=nullptr;uint64_t last=0;
    for(unsigned i=0;i<count_;++i){const auto& p=points_[i];if(p.local<begin||p.available>at||p.local>at)continue;
      if(!origin){origin=&p;}last=p.local;
      const double x=double(p.local-origin->local)/Hz;
      const double y=double(int64_t(p.remote)-int64_t(origin->remote)-int64_t(p.local-origin->local)*50);
      sx_[p.channel]+=x;sy_[p.channel]+=y;++n_[p.channel];++r.points;}
    if(!origin)return r;
    r.span_ms=uint32_t((last-origin->local)/20000);
    if(r.points<32||r.span_ms<seconds*850)return r;
    double xx=0,xy=0;
    for(unsigned ch=0;ch<37;++ch)if(n_[ch]>1)r.pairs+=n_[ch]-1;
    if(r.pairs<8)return r;
    for(unsigned i=0;i<count_;++i){const auto& p=points_[i];if(p.local<begin||p.available>at||p.local>at||n_[p.channel]<2)continue;
      const double x=double(p.local-origin->local)/Hz-sx_[p.channel]/n_[p.channel];
      const double y=double(int64_t(p.remote)-int64_t(origin->remote)-int64_t(p.local-origin->local)*50)-sy_[p.channel]/n_[p.channel];
      xx+=x*x;xy+=x*y;}
    if(xx<=0)return r;
    r.rate=xy/xx;if(!std::isfinite(r.rate)||std::abs(r.rate)>50000)return r;
    double squares=0;
    for(unsigned i=0;i<count_;++i){const auto& p=points_[i];if(p.local<begin||p.available>at||p.local>at||n_[p.channel]<2)continue;
      const double x=double(p.local-origin->local)/Hz-sx_[p.channel]/n_[p.channel];
      const double y=double(int64_t(p.remote)-int64_t(origin->remote)-int64_t(p.local-origin->local)*50)-sy_[p.channel]/n_[p.channel];
      const double e=y-r.rate*x;squares+=e*e;}
    r.guard=std::max(50000.0/r.span_ms,3*std::sqrt(squares/(r.pairs-1)/xx));
    r.valid=std::isfinite(r.guard);return r;
  }
 public:
  void clear(uint32_t generation=0){count_=0;last_=refreshed_=0;generation_=generation;short_={};long_={};}
  bool observe(uint64_t local,uint64_t remote,uint64_t available,uint8_t channel,uint32_t generation){
    if(generation!=generation_)clear(generation);
    if(!local||available<local||remote>uint64_t(INT64_MAX)||channel>=37||local<=last_)return false;
    if(last_&&local-last_<Hz/4)return false;
    while(count_&&(local-points_[0].local>32*Hz||count_==160)){
      for(unsigned i=1;i<count_;++i){points_[i-1]=points_[i];}--count_;}
    points_[count_++]={local,remote,available,channel};last_=local;return true;
  }
  void refresh(uint64_t at){
    if(!at||(refreshed_&&at>=refreshed_&&at-refreshed_<Hz))return;
    short_=fit(at,16);long_=fit(at,32);refreshed_=at;
  }
  Choice choose(uint64_t at,double fast,double fast_guard)const{
    Choice c{fast,0,false};
    if(!refreshed_||at<refreshed_||at-refreshed_>2*Hz||!last_||at<last_||at-last_>2*Hz||
       !std::isfinite(fast)||!std::isfinite(fast_guard)||!short_.valid||!long_.valid||
       short_.guard>10||long_.guard>10||long_.guard>=fast_guard||std::abs(short_.rate-long_.rate)>5)return c;
    double values[3]{fast,short_.rate,long_.rate};std::sort(std::begin(values),std::end(values));
    if(std::abs(values[1]-fast)>15)return c;
    c.delta=std::clamp(values[1]-fast,-5.0,5.0);c.rate=fast+c.delta;c.selected=true;return c;
  }
  const Fit& fit16()const{return short_;}const Fit& fit32()const{return long_;}
  uint64_t refreshed20()const{return refreshed_;}uint32_t generation()const{return generation_;}
  unsigned points()const{return count_;}
  // Preserve delivered phase/calibration at entry. Only HOLD's rate changes.
  static bool reanchor(G1ClockPrediction& clock,uint64_t at,double rate){
    const auto value=clock.relative_at(at);if(value<=0||!std::isfinite(rate)||std::abs(rate)>50000)return false;
    const auto phase=clock.phase_remaining(at);clock.tick=at;clock.epoch=value-clock.receiver_phase_ns;
    clock.phase=phase;clock.rate=rate;clock.thermal={};return true;
  }
};
static_assert(sizeof(W12HoldoverRate)<=6400,"Bounded HOLD-only history and fit workspace");
namespace w12_hold_rate {
using PairSink=void(*)(uint64_t,uint64_t,uint64_t,uint8_t,uint32_t);
inline PairSink pair_sink=nullptr;
}
}

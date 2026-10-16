#pragma once
#include "ui.hpp"
#include <algorithm>
#include <cstring>
namespace sticks3 {
// These classes consume presentation evidence only. They cannot change the
// protocol clock, source leases, precision flags or radio permissions.
class PresentationFilter {
public:
 static constexpr uint64_t RecoveryTicks=2*HeldClock::Hz;
 HeldClock::State update(uint64_t tick,HeldClock::State raw,uint32_t root,uint32_t session) {
  if(!initialized_ || tick<last_ || root!=root_ || session!=session_){
   shown_=tick<last_?HeldClock::Fault:raw;initialized_=true;pending_=false;
  } else if(raw!=HeldClock::Live){shown_=raw;pending_=false;}
  else if(shown_!=HeldClock::Live){
   if(!pending_){pending_=true;since_=tick;}
   if(tick-since_>=RecoveryTicks){shown_=HeldClock::Live;pending_=false;}
  }
  last_=tick;root_=root;session_=session;return shown_;
 }
 HeldClock::State shown()const{return shown_;}
private:
 HeldClock::State shown_=HeldClock::Waiting;
 uint64_t last_=0,since_=0;uint32_t root_=0,session_=0;
 bool initialized_=false,pending_=false;
};
class MetricHistory {
public:
 static constexpr uint64_t PaceTicks=2*HeldClock::Hz,MaxAgeTicks=30*HeldClock::Hz;
 struct Value {uint32_t ns=UINT32_MAX,samples=0,age_ms=UINT32_MAX;};
 Value update(uint64_t tick,uint32_t root,uint32_t session,uint8_t percentile,
              uint32_t ns,uint32_t samples,uint64_t available) {
  if(tick<last_ || root!=root_ || session!=session_ || percentile!=percentile_){
   value_={};sample_tick_=shown_at_=0;
  }
  last_=tick;root_=root;session_=session;percentile_=percentile;
  if(ns!=UINT32_MAX && available && available<=tick && tick-available<MaxAgeTicks &&
     (value_.ns==UINT32_MAX || tick-shown_at_>=PaceTicks)){
   value_.ns=ns;value_.samples=samples;sample_tick_=available;shown_at_=tick;
  }
  if(!sample_tick_ || tick<sample_tick_ || tick-sample_tick_>=MaxAgeTicks)value_={};
  else value_.age_ms=uint32_t((tick-sample_tick_)/(HeldClock::Hz/1000));
  if(value_.ns==UINT32_MAX)value_.samples=samples;
  return value_;
 }
private:
 Value value_{};uint64_t last_=0,sample_tick_=0,shown_at_=0;
 uint32_t root_=0,session_=0;uint8_t percentile_=95;
};
// Ignore changing seconds/age in the transition key. Those are ordinary clock
// motion; page/state/source/metric availability or displayed value is an edge.
inline bool same_ui_transition(const View& a,const View& b,bool metric=true) {
 return a.page==b.page && a.presentation==b.presentation && a.time_valid==b.time_valid &&
  a.held==b.held && (a.page<2 || a.fit==b.fit) && a.root==b.root && a.session==b.session &&
  a.metric_history==b.metric_history && (!metric || a.history_ns==b.history_ns) &&
  a.percentile==b.percentile && !std::strcmp(a.header,b.header) && !std::strcmp(a.reason,b.reason);
}
inline uint64_t strip_digest(const uint8_t* data,unsigned size) {
 uint64_t hash=14695981039346656037ULL;
 for(unsigned i=0;i<size;++i){hash^=data[i];hash*=1099511628211ULL;}
 return hash;
}
}

// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "n30_timing_probe.hpp"
#include <cstring>
namespace n30::timing {
struct Sample {
 v12::periodic_rx::Stamp capture{};adv_rx::BleAnchor anchor{};
 v12::n30_probe::Pair pair{};uint64_t received_us=0;uint32_t number=0;int8_t rssi=127;bool matched=false;
};
struct Snapshot {
 uint64_t id=0,until_us=0;uint32_t every=0,seen=0,sampled=0,matched=0;
 uint32_t overwritten=0,pair_missing=0,oldest=0;uint8_t queued=0;bool active=false;
};
// The caller serializes this fixed RAM owner; no allocation, RF or I/O here.
class Recorder {
 static constexpr unsigned Capacity=16;
 Sample samples_[Capacity]{};Snapshot state_{};uint64_t last_us_=0;
 unsigned head_=0,count_=0;
 static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
 public:
 static constexpr uint32_t MaximumMs=600000;
 [[gnu::noinline]] bool start(uint64_t id,uint64_t now,uint32_t duration_ms,uint32_t every,uint64_t current=0){
  if(!current)current=now;
  expire(current);
  if(state_.active||!id||!now||duration_ms<1000||duration_ms>MaximumMs||!every||every>64||
     now>UINT64_MAX-uint64_t(duration_ms)*1000||current<now||current>=now+uint64_t(duration_ms)*1000)return false;
  for(auto& row:samples_)row={};
  state_={};head_=count_=0;state_.id=id;state_.until_us=now+uint64_t(duration_ms)*1000;
  state_.every=every;state_.active=true;last_us_=current;return true;
 }
 void stop(){state_.active=false;}
 void expire(uint64_t now){if(now<last_us_||now>=state_.until_us)state_.active=false;last_us_=now;}
 void capture(const v12::periodic_rx::Stamp& s,const adv_rx::BleAnchor& a,int8_t rssi,uint64_t now){
  expire(now);if(!state_.active)return;
  inc(state_.seen);if((state_.seen-1)%state_.every)return;
  if(count_==Capacity){head_=(head_+1)%Capacity;--count_;inc(state_.overwritten);}
  auto& row=samples_[(head_+count_)%Capacity];row={};row.capture=s;row.anchor=a;
  inc(state_.sampled);row.number=state_.sampled;row.received_us=now;row.rssi=rssi;++count_;
 }
 void pair(const v12::n30_probe::Pair& p,uint64_t now){
  expire(now);if(!state_.active)return;
  for(unsigned i=0;i<count_;++i){auto& row=samples_[(head_+i)%Capacity];
   const auto& c=row.capture;
   if(c.epoch==p.epoch&&c.tick20==p.local20&&c.event==p.event&&c.channel==p.channel){
    if(!row.matched){row.pair=p;row.matched=true;inc(state_.matched);}return;
   }
  }
  inc(state_.pair_missing); // Includes intentional decimation; never zero-fill.
 }
 Snapshot snapshot(uint64_t now){expire(now);auto s=state_;s.queued=uint8_t(count_);s.oldest=count_?samples_[head_].number:0;return s;}
 unsigned read(uint32_t after,Sample* out,unsigned capacity,uint64_t now){
  expire(now);unsigned n=0;
  for(unsigned i=0;i<count_&&n<capacity;++i){const auto& row=samples_[(head_+i)%Capacity];
   if(row.number<=after)continue;
   // Complete the original250ms association population before export. Reads
   // are non-consuming: a failed CRC response never advances the host cursor.
   if(!row.matched&&(now<row.received_us||now-row.received_us<v12::periodic_rx::MaxAgeUs))break;
   out[n++]=row;
  }
  return n;
 }
};
static_assert(sizeof(Recorder)<=4096,"N30 diagnostic ring exceeds4KiB budget");
}

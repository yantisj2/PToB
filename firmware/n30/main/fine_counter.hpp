// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace n30::fine {
// One arbitrary fixed origin. Coarse time checks lifecycle only, never phase.
struct Time {uint64_t raw80=0,tick20=0;uint32_t incarnation=0;};
class Counter {
 public:
 static constexpr uint64_t HalfWrapUs=26843545,MaxEventUs=250000,Base20=1000000;
 bool begin(uint32_t incarnation,uint32_t raw,uint64_t coarse,uint32_t hz){
  *this={};if(!incarnation||hz!=80000000)return false;
  incarnation_=incarnation;raw_=raw;extended_=origin_=raw;coarse_=rate_coarse_=progress_coarse_=coarse;rate_raw_=raw;live_=true;return true;
 }
 bool current(uint32_t incarnation,uint32_t raw,uint64_t coarse,Time& out){
  out={};if(!live_||incarnation!=incarnation_||coarse<coarse_||coarse-coarse_>=HalfWrapUs)return fail();
  const uint32_t delta=raw-raw_;if(delta>=0x80000000U||extended_>UINT64_MAX-delta)return fail();
  const auto next=extended_+delta;
  // Health guards only: stopped/stale/half-rate counters cannot create time.
  if(delta)progress_coarse_=coarse;
  else if(coarse-progress_coarse_>100)return fail();
  const auto elapsed=coarse-rate_coarse_;
  if(elapsed>=1000){
   const auto advance=next-rate_raw_;
   if(advance+8000<elapsed*70||advance>elapsed*90+8000)return fail();
   rate_coarse_=coarse;rate_raw_=next;
  }
  extended_=next;raw_=raw;coarse_=coarse;out={extended_,Base20+(extended_-origin_)/4,incarnation_};return true;
 }
 bool event(uint32_t incarnation,uint32_t raw,Time& out) const{
  out={};if(!live_||incarnation!=incarnation_)return false;
  const uint32_t age=raw_-raw;
  if(age>MaxEventUs*80||age>extended_||extended_-age<origin_)return false;
  const auto value=extended_-age;out={value,Base20+(value-origin_)/4,incarnation_};return true;
 }
 bool live() const{return live_;}
 private:
 bool fail(){live_=false;return false;}
 uint64_t extended_=0,origin_=0,coarse_=0,rate_coarse_=0,rate_raw_=0,progress_coarse_=0;uint32_t raw_=0,incarnation_=0;bool live_=false;
};
struct Edge {uint64_t raw80=0,tick20=0,seen_us=0;uint32_t number=0,raw32=0,incarnation=0;uint8_t kind=0,polarity=0;bool valid=false;};
class History {
 public:
 static constexpr unsigned Capacity=32;
 bool start(uint64_t id,uint64_t now,uint32_t ms,uint32_t every,bool persistent=false){
  if(active_||!id||ms<1000||ms>600000||!every||every>64||now>UINT64_MAX-uint64_t(ms)*1000)return false;
  *this={};id_=id;last_=now;until_=persistent?UINT64_MAX:now+uint64_t(ms)*1000;every_=every;active_=true;return true;
 }
 bool active(uint64_t now){if(now>=until_||now<last_)active_=false;last_=now;return active_;}
 void stop(){active_=false;}
 void append(Edge e){
  if(!active_)return;
  if(seen_==UINT32_MAX){active_=false;return;}
  auto& count=e.kind==0?rx_:tx_;if(count<UINT32_MAX)++count;
  ++seen_;
  if((seen_-1)%every_)return;
  if(sequence_==UINT32_MAX){active_=false;return;}
  e.number=++sequence_;rows_[(e.number-1)%Capacity]=e;if(e.number>Capacity&&overwritten_<UINT32_MAX)++overwritten_;
 }
 unsigned read(uint32_t after,Edge* out,unsigned capacity)const{
  unsigned n=0;const auto oldest=sequence_>=Capacity?sequence_-Capacity+1:1;
  const auto available=sequence_?sequence_-oldest+1:0;
  for(uint32_t i=0;i<available&&n<capacity;++i){const auto v=oldest+i;if(v>after)out[n++]=rows_[(v-1)%Capacity];}
  return n;
 }
 uint64_t id()const{return id_;}uint64_t until()const{return until_;}
 uint32_t seen()const{return seen_;}uint32_t rx()const{return rx_;}uint32_t tx()const{return tx_;}
 uint32_t sequence()const{return sequence_;}uint32_t overwritten()const{return overwritten_;}
 uint32_t every()const{return every_;}uint32_t oldest()const{return sequence_?sequence_>=Capacity?sequence_-Capacity+1:1:0;}
 private:
 Edge rows_[Capacity]{};uint64_t id_=0,until_=0,last_=0;
 uint32_t every_=1,seen_=0,sequence_=0,rx_=0,tx_=0,overwritten_=0;bool active_=false;
};
}

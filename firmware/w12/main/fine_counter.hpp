// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace w12::fine {
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
}

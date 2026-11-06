// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "ptob_gnss_reference.hpp"
#include <algorithm>
namespace n30 {
// Locked APB80/XTAL20 mapping. Intersect the actual enclosing brackets of
// preceding software captures instead of selecting one noisy midpoint. Every
// anchor keeps its original availability and two-second expiry. No BLE clock,
// RF bias correction, future anchor or statistical uncertainty enters this map.
class LoRaCounterMap {
 ptob_gnss::CounterMap guards_;
 std::array<ptob_gnss::CounterMap::Anchor,16> anchors_{};
 unsigned head_=0;
 public:
 using Anchor=ptob_gnss::CounterMap::Anchor;
 using Edge=ptob_gnss::CounterMap::Edge;
 bool observe(const Anchor& a){
  if(!guards_.observe(a))return false;
  anchors_[head_]=a;head_=(head_+1)%anchors_.size();return true;
 }
 bool release(uint64_t available){
  if(!guards_.release(available))return false;
  anchors_[(head_+anchors_.size()-1)%anchors_.size()].available=available;
  return true;
 }
 Edge convert(uint32_t capture,uint64_t consumed)const{
  const auto original=guards_.convert(capture,consumed);
  if(!original.valid)return {};
  uint64_t lower=0,upper=UINT64_MAX;unsigned count=0;
  for(const auto& a:anchors_){
   if(!a.before||consumed<a.available||consumed-a.available>40000000)continue;
   const uint32_t delta=capture-a.capture;
   if(delta>160000000u||a.after>(UINT64_MAX-delta-8)/4)continue;
   const uint64_t low=a.before*4+delta;
   const uint64_t high=a.after*4+delta;
   lower=std::max(lower,low>=8?low-8:0);
   upper=std::min(upper,high+8);++count;
  }
  if(!count||lower>upper)return {};
  const uint64_t midpoint=lower+(upper-lower)/2;
  const uint64_t tick=midpoint/4+(midpoint%4>=2);
  if(tick>consumed)return {};
  // Half the surviving interval plus original quantization allowances. This
  // remains a counter enclosure, never a measured RF or UTC error bound.
  const uint64_t width=upper-lower;
  const uint64_t bound=(width*25+3)/4+113;
  if(bound>UINT32_MAX)return {};
  return {tick,original.anchor_available,uint32_t(bound),true};
 }
 void reset(){guards_.reset();anchors_={};head_=0;}
 uint32_t accepted()const{return guards_.accepted;}
};
// Only a wide, otherwise coherent software bracket is recoverable. This does
// not install the rejected anchor or renew the qualified anchor's availability.
// CounterMap still enforces its original preceding-anchor/two-second limits.
inline bool recoverable_map_bracket(const ptob_gnss::CounterMap::Anchor& a,
 const ptob_gnss::CounterMap::Anchor& qualified){
 if(!a.before||!qualified.before||a.after<a.before||a.available<a.after||
    a.after-a.before<=400||a.before<=qualified.after||
    a.before-qualified.after>20000000||a.available<qualified.available||
    a.available-qualified.available>40000000||a.after-qualified.before>40000000)return false;
 const uint32_t dc=a.capture-qualified.capture;
 const uint64_t low=(a.before-qualified.after)*4,high=(a.after-qualified.before)*4;
 return uint64_t(dc)+8>=low&&uint64_t(dc)<=high+8;
}
}

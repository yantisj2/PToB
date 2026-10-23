// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "fine_counter.hpp"
namespace n30::fine {
// Continuous software-callback association. This does not establish that the
// peripheral did not coalesce physical transitions before interrupt service.
// The descriptor coordinate locates a search interval only; the chosen raw
// falling capture is the sole phase input to the fine clock.
class Receiver {
 public:
 static constexpr unsigned Capacity=256,Fences=16;
 static constexpr uint64_t EventAge80=Counter::MaxEventUs*80;
 enum class Result : uint8_t {Ready,Pending,Unsupported,Missing,Ambiguous,Fault};
 struct Match {Edge rise{},fall{};uint64_t lower80=0,upper80=0;uint32_t sequence=0;};
 struct Status {uint32_t callbacks=0,claimed=0,overwritten=0,late=0,fault=0;};
 private:
 struct Fence {uint64_t lower=0,upper=0;uint32_t sequence=0;};
 Edge rows_[Capacity]{};Fence fences_[Fences]{};
 uint64_t origin80_=0,from80_=0,overwritten_max80_=0,sealed_max80_=0,last_channel80_[2]{};
 uint32_t incarnation_=0,sequence_=0,count_=0,last_packet_epoch_=0,last_packet_sequence_=0;
 Status status_{};
 static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
 public:
 void begin(uint64_t origin80,uint64_t capture_from80,uint32_t incarnation){
  for(auto& e:rows_){e={};}
  for(auto& f:fences_){f={};}
  status_={};
  overwritten_max80_=sealed_max80_=0;last_channel80_[0]=last_channel80_[1]=0;
  sequence_=count_=last_packet_epoch_=last_packet_sequence_=0;
  origin80_=origin80;from80_=capture_from80;incarnation_=incarnation;
  if(!incarnation||capture_from80<origin80)status_.fault=1;
 }
 void observe(const Edge& e){
  inc(status_.callbacks);
  if(!e.valid||e.kind>1||e.polarity>1||e.raw32!=uint32_t(e.raw80)||e.incarnation!=incarnation_||!e.number||
     (sequence_&&e.number!=sequence_+1)||e.raw80<origin80_||
     (last_channel80_[e.kind]&&e.raw80<last_channel80_[e.kind])){status_.fault|=1;return;}
  // The usual callback is newer than all accepted intervals: constant work.
  if(e.raw80<=sealed_max80_)for(const auto& f:fences_){
   if(f.sequence&&e.number>f.sequence&&e.raw80>=f.lower&&e.raw80<=f.upper){inc(status_.late);status_.fault|=2;}
  }
  auto& row=rows_[(e.number-1)%Capacity];
  if(count_==Capacity){if(row.raw80>overwritten_max80_)overwritten_max80_=row.raw80;inc(status_.overwritten);}else ++count_;
  row=e;sequence_=e.number;last_channel80_[e.kind]=e.raw80;
 }
 Result claim(uint64_t descriptor20,uint32_t enclosure_ns,uint8_t phy,uint8_t pdu_length,uint8_t ext_header,
              uint32_t packet_epoch,uint32_t packet_sequence,uint64_t current80,uint32_t pending,Match& out){
  out={};if(status_.fault||!incarnation_)return Result::Fault;
  if(phy!=0||pdu_length!=191||ext_header!=0||!packet_epoch||packet_epoch<last_packet_epoch_||!packet_sequence||
     (packet_epoch==last_packet_epoch_&&packet_sequence<=last_packet_sequence_)||
     descriptor20<Counter::Base20||enclosure_ns==UINT32_MAX||enclosure_ns>1000000)return Result::Unsupported;
  const auto d=descriptor20-Counter::Base20;
  if(d>(UINT64_MAX-origin80_)/4)return Result::Fault;
  const auto center=origin80_+d*4;
  const uint64_t radius=UINT64_C(3100)*80+uint64_t(enclosure_ns)*80/1000+80;
  if(center<radius||center-radius<from80_||center>UINT64_MAX-radius)return Result::Missing;
  const auto lower=center-radius,upper=center+radius;
  if(current80<=upper||pending)return Result::Pending;
  if(current80-upper>EventAge80||lower<=overwritten_max80_)return Result::Missing;
  unsigned rising=0,falling=0;uint32_t previous=0;
  const auto first=sequence_-count_+1;
  for(uint32_t i=0;i<count_;++i){const auto& e=rows_[(first+i-1)%Capacity];
   if(e.number!=first+i||(previous&&e.number!=previous+1))return Result::Missing;
   previous=e.number;
   if(e.kind||e.raw80<lower||e.raw80>upper)continue;
   if(e.polarity==0){++rising;out.rise=e;}else if(e.polarity==1){++falling;out.fall=e;}else return Result::Fault;
  }
  if(!rising||!falling)return Result::Missing;
  if(rising!=1||falling!=1||out.rise.raw80>=out.fall.raw80)return Result::Ambiguous;
  // No fence may be reused while an event of that age could still extend.
  Fence* free=nullptr;
  for(auto& f:fences_){
   if(f.sequence&&current80>f.upper&&current80-f.upper>EventAge80)f={};
   if(f.sequence&&lower<=f.upper&&upper>=f.lower)return Result::Ambiguous;
   if(!f.sequence&&!free)free=&f;
  }
  if(!free)return Result::Missing;
  *free={lower,upper,sequence_};if(upper>sealed_max80_)sealed_max80_=upper;
  last_packet_epoch_=packet_epoch;last_packet_sequence_=packet_sequence;inc(status_.claimed);
  out.lower80=lower;out.upper80=upper;out.sequence=sequence_;return Result::Ready;
 }
 // 50ns delivered coordinate, ties rounded upward. The phase convention is
 // explicit and separately calibrated; neither it nor the counter resolution
 // is a measured RF endpoint bound. Translation is applied before rounding.
 bool coordinate(uint64_t raw80,uint64_t translation80,uint64_t& tick20)const{
  tick20=0;if(status_.fault||raw80<origin80_||raw80-origin80_<translation80)return false;
  const auto delta=raw80-origin80_-translation80;
  if(delta>UINT64_MAX-2)return false;
  tick20=Counter::Base20+(delta+2)/4;return true;
 }
 Status status()const{return status_;}
 // Triangle enclosure for source-lifetime/coarse permission arithmetic only.
 // The old software association bound follows the actual coordinate change;
 // this is neither a marker RF bound nor a sample in the causal RMS fit.
 static bool lifetime_guard(uint64_t old20,uint64_t fine20,uint32_t old_ns,uint32_t& guard_ns){
  guard_ns=UINT32_MAX;if(old_ns>UINT32_MAX-25)return false;
  const auto delta=old20>fine20?old20-fine20:fine20-old20;
  if(delta>(UINT32_MAX-uint64_t(old_ns)-25)/50)return false;
  guard_ns=uint32_t(uint64_t(old_ns)+delta*50+25);return true;
 }
 bool pending(uint64_t raw80){
  if(raw80<=sealed_max80_)for(const auto& f:fences_)
   if(f.sequence&&raw80>=f.lower&&raw80<=f.upper){status_.fault|=2;return false;}
  return !status_.fault;
 }
};
static_assert(sizeof(Receiver)<13312,"Continuous fine callback history exceeds13KiB");
}

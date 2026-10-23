// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "fine_counter.hpp"
namespace n30::fine {
// Software callback completeness only. A single capture register cannot prove
// that the physical input did not coalesce before interrupt service.
enum WindowFault : uint32_t { NoPrehistory=1,CapacityExceeded=2,InvalidEdge=4,
 DomainChanged=8,ChannelOrder=16,PendingCapture=32,OwnerFault=64,Withdrawn=128 };
struct Bridge {uint64_t before80=0,gpt20=0,after80=0,origin80=0,lease=0,until_us=0;uint32_t incarnation=0,hz=0;};
struct WindowStatus {
 uint64_t lease=0,origin80=0,lower80=0,upper80=0,seal80=0,until_us=0,capture_from80=0;
 uint64_t predecessor80[2]{},successor80[2]{};
 uint32_t id=0,incarnation=0,count=0,fault=0,begin_sequence=0,seal_sequence=0;
 uint32_t oldest=0,overwritten=0,pending=0,irq=0,channels=0;
 uint32_t predecessor[2]{},successor[2]{},late_mask=0,late_sequence[8]{};
 uint32_t late_words[4]{},late_count=0,late_callbacks=0;
 uint64_t current80=0;uint32_t current_incarnation=0,hz=0,timeouts=0,invalid=0,inactive=0;int error=0;
 bool collecting=false,sealed=false,from_origin=false,active=false,contained=false;
};
class Window {
 public:
 static constexpr unsigned Capacity=64,Maximum=128;
 struct Late {uint32_t id=0,sequence=0;};
 private:
 Edge edges_[Capacity]{};WindowStatus s_{};
 struct Fence {uint64_t lo=0,hi=0;uint32_t id=0,inc=0,sequence=0;};Fence retained_[Maximum]{};
 uint32_t late_sequence_[Maximum]{};uint64_t sealed_max80_=0;
 Edge scratch_{};uint32_t last_sequence_=0;uint64_t channel_last_[2]{},capture_from80_=0;
 static void inc(uint32_t& x){if(x!=UINT32_MAX)++x;}
 void append(const Edge& e){
  if(e.raw80<s_.lower80){if(e.raw80>=s_.predecessor80[e.kind]){s_.predecessor[e.kind]=e.number;s_.predecessor80[e.kind]=e.raw80;}return;}
  if(e.raw80>s_.upper80){if(!s_.successor[e.kind]||e.raw80<s_.successor80[e.kind]){s_.successor[e.kind]=e.number;s_.successor80[e.kind]=e.raw80;}return;}
  if(s_.count==Capacity){s_.fault|=CapacityExceeded;return;}edges_[s_.count++]=e;
 }
 public:
 void reset(const Bridge& b){s_={};for(auto& f:retained_)f={};for(auto& n:late_sequence_)n=0;sealed_max80_=0;last_sequence_=0;channel_last_[0]=channel_last_[1]=0;capture_from80_=b.after80;
  s_.lease=b.lease;s_.incarnation=b.incarnation;s_.origin80=b.origin80;s_.until_us=b.until_us;}
 bool begin(uint32_t id,uint64_t lower,uint64_t upper,const History& history,uint32_t incarnation){
  if(!id||id>Maximum||id!=s_.id+1||s_.collecting||lower>upper||lower<s_.origin80||incarnation!=s_.incarnation||retained_[id-1].id)return false;
  const auto old=s_;
  const auto lease=s_.lease,origin=s_.origin80,until=s_.until_us;const auto inc=s_.incarnation;
  s_={};s_.lease=lease;s_.origin80=origin;s_.until_us=until;s_.incarnation=inc;s_.id=id;s_.lower80=lower;s_.upper80=upper;s_.collecting=true;s_.capture_from80=capture_from80_;retain_late(old);
  if(lower<capture_from80_)s_.fault|=NoPrehistory;
  s_.oldest=history.oldest();s_.overwritten=history.overwritten();s_.begin_sequence=history.sequence();s_.from_origin=s_.oldest<=1;
  last_sequence_=0;channel_last_[0]=channel_last_[1]=0;
  // Fixed32 bounded backfill; callback sequence is NOT raw-time order.
  uint32_t cursor=0;
  for(unsigned i=0;i<History::Capacity&&history.read(cursor,&scratch_,1);++i){
   cursor=scratch_.number;observe(scratch_);
  }
  if(!s_.from_origin&&(!s_.predecessor[0]||!s_.predecessor[1]))s_.fault|=NoPrehistory;
  return true;
 }
 bool skip(uint32_t id){
  if(!id||id>Maximum||id!=s_.id+1||s_.collecting)return false;
  const auto old=s_;s_={};s_.lease=old.lease;s_.origin80=old.origin80;s_.until_us=old.until_us;s_.incarnation=old.incarnation;
  s_.capture_from80=capture_from80_;retain_late(old);
  s_.id=id;s_.sealed=true;s_.fault=OwnerFault;return true;
 }
 void arm(uint64_t at){capture_from80_=at;}
 void observe(const Edge& e){
  // Sticky invalidation survives ACK/new slots. Never mutate a frozen edge.
  // Ordinary future edges cannot invalidate any sealed interval. Keep this
  // ISR path independent of the number of retained validation windows.
  if(e.valid&&e.raw80<=sealed_max80_)for(unsigned i=0;i<Maximum;++i){const auto& f=retained_[i];
   if(f.id&&e.number>f.sequence&&e.incarnation==f.inc&&e.raw80>=f.lo&&e.raw80<=f.hi){
    inc(s_.late_callbacks);const auto bit=UINT32_C(1)<<(i%32);
    if(!(s_.late_words[i/32]&bit)){
     inc(s_.late_count);s_.late_words[i/32]|=bit;late_sequence_[i]=e.number;
     if(i<32){s_.late_mask|=bit;}if(i<8){s_.late_sequence[i]=e.number;}
    }
   }
  }
  if(!s_.collecting)return;
  if(e.incarnation!=s_.incarnation){s_.fault|=DomainChanged;return;}
  if(!e.valid||e.kind>1){s_.fault|=InvalidEdge;return;}
  if(last_sequence_&&e.number!=last_sequence_+1)s_.fault|=NoPrehistory;
  last_sequence_=e.number;
  if(channel_last_[e.kind]&&e.raw80<channel_last_[e.kind])s_.fault|=ChannelOrder;
  channel_last_[e.kind]=e.raw80;append(e);
 }
 void seal(uint64_t current80,uint32_t sequence,uint32_t raw,uint32_t irq,uint32_t channels,uint32_t fault){
  if(!s_.collecting||current80<=s_.upper80)return;
  s_.seal80=current80;s_.seal_sequence=sequence;s_.pending=raw;s_.irq=irq;s_.channels=channels;s_.fault|=fault;
  if(raw)s_.fault|=PendingCapture;
  s_.collecting=false;s_.sealed=true;retained_[s_.id-1]={s_.lower80,s_.upper80,s_.id,s_.incarnation,sequence};if(s_.upper80>sealed_max80_)sealed_max80_=s_.upper80;
 }
 void withdraw(){if(s_.collecting){s_.fault|=Withdrawn;s_.collecting=false;s_.sealed=true;retained_[s_.id-1]={s_.lower80,s_.upper80,s_.id,s_.incarnation,last_sequence_};if(s_.upper80>sealed_max80_)sealed_max80_=s_.upper80;}}
 WindowStatus status()const{return s_;}
 unsigned read(uint32_t after,Edge* out,unsigned capacity)const{unsigned n=0;if(!s_.sealed)return 0;
  for(unsigned i=0;i<s_.count&&n<capacity;++i){if(edges_[i].number>after){out[n++]=edges_[i];}}
  return n;}
 unsigned late(uint32_t after,Late* out,unsigned capacity)const{unsigned n=0;
  for(unsigned i=after;i<Maximum&&n<capacity;++i)if(late_sequence_[i])out[n++]={i+1,late_sequence_[i]};
  return n;}
 private:
 void retain_late(const WindowStatus& old){s_.late_mask=old.late_mask;s_.late_count=old.late_count;s_.late_callbacks=old.late_callbacks;
  for(unsigned i=0;i<8;++i){s_.late_sequence[i]=old.late_sequence[i];}for(unsigned i=0;i<4;++i){s_.late_words[i]=old.late_words[i];}}
};
static_assert(sizeof(Window)<9216,"Fixed callback window and128 retained fences exceed9KiB");
// Search only. Never use this mapping as estimator phase. GPTimer and MCPWM
// may differ in rate; <=250ms age plus100us fixed guard is explicit enclosure.
inline bool search(const Bridge& b,uint64_t descriptor20,uint32_t old_enclosure_ns,uint64_t& lower,uint64_t& upper){
 lower=upper=0;
 if(b.hz!=80000000||!b.incarnation||!b.lease||b.after80<b.before80||b.after80-b.before80>80000||!descriptor20||descriptor20>b.gpt20||b.gpt20-descriptor20>5000000||old_enclosure_ns==UINT32_MAX||old_enclosure_ns>1000000)return false;
 const auto age=b.gpt20-descriptor20;if(age>UINT64_MAX/4||age*4>b.before80)return false;
 const uint64_t radius=uint64_t(3000+100)*80+uint64_t(old_enclosure_ns)*80/1000+80;
 const auto lo=b.before80-age*4,hi=b.after80-age*4;
 if(lo<radius||lo-radius<b.origin80||hi>UINT64_MAX-radius)return false;
 lower=lo-radius;upper=hi+radius;return true;
}
}

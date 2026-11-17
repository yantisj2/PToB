// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>
namespace watch {
// Single UI owner, monotonic boot time only. No flash writes or timing authority.
// One active minute plus 24 hours of completed minutes, allocated in PSRAM.
class BatteryHistory {
public:
 static constexpr uint64_t Minute=60000000,Hour=60*Minute,Retention=24*Hour;
 static constexpr unsigned Capacity=1441,Columns=30;
 struct Column {uint64_t sampled_us=0;uint32_t awake_us=0,observed_us=0;int16_t level=-1;bool usb=false;};
 static_assert(Retention/Columns<=UINT32_MAX,"A 48-minute column must fit microsecond counters");
 struct Window {Column columns[Columns]{};uint64_t awake_us=0,observed_us=0,span_us=Hour;};
private:
 struct MinuteRecord {uint64_t tag=UINT64_MAX;uint32_t awake_us=0,observed_us=0,sample_offset=0;int16_t level=-1;bool usb=false;};
 MinuteRecord records[Capacity]{};
 mutable Window scratch{}; // Same PSRAM allocation, sole UI owner.
 uint64_t last=0;bool started=false,on=false;
 MinuteRecord& slot(uint64_t minute){auto& r=records[minute%Capacity];if(r.tag!=minute){r={};r.tag=minute;}return r;}
public:
 void observe(uint64_t now,bool display_on){
  if(started&&now<last){for(auto& r:records)r={};started=false;}
  if(started){
   // The panel retains its state during worker stalls. Bound work to retention.
   uint64_t at=std::max(last,now>Retention?now-Retention:0);
   while(at<now){const auto minute=at/Minute,end=std::min(now,(minute+1)*Minute);auto& r=slot(minute);
    const auto dt=uint32_t(end-at);r.observed_us+=dt;if(on)r.awake_us+=dt;at=end;}
  }
  started=true;last=now;on=display_on;
 }
 void sample(uint64_t now,int percentage,bool usb){
  // observe() must precede a sample; reject samples from another boot/context.
  if(!started||now!=last)return;
  auto& r=slot(now/Minute);r.level=percentage>=0&&percentage<=100?percentage:-1;r.usb=usb;r.sample_offset=uint32_t(now%Minute);
 }
 void window(uint64_t now,uint8_t hours,Window& w)const{
  w={};w.span_us=uint64_t(hours==6||hours==12||hours==24?hours:1)*Hour;
  // Each column spans exactly 1/30 of the chosen scale, including before boot.
  const uint64_t edge=std::max(now,w.span_us),padding=edge-now,start=edge-w.span_us;
  const uint64_t width=w.span_us/Columns;
  for(const auto& r:records){
   if(r.tag==UINT64_MAX)continue;
   const uint64_t begin=r.tag*Minute+padding,end=begin+Minute;
   if(end<=start||begin>edge)continue;
   const uint64_t sampled=begin+r.sample_offset;
   if(r.level>=0&&sampled>=start&&sampled<=edge){auto& c=w.columns[std::min(Columns-1,unsigned((sampled-start)/width))];if(sampled>=c.sampled_us){c.level=r.level;c.sampled_us=sampled;}c.usb|=r.usb;}
   const auto lo=std::max(begin,start),hi=std::min(end,edge);
   if(hi<=lo)continue;
   for(unsigned i=unsigned((lo-start)/width);i<Columns&&start+i*width<hi;++i){
    const auto overlap=std::min(hi,start+(i+1)*width)-std::max(lo,start+i*width);
    // A completed minute is exact. The active minute is clipped to elapsed time.
    const auto elapsed=std::min(Minute,now>=r.tag*Minute?now-r.tag*Minute:0);
    if(!elapsed)continue;
    auto& c=w.columns[i];c.observed_us+=uint64_t(r.observed_us)*overlap/elapsed;
    c.awake_us+=uint64_t(r.awake_us)*overlap/elapsed;
   }
  }
  for(const auto& c:w.columns){w.awake_us+=c.awake_us;w.observed_us+=c.observed_us;}
 }
 Window window(uint64_t now,uint8_t hours)const{Window w;window(now,hours,w);return w;}
 const Window& snapshot(uint64_t now,uint8_t hours)const{window(now,hours,scratch);return scratch;}
};
static_assert(sizeof(BatteryHistory)<=40*1024,"Battery history must fit its PSRAM budget");
}

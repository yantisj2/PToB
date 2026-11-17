// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cmath>
#include <iterator>
#include <cstdint>
namespace watch {
enum class ShadowStage:uint8_t {Off,Starting,Searching,Fix,WaitPps,WaitMesh,Settling,Measuring,Lost,Fault};
struct Satellite {uint16_t id=0,azimuth=0;uint8_t elevation=0,cn0=0;char constellation='G';bool position_known=true;};
struct ShadowView {
 ShadowStage stage=ShadowStage::Off;
 Satellite satellites[16]{};
 uint64_t sampled_us=0,last_pps_us=0,elapsed_us=0;
 int64_t mean_tenth_ns=0;
 uint32_t samples=0,p99_ns=0,rms_ns=0,skipped=0,missing=0,settling_ms=0,edges=0;
 uint16_t in_view=0,used=0;uint8_t satellite_count=0,fix_dimension=0;
 bool powered=false,fix=false,pps=false,valid=false,approximate=false;
};
// Read-only, phase-blind ten-second startup; no phase trimming or outlier removal.
// Same magnitude histogram/interpolation as T096, including marked coarse tails.
// Every source/scope change starts a disjoint population. No clock writes.
class ShadowStatistics {
 static constexpr unsigned Exact=1024,Bins=Exact+19;
 uint32_t histogram_[Bins]{};
 uint64_t first_=0,last_=0,started_=0,scope_=0;
 uint32_t cursor_=0,count_=0,skipped_=0,missing_=0;
 int64_t sum_=0;long double squares_=0;
 bool tracking_=false,settled_=false,active_=false;
 static unsigned bin(uint32_t ns){unsigned b=Exact;uint32_t edge=2048;if(ns<Exact)return ns;while(ns>=edge&&b+1<Bins){++b;edge*=2;}return b;}
 static uint32_t value(unsigned b){if(b<Exact)return b;const uint32_t lo=1024u<<(b-Exact);return lo+lo/2-1;}
 uint32_t rank(uint32_t r,bool& approximate)const{uint64_t n=0;for(unsigned i=0;i<Bins;++i)if((n+=histogram_[i])>r){approximate|=i>=Exact;return value(i);}return 0;}
 void population(){std::fill(std::begin(histogram_),std::end(histogram_),0);first_=last_=started_=0;count_=0;sum_=0;squares_=0;tracking_=settled_=active_=false;}
 public:
 void reset(){population();scope_=0;cursor_=skipped_=missing_=0;}
 void withdraw(){population();}
 // Permission can lapse between physical pulses while the authenticated clock
 // context stays unchanged. Retain its scored population, but never present or
 // extend permission from it. Only a newly eligible physical PPS can resume it.
 void pause(){active_=false;}
 void edge(uint64_t at,uint32_t sequence,uint64_t scope,bool usable,int64_t phase_ns){
  if(sequence<=cursor_)return;
  const auto gap=sequence-cursor_-1;cursor_=sequence;
  missing_=uint32_t(std::min<uint64_t>(UINT32_MAX,uint64_t(missing_)+gap));
  if(gap||scope_!=scope||(tracking_&&(at<=last_||at-last_>1500000)))population();
  scope_=scope;
  if(!at||!scope||phase_ns< -500000000||phase_ns>500000000){if(skipped_<UINT32_MAX)++skipped_;population();return;}
  last_=at;
  if(!usable){if(skipped_<UINT32_MAX)++skipped_;pause();return;}
  active_=true;
  if(!tracking_){tracking_=true;first_=at;}
  if(!settled_){if(at-first_<10000000)return;settled_=true;started_=at;return;}
  if(count_==UINT32_MAX)return;
  const uint32_t magnitude=uint32_t(phase_ns<0?-phase_ns:phase_ns);
  ++histogram_[bin(magnitude)];++count_;sum_+=phase_ns;squares_+=static_cast<long double>(phase_ns)*phase_ns;
 }
 void apply(ShadowView& v,uint64_t now)const{
  v.samples=count_;v.skipped=skipped_;v.missing=missing_;
  const bool recent=tracking_&&now>=last_&&now-last_<=1500000;
  v.settling_ms=recent?uint32_t(std::min<uint64_t>(10000,(last_-first_)/1000)):0;
  v.elapsed_us=recent&&settled_&&now>=started_?now-started_:0;
  v.valid=recent&&active_&&settled_&&count_;
  if(!v.valid)return;
  const auto q=sum_/count_,r=sum_%count_;
  v.mean_tenth_ns=q*10+(r*10+(r<0?-int64_t(count_)/2:int64_t(count_)/2))/count_;
  v.rms_ns=uint32_t(std::ceil(std::sqrt(squares_/count_)));
  const uint64_t scaled=uint64_t(count_-1)*99;const auto fraction=scaled%100;
  bool approximate=false;const auto lo=rank(uint32_t(scaled/100),approximate),hi=fraction?rank(uint32_t(scaled/100+1),approximate):lo;
  v.p99_ns=uint32_t((uint64_t(lo)*(100-fraction)+uint64_t(hi)*fraction+50)/100);v.approximate=approximate;
 }
 bool settled()const{return settled_;}
 bool tracking()const{return tracking_&&active_;}
};
static_assert(sizeof(ShadowStatistics)<4352,"Bounded shadow histogram");
}

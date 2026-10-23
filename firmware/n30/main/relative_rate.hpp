// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

struct N30RelativeRateView {
 uint64_t root=0,boot=0,sampled_us=0,until_us=0;
 uint32_t source_epoch=0,span_ms=0,samples=0;int32_t ppb=0;bool valid=false;
};
namespace n30 {
// A display-only trend of signed, frozen PRE-UPDATE prediction errors.
// Positive means the delivered prediction is gaining against the GM.
// ns/second has the numerical unit ppb. This is neither oscillator trim nor
// an independent physical frequency measurement. Steering and RMS ignore it.
struct RelativeRatePoint {
 uint64_t root=0,boot=0,local20=0,available20=0,remote_ns=0;
 int64_t predicted_ns=0;uint32_t source_epoch=0;bool causal=false;
};
class RelativeRateEstimator {
 struct Bin {uint64_t second=0,first20=0;uint32_t count=0;double t=0,tt=0,e=0,te=0;};
 std::array<Bin,16> bins_{};
 uint64_t root_=0,boot_=0,last_us_=0,newest20_=0;uint32_t epoch_=0;
public:
 static constexpr uint64_t ExpiryUs=500000,Hz=20000000,MinimumSpan20=8*Hz;
 void reset(){bins_={};root_=boot_=last_us_=newest20_=0;epoch_=0;}
 N30RelativeRateView observe(const RelativeRatePoint& p,uint64_t now){
  const bool usable=p.root&&p.boot&&p.source_epoch&&p.causal&&p.local20&&p.available20>=p.local20&&p.predicted_ns>0&&p.remote_ns<=uint64_t(INT64_MAX);
  if(!usable){reset();return {};}
  if(root_!=p.root||boot_!=p.boot||epoch_!=p.source_epoch||now<last_us_||
    (last_us_&&now-last_us_>=ExpiryUs)||
    (newest20_>p.local20&&newest20_-p.local20>=Hz/2))reset();
  root_=p.root;boot_=p.boot;epoch_=p.source_epoch;last_us_=now;newest20_=std::max(newest20_,p.local20);
  const int64_t error=p.predicted_ns-int64_t(p.remote_ns);
  if(error>1000000000||error< -1000000000){reset();return {};}
  const auto sec=p.local20/Hz;auto& b=bins_[sec%bins_.size()];
  if(!b.count||b.second!=sec)b={sec,p.local20};
  if(b.count==UINT32_MAX){reset();return {};}
  const double t=double(p.local20%Hz)/Hz;
  ++b.count;b.first20=std::min(b.first20,p.local20);b.t+=t;b.tt+=t*t;b.e+=double(error);b.te+=t*double(error);
  N30RelativeRateView v;v.root=root_;v.boot=boot_;v.source_epoch=epoch_;v.sampled_us=now;
  v.until_us=now<=UINT64_MAX-ExpiryUs?now+ExpiryUs:now;
  const uint64_t newest_sec=newest20_/Hz,base=newest_sec>=15?newest_sec-15:0;
  uint64_t first=newest20_;double n=0,sx=0,sxx=0,sy=0,sxy=0;unsigned occupied=0;
  for(const auto& x:bins_){
   if(!x.count||x.second<base||x.second>newest_sec)continue;
   const double dt=double(x.second-base);n+=x.count;sx+=dt*x.count+x.t;
   sxx+=dt*dt*x.count+2*dt*x.t+x.tt;sy+=x.e;sxy+=dt*x.e+x.te;
   first=std::min(first,x.first20);++occupied;
  }
  v.samples=uint32_t(std::min(n,double(UINT32_MAX)));
  v.span_ms=uint32_t(std::min<uint64_t>(UINT32_MAX,(newest20_-first)/20000));
  if(n<64||occupied<9||newest20_-first<MinimumSpan20)return v;
  const double denominator=sxx-sx*sx/n;
  const double rate=denominator>0?(sxy-sx*sy/n)/denominator:INFINITY;
  if(!std::isfinite(rate)||std::abs(rate)>50000)return v;
  v.ppb=int32_t(std::llround(rate));v.valid=true;return v;
 }
};
}
namespace v12::n30_probe {struct Pair;}
void n30_relative_rate_pair(const v12::n30_probe::Pair&,uint64_t now_us);
N30RelativeRateView n30_relative_rate_snapshot(uint64_t now_us);

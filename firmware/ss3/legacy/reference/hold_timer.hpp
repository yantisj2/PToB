#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <type_traits>
#include <cstdio>
#include "clock_statistics.hpp"
#include "clock_prediction.hpp"

// Retrospective frozen forecasts: for an endpoint T and horizon H, use only
// the immutable clock state that was actually available at or before T-H.
// Delayed D2 cannot train that state. Trials within each bucket are disjoint;
// a missing snapshot is missing coverage, never an interpolated reference.
template<bool WithIdentity=false> class HoldTimerBase {
public:
 static constexpr uint64_t Hz=20000000;
 static constexpr unsigned Buckets=8,Capacity=100,Minimum=50;
 static constexpr uint64_t WindowTicks=Hz*300;
 static constexpr unsigned ModelCapacity=128;
 static constexpr uint64_t FreshTicks=Hz*120;
 inline static constexpr uint32_t Horizons[Buckets]={100,500,1000,2000,3000,5000,10000,20000};
 // P95 keeps the original five-minute view. P99 retains up to 100 trials
 // long enough to collect them at a sparse, <15s reference cadence, while
 // still requiring recent endpoints and the current phase/rate gates.
 static constexpr uint32_t p99_window_seconds(unsigned i){return Capacity*(15+(Horizons[i]+999)/1000);}
 struct Summary {uint32_t samples=0,p95_ns=UINT32_MAX,missed=0,failures=0,expired=0,p99_ns=UINT32_MAX,p99_samples=0;};
 static constexpr unsigned minimum(unsigned percentile){return percentile==99?100:Minimum;}
 void reset(){for(auto &b:bins_)b=Bin{};experiments_=failures_=last_error_=0;last_available_=now_=last_endpoint_=0;model_next_=model_count_=0;}
 void age(uint64_t now){
  for(unsigned i=0;i<Buckets;++i){auto& b=bins_[i];
   while(b.count){const auto oldest=(b.next+Capacity-b.count)%Capacity;
    if(now<b.times[oldest] || now-b.times[oldest]<=Hz*p99_window_seconds(i))break;
    --b.count;++b.expired;b.cache_valid=false;
   }
  }
  now_=std::max(now_,now);
 }
 void score(uint64_t tick,int64_t reference,uint64_t identity=0){
  // References can arrive late, but never let duplicate or reordered D1
  // timestamps inflate a population or underflow an age subtraction.
  if(!tick || tick<=last_endpoint_)return;
  last_endpoint_=tick;
  for(unsigned i=0;i<Buckets;++i){
   auto &b=bins_[i];
   const uint64_t horizon=uint64_t(Horizons[i])*(Hz/1000);
   if(tick<horizon || tick-horizon<b.scored)continue;
   const uint64_t frozen=tick-horizon;
   const SnapshotType* snapshot=nullptr;
   for(unsigned j=0;j<model_count_;++j){
    const auto& s=models_[(model_next_+ModelCapacity-1-j)%ModelCapacity];
    if(s.available<=frozen){snapshot=&s;break;}
   }
   // Source/session/channel changes cannot score an unrelated frozen output.
   // No retrospective launch from newly learned data. An old model is also
   // unavailable once the reference continuity gate (15s) has expired.
   bool same_identity=true;
   if constexpr(WithIdentity) same_identity=snapshot && snapshot->identity==identity;
   if(!snapshot || !same_identity || frozen<snapshot->model.tick || frozen-snapshot->model.tick>Hz*15){++b.missed;b.cache_valid=false;continue;}
   const uint64_t error=uint64_t(std::llabs(reference-snapshot->model.at(tick)));
   // A newly completed trial must not revive a stale 100-sample population.
   if(b.scored && tick-b.scored>FreshTicks){b.count=b.next=0;}
   last_error_=uint32_t(std::min<uint64_t>(UINT32_MAX,error));
   b.errors[b.next]=last_error_;b.times[b.next]=tick;b.next=(b.next+1)%Capacity;
   if(b.count<Capacity)++b.count;
   ++experiments_;if(error+50>=1000){++failures_;++b.failures;}
   b.scored=tick;b.cache_valid=false;
  }
 }
 void start(uint64_t available,const ClockPrediction& model,uint32_t precision,uint64_t identity=0){
  age(available);
  // Continue measuring during poor precision so the experiment can observe
  // recovery. Qualification gates belong in budget(), not in training.
  if(precision==UINT32_MAX || !model.tick || available<model.tick)return;
  if(available<=last_available_)return;
  last_available_=available;
  models_[model_next_].available=available;models_[model_next_].model=model;
  if constexpr(WithIdentity){models_[model_next_].identity=identity;}
  model_next_=(model_next_+1)%ModelCapacity;
  if(model_count_<ModelCapacity)++model_count_;
 }
 // Percentiles already contain phase and endpoint noise. Do not add three
 // RMS again or convert that timestamp noise into a rate.
 uint32_t tested_budget(uint64_t now,uint32_t precision,double current_guard,
                 uint64_t output_error=0,uint64_t age_ticks=0,unsigned percentile=95) const {
  if(precision>=1000 || output_error>=1000-precision || !std::isfinite(current_guard))return 0;
  uint32_t best=0;
  for(unsigned i=0;i<Buckets;++i){const auto& b=bins_[i];const auto s=summary(i,now);
   if((percentile==99?s.p99_samples:s.samples)<minimum(percentile) || !b.scored || now<b.scored || now-b.scored>FreshTicks)continue;
   // A passing longer endpoint cannot conceal a known shorter-horizon failure.
   if(uint64_t(percentile==99?s.p99_ns:s.p95_ns)+50+output_error>=1000)break;
   // Separate current model guard: historical trials cannot hide a newly
   // unapplied correction. No empirical percentile-to-rate conversion.
   if(precision+output_error+std::max(0.0,current_guard)*Horizons[i]/1000>=1000)continue;
   best=Horizons[i];
  }
  const uint64_t age_ms=age_ticks/(Hz/1000);
  const auto result=best>age_ms?uint32_t(best-age_ms):0;
  return percentile==99?std::min(result,tested_budget(now,precision,current_guard,output_error,age_ticks,95)):result;
 }
 // Estimate the 1us crossing of the recent fixed-horizon p95 envelope.
 // Interpolation is a model between measured endpoints, not an independently
 // certified millisecond trial. Beyond the measured history, project using
 // the larger of current rate uncertainty and observed p95-envelope growth.
 uint32_t budget(uint64_t now,uint32_t precision,double guard,
                 uint64_t output_error=0,uint64_t age_ticks=0,unsigned percentile=95) const {
  if(precision>=1000 || output_error>=1000-precision || !std::isfinite(guard))return 0;
  const double limit=949.0-output_error; // retain 50ns counter allowance
  double error=precision;
  if(error>=limit)return 0;
  uint32_t best=0;double growth=std::max(0.0,guard);bool crossed=false;
  double previous_p95=0;uint32_t previous_horizon=0;
  for(unsigned i=0;i<Buckets;++i){const auto& b=bins_[i];const auto s=summary(i,now);
   if((percentile==99?s.p99_samples:s.samples)<minimum(percentile) || !b.scored || now<b.scored || now-b.scored>FreshTicks)continue;
   const auto error_percentile=percentile==99?s.p99_ns:s.p95_ns;
   // Differences between measured quantiles estimate growth without turning
   // the endpoint noise floor itself into a rate.
   if(previous_horizon)growth=std::max(growth,
       std::max(0.0,double(error_percentile)-previous_p95)*1000/(Horizons[i]-previous_horizon));
   previous_p95=std::max(previous_p95,double(error_percentile));previous_horizon=Horizons[i];
   const double next_error=std::max(error,double(error_percentile));
   if(next_error>=limit){
    best+=uint32_t(std::floor((Horizons[i]-best)*(limit-error)/(next_error-error)));
    crossed=true;break; // a later passing endpoint cannot erase this earlier crossing
   }
   best=Horizons[i];error=next_error;
  }
  // No measured population means no estimate. Zero observed/guard growth
  // cannot justify an infinite horizon: retain the last tested duration.
  if(best && !crossed && growth>0){
   const double projected=best+(limit-error)*1000/growth;
   best=uint32_t(std::min(double(UINT32_MAX),std::floor(projected)));
  }
  const double rate=std::max(0.0,guard);
  const double room=999.0-precision-output_error;
  if(rate>0)best=uint32_t(std::max(0.0,std::min(double(best),std::floor(room*1000/rate))));
  const uint64_t age_ms=age_ticks/(Hz/1000);
  const auto result=best>age_ms?uint32_t(best-age_ms):0;
  return percentile==99?std::min(result,budget(now,precision,guard,output_error,age_ticks,95)):result;
 }
 Summary summary(unsigned i,uint64_t now) const {
  if(i>=Buckets)return {};
  const auto& b=bins_[i];Summary s{};
  if(b.cache_valid && now>=b.cache_from && now<b.cache_until)return b.cache;
  uint64_t until=UINT64_MAX;
  s.missed=b.missed;s.failures=b.failures;s.expired=b.expired;
  uint32_t p95[Capacity],p99[Capacity];
  for(unsigned j=0;j<b.count;++j){const auto k=(b.next+Capacity-b.count+j)%Capacity;
   if(now<b.times[k]){until=std::min(until,b.times[k]);continue;}
   const auto age=now-b.times[k];
   if(age<=WindowTicks){p95[s.samples++]=b.errors[k];until=std::min(until,b.times[k]+WindowTicks+1);}
   if(age<=Hz*p99_window_seconds(i)){p99[s.p99_samples++]=b.errors[k];until=std::min(until,b.times[k]+Hz*p99_window_seconds(i)+1);}
  }
  if(s.samples){std::sort(p95,p95+s.samples);s.p95_ns=p95[clock_stats::percentile_rank(s.samples)-1];}
  if(s.p99_samples){std::sort(p99,p99+s.p99_samples);s.p99_ns=p99[clock_stats::percentile_rank(s.p99_samples,99)-1];}
  b.cache=s;b.cache_from=now;b.cache_until=until;b.cache_valid=true;return s;
 }
 Summary summary(unsigned i) const {return summary(i,std::max(now_,last_endpoint_));}
 uint32_t samples(unsigned percentile=95) const {uint32_t n=0;for(unsigned i=0;i<Buckets;++i){const auto s=summary(i);n=std::max(n,percentile==99?s.p99_samples:s.samples);}return n;}
 uint32_t samples(uint64_t now,unsigned percentile) const {uint32_t n=0;for(unsigned i=0;i<Buckets;++i){const auto s=summary(i,now);n=std::max(n,percentile==99?s.p99_samples:s.samples);}return n;}
 uint32_t experiments() const{return experiments_;}
 uint32_t failures() const{return failures_;}
 uint32_t last_error_ns() const{return last_error_;}
private:
 struct Bin {
  uint64_t scored=0,times[Capacity]{};
  uint32_t errors[Capacity]{},missed=0,failures=0,expired=0;
  unsigned count=0,next=0;
  mutable Summary cache{};
  mutable uint64_t cache_from=0,cache_until=0;
  mutable bool cache_valid=false;
 } bins_[Buckets]{};
 struct Snapshot {uint64_t available=0;ClockPrediction model{};};
 struct IdentifiedSnapshot:Snapshot {uint64_t identity=0;};
 using SnapshotType=std::conditional_t<WithIdentity,IdentifiedSnapshot,Snapshot>;
 SnapshotType models_[ModelCapacity]{};
 unsigned model_next_=0,model_count_=0;
 uint64_t last_available_=0,now_=0,last_endpoint_=0;
 uint32_t experiments_=0,failures_=0,last_error_=0;
};
using HoldTimer=HoldTimerBase<false>;
using IdentifiedHoldTimer=HoldTimerBase<true>;
inline void format_hold_time(char* out,unsigned size,uint32_t ms){
 if(ms<1)std::snprintf(out,size,"--");
 else if(ms<1000)std::snprintf(out,size,"%lums",(unsigned long)ms);
 else std::snprintf(out,size,"%lu.%lus",(unsigned long)(ms/1000),(unsigned long)((ms%1000)/100));
}

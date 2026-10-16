#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "clock_statistics.hpp"

// Causal phase/frequency learner. Candidate and regression are scored against
// the SAME unseen reference, before either uses it for training. Steering uses
// the winner through the discipline's existing phase slew and rate actuator.
class ClockFeedback {
public:
 static constexpr uint64_t Hz=20000000;
 void reset(){*this=ClockFeedback{};}
 int64_t predict(uint64_t tick) const {
  if(!initialized_ || tick<tick_)return epoch_;
  const auto dt=tick-tick_;
  return epoch_+int64_t(dt)*50+int64_t(std::llround(double(dt)/Hz*rate_));
 }
 void observe(uint64_t tick,int64_t reference,int64_t baseline,double baseline_rate,
              uint32_t noise_ns){
  if(initialized_ && (tick<=tick_ || tick-tick_>Hz*15)){reset();}
  if(!initialized_){
   initialized_=true;tick_=tick;epoch_=baseline;rate_=baseline_rate;return;
  }
  // 1 ms hold trials remain available; rate learning does not divide noisy
  // packet measurements by a 1 ms interval. Decimate learning to >=100 ms.
  if(tick-tick_<Hz/10)return;
  const double dt=double(tick-tick_)/Hz;
  const int64_t predicted=predict(tick);
  const double innovation=double(reference-predicted);
  innovation_ns_=int32_t(std::clamp(innovation,double(INT32_MIN),double(INT32_MAX)));
  const double base_error=std::abs(double(reference-baseline));
  const double candidate_error=std::abs(innovation);
  baseline_errors_[count_]=saturate(base_error);
  candidate_errors_[count_]=saturate(candidate_error);
  ++count_;++observations_;
  if(count_==100){
   double base_square=0,candidate_square=0;
   for(unsigned i=0;i<100;++i){
    base_square+=double(baseline_errors_[i])*baseline_errors_[i];
    candidate_square+=double(candidate_errors_[i])*candidate_errors_[i];
   }
   std::sort(baseline_errors_,baseline_errors_+100);
   std::sort(candidate_errors_,candidate_errors_+100);
   baseline_p95_=baseline_errors_[clock_stats::percentile_rank(100)-1];candidate_p95_=candidate_errors_[clock_stats::percentile_rank(100)-1];
   baseline_rms_=saturate(std::sqrt(base_square/100));
   candidate_rms_=saturate(std::sqrt(candidate_square/100));
   // Require a material improvement, with no tail regression. An active
   // candidate must keep earning selection on each new disjoint block.
   const bool better=candidate_rms_+25<baseline_rms_ &&
       double(candidate_rms_)<0.95*baseline_rms_ && candidate_p95_<=baseline_p95_;
   if(better && !selected_)++promotions_;
   if(!better && selected_)++rollbacks_;
   selected_=better;count_=0;
  }
  // Reject a catastrophically worse active prediction immediately, rather
  // than waiting for the block to finish. Its full error stays in the scores.
  if(selected_ && candidate_error>std::max(2000.0,4*base_error)){
   selected_=false;++rollbacks_;
  }
  // Adapt gain to recent prediction noise; clip only the TRAINING update.
  // The validation tail above always keeps the entire error.
  const double limit=std::clamp(4*jitter_,200.0,20000.0);
  const double bounded=std::clamp(innovation,-limit,limit);
  const double confidence=std::clamp(500.0/std::max(100.0,jitter_),0.05,1.0)*
      std::min(1.0,5000.0/std::max(1.0,double(noise_ns)));
  const double alpha=std::min(0.25,dt/(4+dt))*confidence;
  const double beta=alpha*alpha/2;
  epoch_=predicted+int64_t(std::llround(alpha*bounded));tick_=tick;
  rate_=std::clamp(rate_+std::clamp(beta*bounded/std::max(1.0,dt),-100.0,100.0),
                   baseline_rate-1000,baseline_rate+1000);
  rate_=std::clamp(rate_,-200000.0,200000.0);
  jitter_+=0.05*(std::min(candidate_error,20000.0)-jitter_);
 }
 bool selected() const{return selected_;}
 double rate() const{return rate_;}
 uint32_t observations() const{return observations_;}
 uint32_t promotions() const{return promotions_;}
 uint32_t rollbacks() const{return rollbacks_;}
 uint32_t baseline_p95() const{return baseline_p95_;}
 uint32_t candidate_p95() const{return candidate_p95_;}
 uint32_t baseline_rms() const{return baseline_rms_;}
 uint32_t candidate_rms() const{return candidate_rms_;}
 int32_t innovation_ns() const{return innovation_ns_;}
private:
 static uint32_t saturate(double v){return uint32_t(std::min(double(UINT32_MAX),std::ceil(v)));}
 uint32_t baseline_errors_[100]{},candidate_errors_[100]{};
 uint32_t observations_=0,promotions_=0,rollbacks_=0;
 uint32_t baseline_p95_=UINT32_MAX,candidate_p95_=UINT32_MAX;
 uint32_t baseline_rms_=UINT32_MAX,candidate_rms_=UINT32_MAX;
 int32_t innovation_ns_=0;
 uint64_t tick_=0;int64_t epoch_=0;
 double rate_=0,jitter_=100;
 unsigned count_=0;bool initialized_=false,selected_=false;
};

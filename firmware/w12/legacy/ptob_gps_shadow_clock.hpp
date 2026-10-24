#pragma once
#include <array>
#include <cstdint>
#include <cmath>
#include "ptob_gnss_reference.hpp"

namespace ptob_gps {
// Separate source models. Neither can write a BLE estimator or authorize TX.
// Integer epochs/counters stay integer; floating point is confined to a
// sub-second quotient and descriptive fit scatter.
#ifdef PTOB_GPS_PHYSICAL_REFERENCE_ONLY
// C3 BLE leaf: retain physical continuity/RMC metadata below, without fitting
// a GPS clock or allocating two unused source histories. Never GPS authority.
template<uint32_t Hz> class Affine {
public:
 struct Model {uint64_t available=0;bool valid=false;
  bool predict(uint64_t,uint64_t,int64_t&)const{return false;}} model;
 uint32_t resets=0,rms_ns=UINT32_MAX;unsigned samples=0;
 bool observe(uint64_t,uint32_t,uint64_t){return false;}
 void reset(){ptob_gnss::increment(resets);}
 void release(uint64_t){}int32_t rate_ppb()const{return 0;}
 const char* prediction_method()const{return "PHYSICAL_REFERENCE_ONLY";}
};
#else
template<uint32_t Hz> class Affine {
public:
 struct Model {
  uint64_t origin=0,available=0,last_tick=0;
  int64_t intercept_q16=0,rate_q16=int64_t(Hz)*65536;
  uint32_t number=0,last_number=0,generation=0;bool valid=false;int64_t curve_q16=0;
  bool predict(uint64_t tick,uint64_t physical_available_before,int64_t& ns)const {
   if(!valid || !physical_available_before || available>=physical_available_before ||
      tick<origin || tick<last_tick || tick-last_tick>uint64_t(Hz)*3)return false;
   if(curve_q16){
    // Invert only the small relative counter interval, never a UTC epoch.
    // The stable quadratic root avoids subtracting two nearly equal rates.
    const double rate=double(rate_q16)/65536,curve=double(curve_q16)/65536;
    const double delta=double(tick-origin)-double(intercept_q16)/65536;
    const double discriminant=rate*rate+4*curve*delta;
    if(!(discriminant>0))return false;
    const double seconds=2*delta/(rate+std::sqrt(discriminant));
    if(!std::isfinite(seconds)||seconds<0||seconds>36)return false;
    const auto whole=int64_t(std::floor(seconds));
    ns=(int64_t(number)+whole)*1000000000LL+int64_t(std::llround((seconds-whole)*1e9));return true;
   }
   const int64_t q=int64_t(tick-origin)*65536-intercept_q16;
   const int64_t whole=q/rate_q16,rem=q%rate_q16;
   ns=int64_t(number)*1000000000LL+whole*1000000000LL+
      int64_t(std::llround(double(rem)/double(rate_q16)*1e9));return true;
  }
 } model;
 uint32_t resets=0;unsigned samples=0;uint32_t rms_ns=0;
 bool observe(uint64_t tick,uint32_t number,uint64_t available) {
  if(!tick || !number || !available)return false;
  if(samples && (number!=last_number_+1 || tick<=last_tick_ ||
     tick-last_tick_<Hz/2 || tick-last_tick_>uint64_t(Hz)*3/2))reset();
  ticks_[head_]=tick;head_=(head_+1)%ticks_.size();if(samples<ticks_.size())++samples;
  last_tick_=tick;last_number_=number;
  if(samples<8){model.valid=false;return false;}
  const unsigned n=samples,oldest=(head_+ticks_.size()-n)%ticks_.size();
  const uint64_t origin=ticks_[oldest];int64_t sy=0,sxy=0,sx=0,sx2=0;
  for(unsigned x=0;x<n;++x){const int64_t y=int64_t(ticks_[(oldest+x)%ticks_.size()]-origin)-int64_t(x)*Hz;
   sy+=y;sxy+=int64_t(x)*y;sx+=x;sx2+=x*x;}
  const int64_t denom=int64_t(n)*sx2-sx*sx;
  int64_t slope=(int64_t(n)*sxy-sx*sy)*65536/denom;
  int64_t intercept=(sy*65536-slope*sx)/n,curve=0;
#ifdef PTOB_W12_FINE_CLOCK
  if(n==32){
   // Orthogonal centered integer basis: x=2*j-31, z=x*x-341.
   // Fit only the previous captured PPS population. Limit acceleration to
   // 20ppb/s and refuse implausible input rather than widening clock authority.
   int64_t xy=0,zy=0,xx=0,zz=0;
   for(unsigned j=0;j<n;++j){
    const int64_t y=int64_t(ticks_[(oldest+j)%ticks_.size()]-origin)-int64_t(j)*Hz;
    if(std::abs(y)>int64_t(Hz)/100){reset();return false;}
    const int64_t x=2*int64_t(j)-31,z=x*x-341;
    xy+=x*y;zy+=z*y;xx+=x*x;zz+=z*z;
   }
   const int64_t b=xy*65536/xx,c=zy*65536/zz;
   curve=4*c;
   if(std::abs(curve)>int64_t(Hz)*65536/100000000){reset();return false;}
   slope=2*b-124*c;intercept=sy*65536/n-31*b+620*c;
  }
#endif
  const int64_t rate=int64_t(Hz)*65536+slope;
  if(rate<int64_t(Hz)*65536/2 || rate>int64_t(Hz)*65536*3/2){reset();return false;}
  double squares=0;
  for(unsigned x=0;x<n;++x){const double residual=double(int64_t(ticks_[(oldest+x)%ticks_.size()]-origin)-int64_t(x)*Hz)-double(intercept+slope*x+curve*x*x)/65536.;squares+=residual*residual;}
  rms_ns=uint32_t(std::fmin(double(UINT32_MAX),std::sqrt(squares/n)*1e9/Hz));
  const uint32_t generation=model.generation+1;
  model={origin,available,tick,intercept,rate,number-n+1,number,generation,true,curve};return true;
 }
 void reset(){samples=head_=0;last_tick_=0;last_number_=0;model.valid=false;increment_reset();}
 void release(uint64_t available){if(model.valid)model.available=available;}
 int32_t rate_ppb()const{return model.valid?int32_t(std::llround(double(model.rate_q16+2*model.curve_q16*(samples-1)-int64_t(Hz)*65536)*1e9/(double(Hz)*65536))):0;}
 const char* prediction_method()const{
#ifdef PTOB_W12_FINE_CLOCK
  return samples==32&&model.valid?"QUADRATIC32":"LINEAR_WARMUP";
#else
  return "LINEAR32";
#endif
 }
private:
 std::array<uint64_t,32> ticks_{};unsigned head_=0;uint64_t last_tick_=0;uint32_t last_number_=0;
 void increment_reset(){ptob_gnss::increment(resets);}
};

#endif

class Shadow {
public:
 struct Score {uint64_t raw_extended=0;int64_t raw_error_ns=0,mapped_error_ns=0;bool raw_valid=false,mapped_valid=false;
  const char* raw_predictor=nullptr;const char* mapped_predictor=nullptr;};
 struct View {int64_t ns=0;uint64_t available=0,pps_age_ticks=UINT64_MAX,rmc_age_ticks=UINT64_MAX;
  uint32_t generation=0,map_bracket_ns=0,raw_rms_ns=UINT32_MAX,mapped_rms_ns=UINT32_MAX;bool valid=false;};
 Affine<80000000> raw;Affine<20000000> mapped;
 uint32_t epoch=1,coherent=0,rmc_accepted=0,rmc_rejected=0;const char* epoch_reason="WAIT_RMC";
 Score edge(uint32_t capture,uint32_t number,uint64_t tick,uint32_t bracket,uint64_t consumed,bool mapping_valid) {
  Score s;const uint32_t delta=capture-last_capture_;
  const bool continuous=last_number_ && last_number_!=UINT32_MAX && number==last_number_+1 &&
    consumed>=last_consumed_ && consumed-last_consumed_<40000000 && delta>=40000000 && delta<=120000000;
  if(!continuous){raw.reset();mapped.reset();invalidate_epoch("PPS_DISCONTINUITY");extended_=capture;}
  else extended_+=delta;
  s.raw_predictor=raw.prediction_method();s.mapped_predictor=mapped.prediction_method();
  s.raw_extended=extended_;int64_t predicted=0;
  // Require proof that prior source models were available before the physical
  // PPS, not merely before its queued consumption. Missing maps stay missing.
  if(mapping_valid){
   s.raw_valid=raw.model.predict(extended_,tick,predicted);if(s.raw_valid)s.raw_error_ns=predicted-int64_t(number)*1000000000LL;
   s.mapped_valid=mapped.model.predict(tick,tick,predicted);if(s.mapped_valid)s.mapped_error_ns=predicted-int64_t(number)*1000000000LL;
  }
  raw.observe(extended_,number,consumed);
  if(mapping_valid)mapped.observe(tick,number,consumed);else{mapped.reset();invalidate_epoch("UNMAPPED_PPS");}
  last_capture_=capture;last_number_=number;last_consumed_=consumed;last_tick_=mapping_valid?tick:0;bracket_=bracket;
  return s;
 }
 void release(uint64_t available){raw.release(available);mapped.release(available);}
 bool rmc(const ptob_gnss::Rmc& r,uint64_t consumed) {
  if(!r.valid || !r.fix || !last_tick_ || consumed<last_tick_ || consumed-last_tick_>18000000){
   ptob_gnss::increment(rmc_rejected);invalidate_epoch(!r.fix?"NO_FIX":"RMC_OUTSIDE_PPS_WINDOW");return false;}
  if(rmc_number_==last_number_){if(r.second==rmc_second_)return false;ptob_gnss::increment(rmc_rejected);invalidate_epoch("CONTRADICTORY_RMC");return false;}
  const bool consecutive=rmc_number_ && last_number_==rmc_number_+1 && rmc_second_!=UINT32_MAX && r.second==rmc_second_+1;
  if(!consecutive){invalidate_epoch("RMC_EPOCH_CHANGE");coherent=1;}else if(coherent!=UINT32_MAX)++coherent;
  rmc_number_=last_number_;rmc_second_=r.second;rmc_tick_=consumed;
  utc_offset_seconds_=int64_t(r.second)-last_number_;ptob_gnss::increment(rmc_accepted);epoch_reason="EMPIRICAL_RMC_FOLLOWS_PPS";return true;
 }
 View view(uint64_t tick)const {
  View v;v.map_bracket_ns=bracket_;v.generation=epoch;v.available=mapped.model.available>rmc_tick_?mapped.model.available:rmc_tick_;
  if(last_tick_ && tick>=last_tick_)v.pps_age_ticks=tick-last_tick_;
  if(rmc_tick_ && tick>=rmc_tick_)v.rmc_age_ticks=tick-rmc_tick_;
  int64_t relative=0;
  if(coherent<3 || !last_tick_ || !rmc_tick_ || tick<last_tick_ || tick<rmc_tick_ ||
     v.pps_age_ticks>30000000 || v.rmc_age_ticks>44000000 || !mapped.model.predict(tick,tick,relative))return v;
  v.ns=relative+utc_offset_seconds_*1000000000LL;v.valid=v.ns>0;
  if(v.valid){v.raw_rms_ns=raw.rms_ns;v.mapped_rms_ns=mapped.rms_ns;}return v;
 }
 void route_reset(){raw.reset();map_reset();last_number_=0;last_consumed_=0;}
 void map_reset(){mapped.reset();last_tick_=0;invalidate_epoch("MAP_EPOCH_CHANGE");}
 uint32_t last_number()const{return last_number_;}
 uint64_t last_tick()const{return last_tick_;}
private:
 uint32_t last_capture_=0,last_number_=0,rmc_number_=0,rmc_second_=0,bracket_=0;
 uint64_t extended_=0,last_consumed_=0,last_tick_=0,rmc_tick_=0;
 int64_t utc_offset_seconds_=0;
 void invalidate_epoch(const char* reason){coherent=0;rmc_number_=0;rmc_tick_=0;epoch_reason=reason;ptob_gnss::increment(epoch);}
};
} // namespace ptob_gps

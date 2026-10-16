#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include "clock_statistics.hpp"

// Three staggered 30--60 s fits of RAW counter versus reference UTC. Integer
// subtraction precedes floating point arithmetic; no disciplined time enters
// the training data. Constant memory also preserves burst observations.
class ClockTracker {
public:
 static constexpr uint64_t Hz=20000000;
 static constexpr double Horizon=10,MaxAcceleration=5;
 struct Model {
  uint64_t tick=0; int64_t epoch=0; double rate=0,acceleration=0;
  bool valid=false,curved=false;
  int64_t at(uint64_t now) const {
   if(!valid || now<tick)return 0;
   const double dt=double(now-tick)/Hz,h=std::min(dt,Horizon);
   return epoch+int64_t(now-tick)*50+int64_t(std::llround(
       dt*rate+acceleration*h*(dt-h/2)));
  }
 };
 void reset(){*this=ClockTracker{};}
 void observe(uint64_t tick,int64_t reference,int64_t baseline,bool baseline_valid=true) {
  if(last_ && (tick<=last_ || tick-last_>Hz*15))reset();
  // Score the frozen previous model before this reference enters any fit.
  if(model_.valid && baseline_valid && (!scored_ || tick-scored_>=Hz/10)){
   const auto ce=uint64_t(std::llabs(reference-model_.at(tick)));
   const auto be=uint64_t(std::llabs(reference-baseline));
   candidate_[count_]=sat(ce);baseline_[count_]=sat(be);
   ++count_;++observations_;scored_=tick;
   if(count_==100){
    double cs=0,bs=0;
    for(unsigned i=0;i<100;++i){cs+=double(candidate_[i])*candidate_[i];bs+=double(baseline_[i])*baseline_[i];}
    candidate_rms_=sat(std::ceil(std::sqrt(cs/100)));baseline_rms_=sat(std::ceil(std::sqrt(bs/100)));
    std::sort(candidate_,candidate_+100);std::sort(baseline_,baseline_+100);
    const auto p=clock_stats::percentile_rank(100)-1;
    candidate_p95_=candidate_[p];baseline_p95_=baseline_[p];
    select(uint64_t(candidate_rms_)+25<baseline_rms_ && double(candidate_rms_)<0.95*baseline_rms_
           && candidate_p95_<=baseline_p95_);count_=0;
   }
   if(selected_ && double(ce)>std::max(2000.0,4.0*be))select(false);
  }
  if(!started_ || tick-started_>=Hz*15){
   unsigned oldest=0;
   for(unsigned i=1;i<3;++i)if(windows_[i].tick<windows_[oldest].tick)oldest=i;
   if(!windows_[oldest].tick || tick-windows_[oldest].tick>Hz*60){
    windows_[oldest]=Window{};windows_[oldest].tick=tick;
    windows_[oldest].epoch=reference;started_=tick;
   }
  }
  Model best{};uint64_t origin=0;
  for(auto& w:windows_)if(w.tick){
   const double x=double(tick-w.tick)/Hz;
   if(x>60)continue;
   const double y=double(reference-w.epoch)-double(tick-w.tick)*50;
   ++w.n;w.sx+=x;w.sxx+=x*x;w.sxxx+=x*x*x;w.sxxxx+=x*x*x*x;
   w.sy+=y;w.sxy+=x*y;w.sxxy+=x*x*y;w.syy+=y*y;
   if(w.n<8 || x<30)continue;
   const double n=w.n,mx=w.sx/n,my=w.sy/n;
   const double xx=w.sxx-w.sx*mx,xy=w.sxy-w.sx*my;
   if(xx<=0)continue;
   double b=xy/xx,c=0,a=my-b*mx;
   const double cross=w.sxxx-w.sx*w.sxx/n;
   const double norm=w.sxxxx-w.sxx*w.sxx/n-cross*cross/xx;
   const double residual=std::max(0.0,w.syy-w.sy*my-xy*xy/xx);
   if(norm>1){
    const double candidate=(w.sxxy-w.sxx*my-cross*xy/xx)/norm;
    const double sigma=std::sqrt(residual/(n-2)/norm);
    if(std::isfinite(candidate) && std::abs(candidate)<=MaxAcceleration/2
       && std::abs(candidate)>2*sigma){c=candidate;b=(xy-c*cross)/xx;a=my-b*mx-c*w.sxx/n;}
   }
   const double rate=b+2*c*x,offset=a+b*x+c*x*x;
   if(!std::isfinite(rate) || !std::isfinite(offset) || std::abs(rate)>200000)continue;
   // Select by window age, never by its own training residual.
   if(!best.valid || w.tick<origin){
    best={tick,reference+int64_t(std::llround(offset-y)),rate,2*c,true,c!=0};origin=w.tick;
   }
  }
  model_=best;last_=tick;
  if(!model_.valid)select(false);
 }
 const Model& model() const{return model_;}
 bool selected() const{return selected_ && model_.valid;}
 uint32_t observations() const{return observations_;}
 uint32_t promotions() const{return promotions_;}
 uint32_t rollbacks() const{return rollbacks_;}
 uint32_t baseline_rms() const{return baseline_rms_;}
 uint32_t candidate_rms() const{return candidate_rms_;}
 uint32_t baseline_p95() const{return baseline_p95_;}
 uint32_t candidate_p95() const{return candidate_p95_;}
private:
 static uint32_t sat(uint64_t v){return uint32_t(std::min<uint64_t>(UINT32_MAX,v));}
 void select(bool v){if(v!=selected_){if(v)++promotions_;else ++rollbacks_;}selected_=v;}
 struct Window {uint64_t tick=0;int64_t epoch=0;uint32_t n=0;
  double sx=0,sxx=0,sxxx=0,sxxxx=0,sy=0,sxy=0,sxxy=0,syy=0;};
 Window windows_[3]{};Model model_{};
 uint64_t last_=0,started_=0,scored_=0;
 uint32_t baseline_[100]{},candidate_[100]{};
 uint32_t observations_=0,promotions_=0,rollbacks_=0;
 uint32_t baseline_rms_=UINT32_MAX,candidate_rms_=UINT32_MAX;
 uint32_t baseline_p95_=UINT32_MAX,candidate_p95_=UINT32_MAX;
 unsigned count_=0;bool selected_=false;
};

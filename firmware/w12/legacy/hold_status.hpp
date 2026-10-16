#pragma once
#include "reference/hold_timer.hpp"
inline void format_hold_status(char* out,unsigned size,uint32_t expected_ms,
                               bool fit,uint32_t age_s,uint32_t precision_ns,
                               uint32_t trials,uint8_t animation,bool estimated=false,unsigned percentile=95,
                               bool audit_required=false,uint32_t audit_samples=0){
 const char* pulse[]={".","..","..."};
 const auto target=percentile==99?99:95;
 const auto minimum=HoldTimer::minimum(target);
 if(expected_ms){
  char duration[12];format_hold_time(duration,sizeof(duration),expected_ms);
  std::snprintf(out,size,"1us P%u %s",target,duration);
 }else if(age_s>15)std::snprintf(out,size,"1us P%u WAIT REF",target);
 else if(!fit)std::snprintf(out,size,"1us P%u ACQUIRE%s",target,pulse[animation%3]);
 else if(precision_ns>=1000)std::snprintf(out,size,"1us P%u QUALIFY%s",target,pulse[animation%3]);
 else if(trials<minimum)std::snprintf(out,size,"P%u LEARN %u/%u%s",target,unsigned(trials),minimum,pulse[animation%3]);
 else if(audit_required && audit_samples<minimum)std::snprintf(out,size,"P%u AUDIT %u/%u%s",target,unsigned(audit_samples),minimum,pulse[animation%3]);
 else std::snprintf(out,size,"1us P%u RECHECK%s",target,pulse[animation%3]);
}

class HoldTarget {
 unsigned percentile_=95;
public:
 unsigned percentile() const{return percentile_;}
 void toggle(){percentile_=percentile_==95?99:95;}
 bool select(unsigned value){if(value!=95 && value!=99)return false;percentile_=value;return true;}
};

// Hold increases must persist for two seconds and advance at most 100 ms.
// This is conservative display pacing, not extra measurement evidence.
// Reductions are never delayed.
class HoldDisplay {
 uint32_t shown_=0,pending_=0;uint64_t since_=0;unsigned source_=0;
public:
 uint32_t update(uint64_t now,uint32_t value,unsigned source){
  if(source!=source_){shown_=pending_=0;since_=now;source_=source;}
  if(value<=shown_){shown_=value;pending_=0;since_=now;return shown_;}
  if(!pending_){pending_=value;since_=now;}
  else pending_=std::min(pending_,value);
  if(now>=since_ && now-since_>=2*HoldTimer::Hz){shown_=std::min(pending_,shown_+100);pending_=0;since_=now;}
  return shown_;
 }
};

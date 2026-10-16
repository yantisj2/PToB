#pragma once
#include <cstdint>
namespace v12 {
// Worker-only display analysis. Raw samples and radio/source decisions remain
// independent. One state per clock; missing/invalid samples never renew age.
struct TdSignal {
 int32_t mean_q8=0,sum=0;uint16_t count=0;
 int8_t value=127;uint8_t bars=0;bool initialized=false,weak=false;
 uint64_t seen_us=0,step_us=0,display_us=0;
 static constexpr uint64_t StepUs=250000,DisplayUs=1000000,StaleUs=10000000;
 static constexpr int NumericDeadband=2,BarHysteresis=3;
 static unsigned direct_bars(int rssi){return rssi>=-55?4:rssi>=-67?3:rssi>=-79?2:1;}
 bool fresh(uint64_t now)const{return initialized&&now>=seen_us&&now-seen_us<StaleUs;}
 void observe(int8_t raw,uint64_t now){
  if(raw<-127||raw>20)return;
  if(initialized&&now<seen_us)return; // Queued observations may arrive out of order.
  if(!initialized||now-seen_us>=StaleUs){
   *this={};initialized=true;value=raw;mean_q8=int32_t(raw)*256;
   bars=uint8_t(direct_bars(raw));weak=raw<-85;seen_us=step_us=display_us=now;return;
  }
  seen_us=now;
  if(count<UINT16_MAX){sum+=raw;++count;}
  if(now-step_us>=StepUs){
   const int average=(sum+(sum<0?-int(count/2):int(count/2)))/int(count);
   mean_q8+=(average*256-mean_q8)/4;sum=0;count=0;step_us=now;
  }
  if(now-display_us<DisplayUs)return;
  const int candidate=(mean_q8+(mean_q8<0?-128:128))/256;
  const int delta=candidate-int(value);
  if(delta>=NumericDeadband||delta<=-NumericDeadband)value=int8_t(candidate);
  const int thresholds[]={-79,-67,-55};
  while(bars<4&&candidate>=thresholds[bars-1]+BarHysteresis)++bars;
  while(bars>1&&candidate<thresholds[bars-2]-BarHysteresis)--bars;
  if(candidate<-85)weak=true;else if(candidate>=-82)weak=false;
  display_us=now;
 }
};
}

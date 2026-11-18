// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cstdint>

namespace w12 {
enum class ClockMode : uint8_t { Unsynced, Ble, LoRa, Hold };
struct ClockInput {
 uint64_t utc=0,until=0,id=0,scope=0;
 int32_t rate=0;
 bool live(uint64_t tick)const {
  return utc&&utc<=INT64_MAX&&until>tick&&id&&scope&&rate>=-50000&&rate<=50000;
 }
};
struct ClockSegment {
 uint64_t tick=0,utc=0,until=0;
 int32_t rate=0,phase=0;
 ClockMode mode=ClockMode::Unsynced;

 ClockMode mode_at(uint64_t at)const{return until&&at>=until?ClockMode::Hold:mode;}
 uint64_t at(uint64_t now)const {
  if(!tick||now<tick||now-tick>uint64_t(INT64_MAX)/100000)return 0;
  const int64_t dt=int64_t(now-tick);
  const int64_t phase_ticks=until>tick?int64_t(std::min(now,until)-tick):0;
  const int64_t budget=phase_ticks/(mode==ClockMode::LoRa?4000:400);
  const auto elapsed=dt*50+dt*rate/20000000+std::clamp<int64_t>(phase,-budget,budget);
  return elapsed>=0&&uint64_t(elapsed)<=uint64_t(INT64_MAX)-utc?utc+uint64_t(elapsed):0;
 }
};

// A boot-long, monotonic actuator. The inputs are projections of independent,
// admitted radio models at the same tick. PPS has no route into the actuator.
class DualClock {
 ClockSegment current_,previous_;
 uint64_t ble_since_=0,ble_last_=0,ble_id_=0,ble_scope_=0;
 uint64_t id_=0,scope_=0,last_tick_=0,lora_at_=0;
 ClockMode selected_=ClockMode::Unsynced;

 public:
 const ClockSegment& segment(uint64_t tick)const{return tick>=current_.tick?current_:previous_;}
 void update(uint64_t tick,const ClockInput& ble,const ClockInput& lora){
  if(!tick||tick<last_tick_)return;
  last_tick_=tick;
  if(ble_last_&&tick-ble_last_>10000000)ble_since_=ble_last_=0;
  if(ble.live(tick)){
   if(ble.scope!=ble_scope_){ble_id_=0;ble_since_=ble_last_=0;ble_scope_=ble.scope;}
   if(ble.id>ble_id_){if(!ble_since_)ble_since_=tick;ble_last_=tick;ble_id_=ble.id;}
  }
  const auto prior=current_.mode_at(tick);
  const auto utc=current_.at(tick);
  if(current_.tick&&!utc)return;
  // Require 100 ms of fresh physical BLE progress after a coarse handoff.
  // A brief projection refusal does not restart a still-progressing fit.
  const bool ble_ready=ble.live(tick)&&ble.id==ble_id_&&
    (!utc||prior==ClockMode::Ble||(ble_since_&&tick-ble_since_>=2000000&&ble_last_&&ble_since_<ble_last_));
  for(const auto mode:{ClockMode::Ble,ClockMode::LoRa}){
   const auto& input=mode==ClockMode::Ble?ble:lora;
   if((mode==ClockMode::Ble&&!ble_ready)||!input.live(tick))continue;
   const auto error=utc?int64_t(input.utc)-int64_t(utc):0;
   if(error< -1000000000||error>1000000000)continue;
   const bool same=selected_==mode&&scope_==input.scope;
   if(same&&input.id<id_)continue;
   const bool new_sample=!same||input.id>id_;
   if(!new_sample&&prior==mode){current_.until=input.until;return;}
   int32_t rate=current_.rate;
   if(!utc)rate=input.rate;
   else if(new_sample&&mode==ClockMode::Ble)rate+=(input.rate-rate)/8;
   else if(new_sample&&same&&tick>lora_at_){
    const int64_t span=std::min<uint64_t>(tick-lora_at_,600000000);
    const int64_t limit=span/1200000; // 500 ppb per 30 physical seconds.
    rate+=int32_t(std::clamp((input.rate-rate)*span/1200000000,-limit,limit));
   }
   if(new_sample&&mode==ClockMode::LoRa)lora_at_=tick;
   previous_=current_;
   current_={tick,utc?utc:input.utc,input.until,rate,int32_t(error),mode};
   selected_=mode;scope_=input.scope;id_=input.id;return;
  }
  if(utc&&current_.mode!=ClockMode::Hold){
   previous_=current_;current_={tick,utc,0,current_.rate,0,ClockMode::Hold};
  }
 }
};
static_assert(sizeof(DualClock)<=160);
}

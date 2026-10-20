// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "ui.hpp"
#include <algorithm>
#include <cstring>
namespace n30 {
inline bool lora_time(const n30_lora_view& l,uint64_t now,uint64_t& utc){
 if(!l.requested||!l.active||l.fault||l.state!=2||!l.utc_us||now<l.sampled_us||now-l.sampled_us>=SnapshotMaxUs||now>=l.until_us||now-l.sampled_us>UINT64_MAX-l.utc_us)return false;
 utc=l.utc_us+now-l.sampled_us;return true;
}
inline bool ble_time(const Model& m,uint64_t now,uint64_t& utc){
 const auto& b=m.ble;if(!current(m,now)||!b.valid||now<b.at_us||now>=b.until_us||now-b.at_us>UINT64_MAX-b.utc_us)return false;
 utc=b.utc_us+now-b.at_us;return true;
}
inline void select_lora(Model& m,const n30_lora_view& l,uint64_t now){
 m.lora=l;uint64_t ignored;
 if(m.state==State::Follow&&time(m,now,ignored))return; // Actual usable BLE wins.
 if(!lora_time(l,now,ignored))return;
 m.utc_at_us=l.sampled_us;m.utc_us=l.utc_us;m.until_us=l.until_us;
 m.valid=m.locked=true;m.state=State::Follow;m.source=Source::LoRa;m.hz=0;m.serving=false;m.delivery=UINT16_MAX;m.rms_ns=l.link_samples>=4?l.link_rms_ns:UINT32_MAX;
}
class DualComparison {
 uint8_t context_[16]{};uint64_t at_=0,epoch_=0;uint32_t sequence_=0,generation_=0;int64_t delta_=0;
 bool seen_=false;
 public:
 void update(Model& m,uint64_t now){
  m.delta_valid=m.drift_valid=false;uint64_t ble,lora;
  const auto& l=m.lora;const auto& b=m.ble;
  if(!ble_time(m,now,ble)||!lora_time(l,now,lora)||!b.following||!l.metadata_known||!b.root||b.root!=l.root||!b.boot||b.boot!=l.root_boot||!b.epoch||b.epoch!=l.source_epoch){seen_=false;return;}
  const bool positive=ble>=lora;const uint64_t difference=positive?ble-lora:lora-ble;
  if(difference>uint64_t(INT64_MAX)/1000-1){seen_=false;return;}
  const int64_t delta=(positive?int64_t(difference)*1000:-int64_t(difference)*1000)-int64_t(l.event_ns%1000);
  m.delta_ns=delta;m.delta_valid=true;
  const bool same=seen_&&generation_==l.generation&&epoch_==l.capture_epoch&&!std::memcmp(context_,l.context,16)&&now>=at_;
  if(same&&sequence_==l.corrected_sequence){m.drift_valid=drift_valid_;m.drift_ppb=drift_;return;}
  drift_valid_=false;
  if(same&&l.corrected_sequence>sequence_&&now-at_>=1000000&&now-at_<12000000){
   const long double rate=(static_cast<long double>(delta)-delta_)*1000000/(now-at_);
   if(rate>=INT32_MIN&&rate<=INT32_MAX){drift_=int32_t(rate);drift_valid_=true;}
  }
  std::memcpy(context_,l.context,16);at_=now;epoch_=l.capture_epoch;generation_=l.generation;sequence_=l.corrected_sequence;delta_=delta;seen_=true;
  m.drift_ppb=drift_;m.drift_valid=drift_valid_;
 }
 private:bool drift_valid_=false;int32_t drift_=0;
};
}

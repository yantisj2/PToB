// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include <cstdint>
namespace n30 {
enum class LoRaMode:uint8_t {Auto,KeepOn,KeepOff};
constexpr uint64_t BleAcquireUs=180000000,BleRecoverUs=180000000;
// Single radio owner. Inputs are actual live BLE lock, never cached UI pixels
// or packet reception. A stale main snapshot counts as loss, not a good clock.
class LoRaPolicy {
 bool seen_=false,good_=false,automatic_=false;
 uint64_t last_=0,changed_=0;
 LoRaMode mode_=LoRaMode::Auto;
 public:
 void update(bool locked,uint64_t now){
  if(!seen_||now<last_){seen_=true;good_=locked;automatic_=false;changed_=now;}
  else if(locked!=good_){good_=locked;changed_=now;}
  last_=now;
  if(!good_&&!automatic_&&now-changed_>=BleAcquireUs)automatic_=true;
  if(good_&&automatic_&&now-changed_>=BleRecoverUs)automatic_=false;
 }
 bool requested()const{return mode_==LoRaMode::KeepOn||(mode_==LoRaMode::Auto&&automatic_);}
 LoRaMode mode()const{return mode_;}
 void toggle(){mode_=mode_==LoRaMode::Auto?(requested()?LoRaMode::KeepOff:LoRaMode::KeepOn):mode_==LoRaMode::KeepOn?LoRaMode::KeepOff:LoRaMode::Auto;}
 void set(LoRaMode mode){mode_=mode;}
 uint32_t remaining_ms(uint64_t now)const{
  if(mode_!=LoRaMode::Auto||!seen_||now<changed_||good_!=automatic_)return 0;
  const auto limit=automatic_?BleRecoverUs:BleAcquireUs;
  return now-changed_>=limit?0:uint32_t((limit-(now-changed_)+999)/1000);
 }
};
}

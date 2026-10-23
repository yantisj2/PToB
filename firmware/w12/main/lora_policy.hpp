// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include <cstdint>
#include "g1_recovery_budget.hpp"
namespace w12 {
enum class LoRaMode:uint8_t {Auto,KeepOn,KeepOff};
constexpr uint64_t BleAcquireUs=180000000,BleRecoverUs=180000000;
// Single radio owner. Inputs are actual live BLE lock, never cached UI pixels
// or packet reception. A stale main snapshot counts as loss, not a good clock.
class LoRaPolicy {
 bool automatic_=false;
 bool client_hold_=false;
 v12::G1RecoveryBudget budget_;
 LoRaMode mode_=LoRaMode::Auto;
 public:
 void update(bool locked,uint64_t now,bool client_hold=false){
  client_hold_=client_hold;
  if(budget_.observe(locked,now,BleRecoverUs))automatic_=false;
  if(!automatic_&&budget_.lost()>=BleAcquireUs)automatic_=true;
  if(automatic_&&budget_.stable(now,BleRecoverUs))automatic_=false;
 }
 bool requested()const{return mode_==LoRaMode::KeepOn||(mode_==LoRaMode::Auto&&(automatic_||client_hold_));}
 LoRaMode mode()const{return mode_;}
 void toggle(){mode_=mode_==LoRaMode::Auto?(requested()?LoRaMode::KeepOff:LoRaMode::KeepOn):mode_==LoRaMode::KeepOn?LoRaMode::KeepOff:LoRaMode::Auto;}
 void set(LoRaMode mode){mode_=mode;}
 uint32_t remaining_ms(uint64_t now)const{
  if(mode_!=LoRaMode::Auto||client_hold_||budget_.good()!=automatic_)return 0;
  const auto left=automatic_?budget_.recovery_left(now,BleRecoverUs):budget_.loss_left(BleAcquireUs);
  return uint32_t((left+999)/1000);
 }

};
}

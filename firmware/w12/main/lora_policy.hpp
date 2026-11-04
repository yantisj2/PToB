// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include <cstdint>
namespace w12 {
enum class LoRaMode:uint8_t {Auto,KeepOn,KeepOff};
// Passive schedule8 needs the300s META context and a warm sparse timing fit.
// This is radio policy only; BLE selection, source expiry and RC9 leases remain separate.
class LoRaPolicy {
 LoRaMode mode_=LoRaMode::Auto;
 public:
 void update(bool,uint64_t,bool=false){}
 bool requested()const{return mode_!=LoRaMode::KeepOff;}
 LoRaMode mode()const{return mode_;}
 void toggle(){mode_=mode_==LoRaMode::Auto?LoRaMode::KeepOff:mode_==LoRaMode::KeepOn?LoRaMode::KeepOff:LoRaMode::Auto;}
 void set(LoRaMode mode){mode_=mode;}
 uint32_t remaining_ms(uint64_t)const{return 0;}
};
}

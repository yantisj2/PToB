// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include <cstdint>
namespace n30 {
enum class LoRaMode:uint8_t {Auto,KeepOn,KeepOff};
// Continuous passive RX keeps the300s META context and independent fit warm.
// The30s BLE recovery dwell belongs to clock selection, never radio shutdown.
constexpr uint64_t BleAcquireUs=0,BleRecoverUs=0;
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

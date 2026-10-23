// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "lora_policy.hpp"

#include <cassert>
#include <cstring>
int main(){
 using namespace w12;LoRaPolicy p;const uint64_t start=1000000;
 p.update(false,start);assert(!p.requested()&&p.remaining_ms(start)==180000);
 p.update(false,start+BleAcquireUs-1);assert(!p.requested());p.update(false,start+BleAcquireUs);assert(p.requested());
 p.toggle();assert(p.mode()==LoRaMode::KeepOff&&!p.requested());p.toggle();assert(p.mode()==LoRaMode::Auto&&p.requested());
 const auto good=start+BleAcquireUs+10;p.update(true,good);p.update(true,good+BleRecoverUs-1);assert(p.requested());
 p.update(false,good+BleRecoverUs-1);p.update(true,good+BleRecoverUs);p.update(true,good+2*BleRecoverUs-1);assert(p.requested());
 p.update(true,good+2*BleRecoverUs);assert(!p.requested());p.toggle();assert(p.mode()==LoRaMode::KeepOn&&p.requested());p.toggle();assert(p.mode()==LoRaMode::KeepOff&&!p.requested());p.toggle();assert(p.mode()==LoRaMode::Auto&&!p.requested());
 p.update(false,good+2*BleRecoverUs+1);p.update(true,good+2*BleRecoverUs+100);p.update(false,good+2*BleRecoverUs+101);p.update(false,good+3*BleRecoverUs);assert(p.requested());
 p.update(false,1);assert(!p.requested()); // Monotonic clock rollback resets dwell.
 LoRaPolicy held;held.update(false,start);held.update(false,start+BleAcquireUs);
 assert(held.requested());const auto recovered=start+BleAcquireUs+1;
 held.update(true,recovered,true);held.update(true,recovered+BleRecoverUs,true);
 assert(held.requested()&&held.remaining_ms(recovered+BleRecoverUs)==0);
 // Once actual final demand expires, already recovered BLE can retire RX.
 held.update(true,recovered+BleRecoverUs+1,false);assert(!held.requested());
 held.update(true,recovered+BleRecoverUs+2,true);assert(held.requested());
 held.set(LoRaMode::KeepOff);assert(!held.requested()); // Explicit OFF wins.

}

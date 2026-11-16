// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "radio_watchdog.hpp"
#include <cassert>
int main(){
 watch::RadioFaultTimer ble,lor;
 assert(!ble.observe(0,true,true));assert(!ble.observe(89999999,true,true));
 assert(ble.observe(90000000,true,true));assert(!ble.observe(90000001,true,true));
 assert(!lor.observe(90000000,true,true));assert(ble.observe(180000000,true,true));
 assert(lor.observe(180000000,true,true));
 // Explicit OFF/maintenance suppress and cancel the original timer.
 assert(!ble.observe(180000001,true,false));assert(!ble.observe(300000000,true,true));
 assert(ble.observe(390000000,true,true));
 // Healthy radio without any external time does not need a restart.
 assert(!ble.observe(400000000,false,true));assert(!ble.observe(999000000,false,true));
 assert(!ble.observe(1000000000,true,true));assert(!ble.observe(1000000001,false,true));
 assert(!ble.observe(1100000000,true,true));assert(!ble.observe(1189999999,true,true));
 assert(ble.observe(1190000000,true,true));
 // Monotonic reset cannot underflow into an immediate restart.
 assert(!ble.observe(1,true,true));assert(ble.observe(90000001,true,true));
}

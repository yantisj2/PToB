// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace watch::energy {
// CPU DFS only. The XTAL/APB counters never stop or change frequency.
bool init();
bool ble(bool required); // BLE lifecycle owner; held before SDK/capture startup.
bool display(bool required); // UI owner; held through every panel transaction.
void usb(bool present);
bool usb();
struct Status {uint32_t cpu_mhz=0;int error=0;bool ready=false,ble=false,display=false,usb=false;};
Status status();
}

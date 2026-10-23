// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace v12::periodic_rx {struct Stamp;}
// Fixed W12 RX-only GPIO16 route. Group0 remains available for GNSS/LoRa.
// One boot-selected counter domain; failure never substitutes GPTimer ticks.
bool w12_fine_start(uint64_t boot, uint64_t factory, uint64_t now_us);
bool w12_fine_current(uint64_t* tick20);
int w12_fine_receive(v12::periodic_rx::Stamp&); // 0 refused,1 pending,2 accepted
void w12_fine_stop();
bool w12_fine_released();
bool w12_fine_fault();
struct W12FineStatus {
  uint32_t callbacks=0,claims=0,direct=0,pending=0,missing=0,ambiguous=0,unsupported=0;
  int error=0;bool active=false;
};
W12FineStatus w12_fine_status(); // Cached observer only; never latches CURRENT.

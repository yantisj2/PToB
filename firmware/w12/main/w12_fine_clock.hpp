// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace v12::periodic_rx {struct Stamp;}
// Fixed W12 RX-only GPIO16 route. Group0 remains exclusively available for GNSS.
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

struct mcpwm_cap_channel_t;
// Main-task GNSS group0 CH1 software capture enclosed by two group1 CURRENT
// latches. Same production counter coordinates; no PPS/receiver model writes.
// Caller holds its GNSS queue critical section until the bracket is complete.
bool w12_fine_gnss_map_bracket(mcpwm_cap_channel_t*,uint64_t* before20,uint64_t* after20);

using W12LoRaCapture=bool(*)(uint64_t tick20);
bool w12_fine_lora_start(W12LoRaCapture);
// Independent LoRa epoch/extension; no BLE Receiver history/fence processing.
// Same physical MCPWM1 timer as CH1. Never returns BLE or GPTimer coordinates.
bool w12_fine_lora_current(uint64_t* tick20);
bool w12_fine_lora_stop();

// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace w12::lora_board {
// W12 schematic U7: DIO8->GPIO14; DIO11 is GC1109 CSD, never IRQ.
constexpr unsigned IrqGpio=14,IrqDio=8;
constexpr uint8_t SwitchDios[]={5,6,9,10,11};
constexpr uint8_t RxSwitchDio=11; // CSD high, CTX/CPS low; both2.4GHz enables low.
}

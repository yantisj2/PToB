// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace watch {
// AXP2101 PEKEY short press: enable 0x41 bit 3, W1C status 0x49 bit 3.
// Preserve every other IRQ and the PMU's long-press/recovery configuration.
struct PowerKey {
 static constexpr uint8_t Enable=0x41,Status=0x49,Short=0x08;
 template<class Read,class Write> static bool init(Read read,Write write){
  uint8_t enabled=0;return read(Enable,enabled)&&write(Status,Short)&&write(Enable,enabled|Short);
 }
 template<class Read,class Write> static bool poll(Read read,Write write,bool& pressed){
  pressed=false;uint8_t status=0;if(!read(Status,status))return false;
  if(!(status&Short))return true;
  if(!write(Status,Short))return false; // Deliver only after acknowledgment.
  pressed=true;return true;
 }
};
}

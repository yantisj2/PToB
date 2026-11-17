// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
namespace v12 {struct ConsoleFrame;}
void watch_lora_shadow_diagnostics(v12::ConsoleFrame&);
extern "C" {
#endif
// Registered once before the worker processes packets; authenticated schedule input only.
typedef void (*watch_lora_shadow_observer)(uint8_t,const uint8_t*,uint64_t,uint32_t,uint64_t,uint64_t,bool);
void watch_lora_shadow_set_observer(watch_lora_shadow_observer);
#ifdef __cplusplus
}
#endif

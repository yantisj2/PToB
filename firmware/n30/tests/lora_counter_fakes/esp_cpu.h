// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
extern uint32_t fake_cpu_cycles;
static inline uint32_t esp_cpu_get_cycle_count(void){return fake_cpu_cycles++;}

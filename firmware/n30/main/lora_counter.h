// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "driver/gptimer.h"
#include "driver/mcpwm_cap.h"
#ifdef __cplusplus
extern "C" {
#endif
esp_err_t n30_lora_counter_bracket(gptimer_handle_t,mcpwm_cap_channel_handle_t,uint64_t*,uint64_t*);
// Fixed group0/CH2, software CURRENT only, no IRQ/callback or BLE resources.
esp_err_t n30_lora_counter_current(mcpwm_cap_channel_handle_t,uint32_t*,uint64_t*);
#ifdef __cplusplus
}
#endif

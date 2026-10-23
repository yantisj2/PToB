// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "lora_view.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
bool w12_rc8_init(uint64_t factory,uint64_t transmitter,uint64_t group_id,uint32_t epoch,const uint8_t psk[32]);
uint32_t w12_rc8_switch(bool enabled);
bool w12_rc8_receive(const uint8_t*,size_t,uint64_t tick16,uint64_t capture_epoch,uint64_t local_us,uint64_t now_us,uint32_t generation);
void w12_rc8_snapshot(struct w12_lora_view*,uint64_t now_us);
#ifdef __cplusplus
}
#endif

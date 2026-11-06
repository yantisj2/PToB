// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "lora_view.h"
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
bool n30_rc8_init(uint64_t factory,uint64_t transmitter,uint64_t group_id,uint32_t epoch,const uint8_t psk[32]);
uint32_t n30_rc8_switch(bool enabled);
bool n30_rc8_receive(const uint8_t*,size_t,uint64_t tick16,uint64_t capture_epoch,uint64_t local_us,uint64_t now_us,uint32_t generation);
void n30_rc8_clock(uint64_t tick16,uint64_t capture_epoch,uint64_t now_us);
void n30_rc8_snapshot(struct n30_lora_view*,uint64_t now_us);
// Native counter/coarse pairs retain their physical sampling time. Availability
// supplies source/deadline checks only and cannot relabel or renew that sample.
void n30_rc8_clock_sample(uint64_t tick16,uint64_t capture_epoch,uint64_t sampled_us,uint64_t available_us);
void n30_rc8_snapshot_sample(struct n30_lora_view*,uint64_t sampled_us,uint64_t available_us);
// Finite engineering delivery-HOLD test. RF remains observable. Never renews
// the original physical/META expiry; rejects untrained/stale/old references.
bool n30_rc8_hold60(uint64_t request_id,uint64_t now_us);
void n30_rc8_radio_fault(void);
#ifdef __cplusplus
}
#endif

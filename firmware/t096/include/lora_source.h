/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "lora_rc8_policy.h"
#include "precision.h"
/* Pure snapshot adapters. Scope is already selected/authenticated by its owner.
 * Their outputs grant only a physical-event mapping, never BLE authority. */
bool r2_lora_source_own(struct rc8_export *,const struct r2_source_clock *,struct rc8_scope,
 struct r2_capture,uint64_t until_tick);
bool r2_lora_source_follower(struct rc8_export *,const r2_servo_map_t *,struct rc8_scope,
 struct r2_capture,uint64_t until_tick,uint64_t created,uint64_t expires,int64_t offset,uint32_t generation);

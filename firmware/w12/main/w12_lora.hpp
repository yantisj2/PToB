// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "lora_view.h"
#include "lora_policy.hpp"
#include "bounded_log.hpp"
void w12_lora_control(bool ble_locked,uint64_t sampled_us,bool allowed,
 uint64_t rc9_client_until_us=0,uint64_t rc9_source_until_us=0);
bool w12_lora_mode(w12::LoRaMode);
bool w12_lora_cycle();
void w12_lora_snapshot(w12_lora_view&);

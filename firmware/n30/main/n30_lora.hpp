// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "lora_view.h"
#include "lora_policy.hpp"
#include "bounded_log.hpp"
void n30_lora_control(bool ble_locked,uint64_t sampled_us,bool allowed);
bool n30_lora_mode(n30::LoRaMode);
bool n30_lora_cycle();
void n30_lora_snapshot(n30_lora_view&);
void n30_lora_diagnostics(v12::ConsoleFrame&);

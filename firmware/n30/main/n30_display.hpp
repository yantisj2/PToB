// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "native_r2.hpp"
#include "display.hpp"
#include "bounded_log.hpp"
#include "ui.hpp"
void n30_display_publish(const v12::R2Status&,const DisplayState&,uint64_t now,bool ble_parked=false);
void n30_display_status(v12::ConsoleFrame&);
void n30_display_observe_source(const v12::R2Status&,uint64_t now,uint32_t refusal);
void n30_display_dual_frame(v12::ConsoleFrame&,uint32_t after,uint32_t parent_after,uint64_t now);
void n30_display_trace_poll(); // Main-task capture-only transition output.

bool n30_display_page(unsigned page);
bool n30_display_take_upstream_toggle(uint64_t now);

bool n30_display_take_lora_toggle(uint64_t now);
bool n30_display_take_ble_toggle(uint64_t now);
void n30_display_action_result(n30::Action,bool accepted);
bool n30_display_minimal();
bool n30_display_take_poweroff();
void n30_display_request_poweroff();
bool n30_display_power_ready();

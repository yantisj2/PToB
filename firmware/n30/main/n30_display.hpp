// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "native_r2.hpp"
#include "display.hpp"
#include "bounded_log.hpp"
void n30_display_publish(const v12::R2Status&,const DisplayState&,uint64_t now);
void n30_display_status(v12::ConsoleFrame&);
void n30_display_trace_poll(); // Main-task capture-only transition output.

bool n30_display_page(unsigned page);
bool n30_display_take_upstream_toggle(uint64_t now);

bool n30_display_take_lora_toggle(uint64_t now);
bool n30_display_minimal();
bool n30_display_take_poweroff();
void n30_display_request_poweroff();
bool n30_display_power_ready();

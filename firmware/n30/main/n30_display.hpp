// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "native_r2.hpp"
#include "display.hpp"
#include "bounded_log.hpp"
void n30_display_publish(const v12::R2Status&,const DisplayState&,uint64_t now);
void n30_display_status(v12::ConsoleFrame&);

bool n30_display_page(unsigned page);
bool n30_display_take_upstream_toggle(uint64_t now);

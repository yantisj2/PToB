// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "n30_timing_probe.hpp"
namespace v12 {struct Report;struct ConsoleFrame;}
void n30_window_init(bool (*read20)(uint64_t*));
bool n30_window_start(uint64_t id,uint64_t factory,uint64_t admitted_us,uint32_t duration_ms,uint32_t count);
void n30_window_stop();
void n30_window_poll();
bool n30_window_ack(uint32_t window);
void n30_window_capture(const v12::periodic_rx::Stamp&,const adv_rx::BleAnchor&,int8_t rssi);
void n30_window_pair(const v12::n30_probe::Pair&);
void n30_window_report(const v12::Report&);
void n30_window_frame(v12::ConsoleFrame&,bool edges,uint32_t after,bool late=false);

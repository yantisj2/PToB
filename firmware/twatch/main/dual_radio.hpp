// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "dual_clock.hpp"
#include "fine_capture.hpp"
#include "g1_pps_diagnostic.hpp"
namespace v12 {struct ConsoleFrame;}
// Atomic worker snapshot and its own XTAL/CURRENT bracket, preserving revoke
// fences. This adds no peripheral accesses from the main task or BLE owner.
void watch_lora_dual_snapshot(n30_lora_view&,watch::dual::LoRaBridge&);
namespace watchdual {
void init(uint64_t boot,bool (*read20)(uint64_t*));
void poll(uint64_t now);
void edge(const WatchPpsCapture&,const v12::G1GpsShadow::Reference&,bool fix);
void reference_lost();
void frame(v12::ConsoleFrame&,uint32_t after);
void snapshot_frame(v12::ConsoleFrame&);
watch::dual::ClockView clock();
}

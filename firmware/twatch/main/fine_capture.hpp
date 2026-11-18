// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include "fine_window.hpp"
namespace v12 {struct ConsoleFrame;}
// Default OFF. No pins or peripheral are initialized before a bound START.
bool n30_fine_start(uint64_t id,uint64_t factory,uint64_t now_us,uint32_t duration_ms,uint32_t every);
// Watch synthetic probes are unsupported; only finite native RX17 is routed.
bool n30_fine_probe(uint64_t id,uint64_t factory,uint64_t now_us,uint32_t spacing_ms,uint32_t total_edges);
void n30_fine_stop();
void n30_fine_poll(uint64_t now_us);
void n30_fine_frame(v12::ConsoleFrame&,uint32_t after,uint64_t now_us);

// Maintenance must include every partial-start/failed-stop resource.
bool n30_fine_released();
bool n30_fine_cleanup_fault();
// Cached owner state only: never starts a timer, latches CURRENT or drives pins.
struct N30FineClockStatus {uint64_t until_us=0;uint32_t hz=0;bool active=false,healthy=false,production=false;};
N30FineClockStatus n30_fine_clock_status(uint64_t now_us);
// Fixed RW packet-window session, exclusive with ordinary fine/probe leases.
// The bridge locates candidates only; it is never a fine phase clock map.
bool n30_fine_window_start(uint64_t id,uint64_t factory,uint64_t now_us,uint32_t duration_ms);
void n30_fine_window_stop();
bool n30_fine_window_bridge(bool (*read20)(uint64_t*),n30::fine::Bridge&);
bool n30_fine_window_begin(uint32_t id,uint64_t lower80,uint64_t upper80,uint32_t incarnation);
bool n30_fine_window_refuse(uint32_t id);
bool n30_fine_window_rearm();
n30::fine::WindowStatus n30_fine_window_status();
unsigned n30_fine_window_read(uint32_t after,n30::fine::Edge* out,unsigned capacity);
unsigned n30_fine_window_late(uint32_t after,n30::fine::Window::Late* out,unsigned capacity,n30::fine::WindowStatus* snapshot=nullptr);

// Independent GPIO13 physical PPS input on the otherwise unused CH1. It never
// enters the radio history, packet association or production actuator.
struct WatchPpsCapture {uint64_t tick20=0,seen_us=0;uint32_t number=0,incarnation=0,enclosure_ns=UINT32_MAX;bool valid=false;};
bool watch_pps_start(uint64_t boot,uint64_t factory,uint64_t now);
bool watch_pps_stop();
bool watch_pps_healthy();
bool watch_pps_take(WatchPpsCapture&,bool (*read20)(uint64_t*));
// Read-only main-owner bridge: XTAL20 encloses the actual fine CURRENT latch,
// before software extension/receiver bookkeeping. It starts no resources.
struct WatchClockBridge {uint64_t fine20=0,xtal_before20=0,xtal_after20=0;uint32_t incarnation=0;bool valid=false;};
bool watch_clock_bridge(bool (*read_xtal20)(uint64_t*),WatchClockBridge&);

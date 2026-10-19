// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "fault_watchdog.hpp"
namespace v12 {
inline uint32_t fault_watchdog_stalled(uint64_t now,uint64_t main_at,uint64_t host_at,bool host_seen,
                                     uint64_t display_at,bool display_seen){
  const auto stale=[&](uint64_t at){return now>=at&&now-at>=2000000;};
  return (stale(main_at)?FaultMainStalled:0u)|(host_seen&&stale(host_at)?FaultHostStalled:0u)|
    (display_seen&&stale(display_at)?FaultDisplayStalled:0u);
}
}

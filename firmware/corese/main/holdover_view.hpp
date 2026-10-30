// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace corese {
struct HoldoverView {
 uint64_t last20=0,age20=0,max_gap20=0,witness_age_us=UINT64_MAX;
 uint64_t p95_ns=UINT64_MAX,p99_ns=UINT64_MAX,max_ns=UINT64_MAX,run_max_ns=0;
 uint32_t samples=0,started=0,completed=0,aborted=0,gaps=0,generation=0;
 uint32_t duration_ms=0,probe_ms=12000,trials=0,bad=0,unknown=0;
 bool holding=false,ready=false,lower_bound=false,rate_selected=false;
};
}

// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace w12 {
struct Rc9SourceDecision {
 bool select_rc8=false,admit_rc9=false;
};
// A demand deadline is a real authenticated lease, never a display cache or a
// fresh-now extension. Existing RC9 children retain their source class through
// BLE recovery and RC8 gaps. Qualified normal BLE takes the local display and
// stops new admissions/renewals, allowing the original child leases to drain.
inline Rc9SourceDecision rc9_source(bool ble_lock,bool rc8_current,
 uint64_t client_until,uint64_t now,bool permit=true){
 if(!permit)return {};
 const bool admit=rc8_current&&!ble_lock;
 return {admit||client_until>now,admit};
}
}

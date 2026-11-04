// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include "lora_view.h"
namespace w12 {
// RC9 wire1 still requires an original signed source envelope and its12s
// physical hold. Passive schedule8 observations supply neither a substitute
// source grant nor permission to invent created/expires or renew by polling.
inline bool rc9_original_envelope(const w12_lora_view& l){
 return l.edge_us&&l.until_us>l.edge_us&&l.until_us-l.edge_us<=12000000&&
  l.event_ns&&l.created_ns<=l.event_ns&&l.event_ns<l.expires_ns;
}
struct Rc9SourceDecision {
 bool client_hold=false,select_rc8=false,display_rc8=false,admit_rc9=false,serve_rc9=false,allow_rc7=false;
};
// A demand deadline is a real authenticated lease, never a display cache or a
// fresh-now extension. Existing RC9 children retain their source class through
// BLE recovery and RC8 gaps. Qualified normal BLE takes the local display and
// stops new admissions/renewals, allowing the original child leases to drain.
inline Rc9SourceDecision rc9_source(bool ble_lock,bool rc8_current,bool rc8_active,
 uint64_t client_until,uint64_t now,bool permit=true){
 Rc9SourceDecision d;
 if(!permit)return d;
 d.client_hold=client_until>now;
 d.select_rc8=d.client_hold||(!ble_lock&&rc8_active);
 d.display_rc8=d.select_rc8&&!ble_lock;
 d.admit_rc9=d.select_rc8&&!ble_lock;
 d.serve_rc9=d.select_rc8&&rc8_current;
 d.allow_rc7=ble_lock&&!d.select_rc8;
 return d;
}
}

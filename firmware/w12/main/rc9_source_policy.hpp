// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace w12 {
struct Rc9SourceDecision {
 bool client_hold=false,select_rc8=false,serve_rc9=false,allow_rc7=false;
};
// A demand deadline is a real authenticated lease, never a display cache or a
// fresh-now extension. A current RC9 lease pins the source class through BLE
// recovery and RC8 gaps. A gap withdraws TIME, while RX keeps seeking RC8.
inline Rc9SourceDecision rc9_source(bool ble_lock,bool rc8_current,bool rc8_active,
 uint64_t client_until,uint64_t now,bool permit=true){
 Rc9SourceDecision d;
 if(!permit)return d;
 d.client_hold=client_until>now;
 d.select_rc8=d.client_hold||(!ble_lock&&rc8_active);
 d.serve_rc9=d.select_rc8&&rc8_current;
 d.allow_rc7=ble_lock&&!d.select_rc8;
 return d;
}
}

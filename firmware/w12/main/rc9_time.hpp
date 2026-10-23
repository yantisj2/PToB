// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "sync_watchdog.hpp"
namespace w12 {
inline bool rc9_time(const v12::Rc9Status& s,uint64_t now,uint64_t& utc){
 utc=0;
 if(!s.valid||!s.following||s.fault||!s.server||!s.server_boot||!s.root||!s.source_epoch||!s.sequence||!s.utc_us||
    now<s.sampled_us||now-s.sampled_us>=SyncSnapshotMaxUs||now>=s.until_us||now-s.sampled_us>UINT64_MAX-s.utc_us)return false;
 utc=s.utc_us+now-s.sampled_us;return true;
}
}

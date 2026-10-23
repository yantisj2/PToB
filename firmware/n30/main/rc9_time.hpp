// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "ui.hpp"
#include "native_r2.hpp"
#include <algorithm>
#include <cstdio>
namespace n30 {
inline bool rc9_time(const v12::Rc9Status& s,uint64_t now,uint64_t& utc){
 utc=0;
 if(!s.following||!s.valid||s.fault||!s.server||!s.server_boot||!s.root||!s.source_epoch||!s.sequence||!s.utc_us||
    now<s.sampled_us||now-s.sampled_us>=SnapshotMaxUs||now>=s.until_us||now-s.sampled_us>UINT64_MAX-s.utc_us)return false;
 utc=s.utc_us+now-s.sampled_us;return true;
}
// Settled normal BLE and direct RC8 retain priority. This separate functional
// stream supplies UTC, never BLE fit/precision/lock or downstream permission.
inline void select_rc9(Model& m,const v12::Rc9Status& s,uint64_t now,const char* server=nullptr,const char* root=nullptr){
 uint64_t utc=0;if(!rc9_time(s,now,utc)||m.mode_pending||m.state==State::Fault||m.state==State::Off)return;
 if(m.source==Source::LoRa&&time(m,now,utc))return;
 if(m.source!=Source::Rc9&&m.locked&&saver_time(m,now,utc))return;
 const auto fresh=s.sampled_us>UINT64_MAX-SnapshotMaxUs?UINT64_MAX:s.sampled_us+SnapshotMaxUs;
 m.utc_at_us=s.sampled_us;m.utc_us=s.utc_us;m.until_us=std::min(s.until_us,fresh);
 m.valid=true;m.locked=false;m.state=State::Follow;m.source=Source::Rc9;m.hz=0;
 m.serving=false;m.delivery=UINT16_MAX;m.rms_ns=UINT32_MAX;m.precision={};m.signal={};
 std::snprintf(m.parent,sizeof m.parent,"%s",server?server:"RC9");std::snprintf(m.root,sizeof m.root,"%s",root?root:"----");
}
}

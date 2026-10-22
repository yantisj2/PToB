// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "native_r2.hpp"
#include "dual_time.hpp"
#include <algorithm>
namespace n30 {
// Radio policy reads actual current BLE quality independently of rendering,
// displayed lock settlement, blank/minimal mode, or OLED initialization.
inline bool ble_locked(const v12::R2Status& s,uint64_t now){
 if(!s.requested||s.fault||s.rx_capture.fault||!s.following||!s.clock_valid||s.g1_retained_estimate||
    s.rc7_upstream_mode_pending||s.rc_parent.pending()||s.rc_parent.state==4||
    !s.root_identity||!s.root_boot||!s.source_epoch||!s.publication||
    now<s.follower_snapshot_us||now-s.follower_snapshot_us>=SnapshotMaxUs)return false;
 const auto until=std::min(s.service_until_us,std::min(s.g1_anchor_deadline_us,s.g1_corrected_until_us));
 const auto tick=s.g1_relative_quality.evaluated20;
 return until>now&&s.g1_relative_quality.locked(tick,s.g1_clock_model)&&
   s.g1_relative_quality.hold_ms(tick,s.g1_clock_model,until-now)>=1000;
}
// Current synchronized UTC, independent of precision lock and OLED caches.
// These are the original source/worker deadlines, never now+hold on polling.
inline uint64_t synchronized_until(const v12::R2Status& s,const n30_lora_view& l,uint64_t now){
 uint64_t until=0;
 if(s.requested&&!s.fault&&s.following&&s.clock_valid&&!s.g1_retained_estimate&&
    !s.rc7_upstream_mode_pending&&!s.rc_parent.pending()&&s.rc_parent.state!=4&&
    s.root_identity&&s.root_boot&&s.source_epoch&&s.publication&&s.follower_utc_us&&
    now>=s.follower_snapshot_us&&now-s.follower_snapshot_us<SnapshotMaxUs){
  const auto fresh=s.follower_snapshot_us>UINT64_MAX-SnapshotMaxUs?UINT64_MAX:s.follower_snapshot_us+SnapshotMaxUs;
  until=std::min(fresh,std::min(s.g1_anchor_deadline_us,s.service_until_us));
 }
 uint64_t utc=0;
 if(lora_time(l,now,utc)){
  const auto fresh=l.sampled_us>UINT64_MAX-SnapshotMaxUs?UINT64_MAX:l.sampled_us+SnapshotMaxUs;
  until=std::max(until,std::min(fresh,l.until_us));
 }
 return until>now?until:0;
}
}

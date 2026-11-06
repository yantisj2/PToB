// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "native_r2.hpp"
#include "g1_ble_eligibility.hpp"
#include "dual_time.hpp"
#include "rc9_time.hpp"
#include "no_time_retention.hpp"
#include <algorithm>
namespace n30 {
// Radio policy reads actual current BLE quality independently of rendering,
// displayed lock settlement, blank/minimal mode, or OLED initialization.
using ptob_ble::BleLockRefusal;
using ptob_ble::BleReady;
using ptob_ble::BleDisabled;
using ptob_ble::BleFault;
using ptob_ble::BleMode;
using ptob_ble::BleNotFollowing;
using ptob_ble::BleContext;
using ptob_ble::BleSnapshot;
using ptob_ble::BleCoarseExpiry;
using ptob_ble::BleCorrectedExpiry;
using ptob_ble::BleReplayExpiry;
using ptob_ble::BleQuality;
using ptob_ble::BleForecast;
using ptob_ble::BleRetained;
using ptob_ble::ble_lock_refusal;
using ptob_ble::ble_lock_reason;
inline bool ble_locked(const v12::R2Status& s,uint64_t now){return ble_lock_refusal(s,now)==BleReady;}
// Current synchronized UTC, independent of precision lock and OLED caches.
// These are the original source/worker deadlines, never now+hold on polling.
inline uint64_t synchronized_until(const v12::R2Status& s,const n30_lora_view& l,uint64_t now,const v12::Rc9Status& r={}){
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
 if(s.requested&&!s.fault&&!s.rc7_upstream_mode_pending&&rc9_time(r,now,utc)){
  const auto fresh=r.sampled_us>UINT64_MAX-SnapshotMaxUs?UINT64_MAX:r.sampled_us+SnapshotMaxUs;
  until=std::max(until,std::min(fresh,r.until_us));
 }
 return until>now?until:0;
}
}

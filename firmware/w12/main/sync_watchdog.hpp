// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "native_r2.hpp"
#include <algorithm>
namespace w12 {
constexpr uint64_t SyncSnapshotMaxUs=250000;
inline bool ble_locked(const v12::R2Status& s,uint64_t now){
 if(!s.requested||s.fault||s.rx_capture.fault||!s.following||!s.clock_valid||s.g1_retained_estimate||
    s.rc7_upstream_mode_pending||s.rc_parent.pending()||s.rc_parent.state==4||
    !s.root_identity||!s.root_boot||!s.source_epoch||!s.publication||now<s.follower_snapshot_us||now-s.follower_snapshot_us>=SyncSnapshotMaxUs)return false;
 // Coarse UTC permission must be live NOW. It renews independently of the
 // precision model and is not the horizon of a one-second quality forecast.
 // Capping that forecast by each short coarse renewal made healthy BLE lock
 // oscillate and prevented AUTO's continuous recovery from completing.
 if(s.service_until_us<=now||s.g1_corrected_until_us<=now)return false;
 // Match the sole G1 controller's lock_protected_quality(): current packet
 // permission must be live now; the quality forecast uses the original
 // replay anchor. Short corrected-pair renewals cannot make a healthy stream
 // alternate between BLE and RC8. This grants no future output permission.
 const auto until=s.g1_anchor_deadline_us;
 const auto tick=s.g1_relative_quality.evaluated20;
 return until>now&&s.g1_relative_quality.locked(tick,s.g1_clock_model)&&s.g1_relative_quality.hold_ms(tick,s.g1_clock_model,until-now)>=1000;
}
// Authenticated current time, independent of fine lock and display caches.
// Polls retain the original source and worker deadlines; HOLD cannot feed it.
inline uint64_t synchronized_until(const v12::R2Status& s,uint64_t now){
  if(!s.requested||s.fault||!s.following||!s.clock_valid||s.g1_retained_estimate||
     s.rc7_upstream_mode_pending||s.rc_parent.pending()||s.rc_parent.state==4||
     !s.root_identity||!s.root_boot||!s.source_epoch||!s.publication||!s.follower_utc_us||
     now<s.follower_snapshot_us||now-s.follower_snapshot_us>=SyncSnapshotMaxUs)return 0;
  const auto fresh=s.follower_snapshot_us>UINT64_MAX-SyncSnapshotMaxUs?UINT64_MAX:s.follower_snapshot_us+SyncSnapshotMaxUs;
  const auto until=std::min(fresh,std::min(s.g1_anchor_deadline_us,s.service_until_us));
  return until>now?until:0;
}
}

#ifdef PTOB_W12_LORA_RX
#include "lora_view.h"
namespace w12 {
inline bool lora_time(const w12_lora_view& l,uint64_t now,uint64_t& utc){
 utc=0;
 // Match N30's functional RC8 display/watchdog path. Authenticated physical
 // pairs may arrive before their companion fills in boots/quality. The core
 // enforces withdrawal, replay and its original hold; missing metadata never
 // grants source authority or an RF/UTC bound, nor trains/publishes BLE time.
 if(!l.requested||!l.active||l.fault||l.state!=2||!l.utc_us||!l.until_us||!l.emitter||!l.root||!l.source_epoch||now<l.sampled_us||now-l.sampled_us>=SyncSnapshotMaxUs||now>=l.until_us)return false;
 const auto age=now-l.sampled_us;if(l.utc_us>UINT64_MAX-age)return false;
 utc=l.utc_us+age;return true;
}
inline uint64_t synchronized_until(const v12::R2Status& s,const w12_lora_view& l,uint64_t now){
 auto until=synchronized_until(s,now);uint64_t utc=0;
 if(lora_time(l,now,utc))until=std::max(until,std::min(l.until_us,l.sampled_us+SyncSnapshotMaxUs));
 return until>now?until:0;
}
}
#endif

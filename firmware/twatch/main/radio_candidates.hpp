// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "mobile_time.hpp"
#include "status_adapter.hpp"
#include "sync_watchdog.hpp"
#include "empirical_holdover.hpp"
namespace watch {
inline Candidate ble_candidate(const v12::R2Status& s,const DisplayState& d,uint64_t now,bool parked,const HoldoverView& hold={}){
 Candidate c;auto& w=c.clock;
 const bool healthy=s.requested&&!parked&&!s.fault&&!s.rc7_upstream_mode_pending&&!s.rc_parent.pending()&&s.rc_parent.state!=4;
 const bool current=healthy&&s.clock_valid&&s.follower_utc_us&&now>=s.follower_snapshot_us&&now-s.follower_snapshot_us<250000;
 const bool following=current&&!s.g1_retained_estimate&&s.following&&s.root_identity&&s.root_boot&&s.source_epoch&&s.publication&&now<s.g1_anchor_deadline_us&&now<s.service_until_us;
 w.utc_us=s.follower_utc_us;w.sampled_us=s.follower_snapshot_us;w.until_us=now+250000;
 w.time_valid=current;w.local_time=!following;w.holding=current&&!following;
 if(following)w.until_us=std::min(w.until_us,std::min(s.g1_anchor_deadline_us,s.service_until_us));
 w.source=s.rc7_bound?(s.rc7_depth==1?Source::RC1:s.rc7_depth==2?Source::RC2:Source::None):Source::BLE;
 if(s.rc7_bound&&w.source==Source::None)w.time_valid=false;
 w.locked=following&&n30::ble_locked(s,now)&&d.r2_relative_lock;
 w.rms_ns=following&&s.g1_clock_model.tick&&now<s.g1_corrected_until_us?d.rms_ns:UINT32_MAX;
 w.hold_valid=following&&hold.ready&&hold.trials>=EmpiricalHoldover::Minimum&&hold.duration_ms;
 w.hold_ms=hold.duration_ms;w.hold_trials=hold.trials;w.hold_lower=hold.lower_bound;
 c.received_us=s.follower_received_us;c.pairs=s.g1_corrected_accepted;
 c.sync_failures=s.g1_window_misses;c.setup_failures=s.setup_retries;c.losses=s.g1_receive_loss.expiries;
 if(healthy&&s.n30_signal.fresh(now))w.source_rssi=s.n30_signal.dbm;
 c.root=s.root_identity;c.boot=s.root_boot;c.epoch=s.source_epoch;c.fresh=following;return c;
}
inline Candidate lora_candidate(const n30_lora_view& l,uint64_t now){
 Candidate c;auto& w=c.clock;w.source=Source::LOR;w.lora_on=l.requested&&l.active&&!l.fault;
 // Fresh authenticated timing-pair reception, independent of source selection.
 w.lora_receiving=w.lora_on&&now>=l.sampled_us&&now-l.sampled_us<250000&&
  ((l.watch_pairs&&l.watch_age_ms<35000)||(l.accepted&&l.age_ms<35000));
 if(w.lora_receiving&&l.rssi_dbm>-127&&l.rssi_dbm<=20)w.source_rssi=l.rssi_dbm;
 uint64_t utc=0;const bool strict=n30::lora_time(l,now,utc);
 uint64_t observer_utc=0;const bool observer=n30::lora_watch_time(l,now,observer_utc);
 if(!strict)utc=observer_utc;
 w.time_valid=strict||observer;w.utc_us=utc;w.sampled_us=now;
 w.until_us=strict?std::min(l.until_us,now+250000):now+250000;
 const auto age=observer?l.watch_age_ms:l.age_ms;
 c.fresh=w.time_valid&&age<35000;w.holding=w.time_valid&&!c.fresh;
 w.local_time=w.holding;w.locked=strict&&c.fresh;
 // This prediction remains observational even when its numerical RMS is low.
 w.rms_ns=c.fresh&&l.watch_prediction_n?l.watch_rms_ns:UINT32_MAX;
 c.root=l.root;c.boot=l.root_boot;c.epoch=l.source_epoch;return c;
}
}

// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "mobile_time.hpp"
#include "status_adapter.hpp"
#include "sync_watchdog.hpp"
#include "empirical_holdover.hpp"
#include <new>
namespace watch {
inline void fill_ble_candidate(Candidate& c,const v12::R2Status& s,const DisplayState& d,uint64_t now,bool parked,const HoldoverView& hold={}){
 new(&c) Candidate;auto& w=c.clock;
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
 w.rms_sampled_us=w.sampled_us;w.rms_until_us=std::min(w.until_us,s.g1_corrected_until_us);w.rms_source=Source::BLE;
 w.hold_sampled_us=w.sampled_us;w.hold_until_us=w.until_us;
 w.hold_valid=following&&hold.ready&&hold.trials>=EmpiricalHoldover::Minimum&&hold.duration_ms;
 w.hold_ms=hold.duration_ms;w.hold_trials=hold.trials;w.hold_lower=hold.lower_bound;
 c.received_us=s.follower_received_us;c.pairs=s.g1_corrected_accepted;
 c.sync_failures=s.g1_window_misses;c.setup_failures=s.setup_retries;c.losses=s.g1_receive_loss.expiries;
 if(healthy&&s.n30_signal.fresh(now))w.source_rssi=s.n30_signal.dbm;
 c.root=s.root_identity;c.boot=s.root_boot;c.epoch=s.source_epoch;c.fresh=following;
}
inline void fill_lora_candidate(Candidate& c,const n30_lora_view& l,uint64_t now){
 new(&c) Candidate;auto& w=c.clock;w.source=Source::LOR;w.lora_on=l.requested&&l.active&&!l.fault;
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
 w.rms_sampled_us=w.sampled_us;w.rms_until_us=w.until_us;w.rms_source=Source::LOR;
 c.root=l.root;c.boot=l.root_boot;c.epoch=l.source_epoch;
}
// Presentation only: prefer a current BLE fit even while LoRa supplies time.
// Lock qualification is deliberately separate from displaying a finite fit.
inline void receiver_metrics(Model& out,const Candidate& ble,const Candidate& lora,uint64_t now){
 const Model* metric=ble.fresh&&receiver_rms_current(ble.clock,now)?&ble.clock:
  lora.fresh&&receiver_rms_current(lora.clock,now)?&lora.clock:nullptr;
 out.rms_ns=metric?metric->rms_ns:UINT32_MAX;out.rms_source=metric?rms_receiver(*metric):Source::None;
 out.rms_sampled_us=metric?metric->rms_sampled_us:now;out.rms_until_us=metric?metric->rms_until_us:now;
 out.hold_valid=ble.fresh&&sampled_hold(ble.clock,now);
 out.hold_sampled_us=ble.clock.hold_sampled_us;out.hold_until_us=ble.clock.hold_until_us;
 out.hold_ms=out.hold_valid?ble.clock.hold_ms:0;out.hold_trials=out.hold_valid?ble.clock.hold_trials:0;out.hold_lower=ble.clock.hold_lower;
}
inline Candidate ble_candidate(const v12::R2Status& s,const DisplayState& d,uint64_t now,bool parked,const HoldoverView& hold={}){Candidate c;fill_ble_candidate(c,s,d,now,parked,hold);return c;}
inline Candidate lora_candidate(const n30_lora_view& l,uint64_t now){Candidate c;fill_lora_candidate(c,l,now);return c;}
}

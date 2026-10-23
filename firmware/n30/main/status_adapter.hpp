// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "ui.hpp"
#include "native_r2.hpp"
#include "display.hpp"
#include <algorithm>
#include <cstdio>
#include <cmath>
namespace n30 {
inline Model model(const v12::R2Status& s,const DisplayState& d,uint64_t now,const char* root_alias=nullptr){
 Model m;std::snprintf(m.unit,sizeof m.unit,"%s",d.unit_label);std::snprintf(m.parent,sizeof m.parent,"%s",d.followed_clock);
 if(s.root_identity&&root_alias)std::snprintf(m.root,sizeof m.root,"%s",root_alias);
 m.sampled_us=now;m.utc_at_us=s.follower_snapshot_us;m.utc_us=s.follower_utc_us;
 m.until_us=now<=UINT64_MAX-SnapshotMaxUs?now+SnapshotMaxUs:now;
 const bool healthy=s.requested&&!s.fault&&!s.rc7_upstream_mode_pending;
 m.relay_only=s.rc7_relay_only_requested;m.mode_pending=s.rc7_upstream_mode_pending;
 m.valid=healthy&&s.clock_valid&&d.valid&&(!s.following||(s.root_identity&&s.root_boot&&s.source_epoch&&s.publication));
 m.state=s.fault?State::Fault:!s.requested?State::Off:s.rc7_upstream_mode_pending?State::Acquire:
   m.valid?(s.following?State::Follow:State::Hold):(s.connecting||s.connections)?State::Join:s.group_member?State::Acquire:State::Search;
 if(m.state==State::Follow){m.until_us=std::min(m.until_us,std::min(s.g1_anchor_deadline_us,s.g1_corrected_until_us));}
 m.locked=healthy&&s.following&&m.valid&&d.r2_relative_lock&&!s.g1_retained_estimate;
 if(m.valid&&s.following){
  const bool relayed=s.rc7_bound&&s.joined_peer&&s.joined_boot&&(s.rc7_depth==1||s.rc7_depth==2);
  m.source=relayed?(s.rc7_depth==1?Source::Rc1:Source::Rc2):s.rc7_bound?Source::None:Source::Ble;
 }
 m.rms_ns=m.valid&&s.following?d.rms_ns:UINT32_MAX;
 // The dashboard's settled causal quality belongs to the active BLE model.
 // Neither the strict-discipline counters nor an external PPS witness replace it.
 auto& p=m.precision;p.until_us=m.until_us;p.scope=s.g1_clock_origin;
 p.root_boot=s.root_boot;p.source_epoch=s.source_epoch;p.quality_scope=s.g1_relative_quality.scope_generation;
 p.valid=m.valid&&s.following&&s.g1_clock_model.tick;
 p.rms_ns=p.valid?m.rms_ns:UINT32_MAX;p.locked=p.valid&&m.locked;p.source=m.source;
 m.hz=s.following?s.precision_hz:0;
 m.serving=healthy&&m.valid&&s.following&&(s.rc7_server_flags&(1u<<4));
 // Delivery denominator is available for admitted RC7 paths only. Scan/PA
 // callback counts cannot manufacture a direct-GM delivery percentage.
 if(s.following&&s.rc7_bound)m.delivery=std::min<uint16_t>(s.rc7_delivery,1000);
 m.ble={m.utc_at_us,m.utc_us,m.until_us,s.root_identity,s.root_boot,s.source_epoch,m.valid,m.state==State::Follow};
 return m;
}
// Read-only causal trend, never the raw oscillator correction or forecast guard.
// The producer owns the baseline/expiry; the UI additionally binds its scope
// to this fresh following BLE model and refuses stale/future observations.
inline void relative_rate(Model& m,uint64_t root,uint64_t boot,uint32_t epoch,
 int32_t ppb,bool valid,uint64_t sampled_us,uint64_t now){
 auto& p=m.precision;
 p.relative_known=valid&&precision_current(m,now)&&m.ble.following&&
  root&&root==m.ble.root&&boot&&boot==m.ble.boot&&epoch&&epoch==m.ble.epoch&&
  now>=sampled_us&&now-sampled_us<SnapshotMaxUs;
 if(p.relative_known)p.relative_ppb=ppb;
}
}

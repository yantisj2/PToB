// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "sync_watchdog.hpp"
#include <cassert>
int main(){
 v12::R2Status s{};n30_lora_view l{};constexpr uint64_t now=1000000;
 s.requested=s.clock_valid=s.following=true;s.root_identity=1;s.root_boot=2;s.source_epoch=3;s.publication=4;
 s.follower_snapshot_us=now;s.follower_utc_us=1700000000000000;s.g1_anchor_deadline_us=now+2000000;s.service_until_us=now+1000000;
 assert(n30::synchronized_until(s,l,now)==now+n30::SnapshotMaxUs); // No precision lock required.
 assert(n30::synchronized_until(s,l,now+n30::SnapshotMaxUs)==0); // Polls cannot renew stale snapshots.
 assert(!n30::ble_locked(s,now)); // Coarse FOLLOW is not a radio lock.
 auto locked=s;locked.g1_corrected_until_us=now+2000000;
 locked.g1_clock_model={now*20,int64_t(now*1000),0,0,0};
 auto& q=locked.g1_relative_quality;q.evaluated20=q.original20=now*20;q.samples=16;q.rms_ns=50;q.max_ns=100;q.rate_guard_ppb=20;q.accel_guard_ppb_s=10;
 assert(n30::ble_locked(locked,now));
 assert(!n30::ble_locked(locked,now+n30::SnapshotMaxUs));
 locked.following=false;assert(!n30::ble_locked(locked,now));locked.following=true;
 locked.g1_retained_estimate=true;assert(!n30::ble_locked(locked,now));locked.g1_retained_estimate=false;
 locked.g1_corrected_until_us=now;assert(!n30::ble_locked(locked,now));
 auto valid=s;s.service_until_us=now+1;assert(n30::synchronized_until(s,l,now)==now+1);
 assert(n30::synchronized_until(s,l,now+1)==0);s=valid;
 for(unsigned failure=0;failure<6;failure++){
  s=valid;
  if(failure==0)s.following=false;if(failure==1)s.g1_retained_estimate=true;
  if(failure==2)s.root_boot=0;if(failure==3)s.fault=true;if(failure==4)s.rc7_upstream_mode_pending=true;if(failure==5)s.follower_snapshot_us=now+1;
  assert(n30::synchronized_until(s,l,now)==0);
 }
 s=valid;s.following=false;
 l.sampled_us=now;l.utc_us=1700000000000000;l.until_us=now+12000000;l.requested=l.active=true;l.state=2;
 assert(n30::synchronized_until(s,l,now)==now+n30::SnapshotMaxUs);
 l.until_us=now+1;assert(n30::synchronized_until(s,l,now)==now+1);
 l.state=1;assert(n30::synchronized_until(s,l,now)==0);l.state=2;l.fault=true;assert(n30::synchronized_until(s,l,now)==0);
 // A failed/expired LoRa stream cannot hide still-current BLE time.
 s=valid;assert(n30::synchronized_until(s,l,now)==now+n30::SnapshotMaxUs);
}

// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "sync_watchdog.hpp"
#include <cassert>
int main(){
  v12::R2Status s{};constexpr uint64_t now=1000000;
  s.requested=s.clock_valid=s.following=true;s.root_identity=1;s.root_boot=2;
  s.source_epoch=3;s.publication=4;s.follower_snapshot_us=now;s.follower_utc_us=1700000000000000;
  s.g1_anchor_deadline_us=now+2000000;s.service_until_us=now+1000000;
  assert(w12::synchronized_until(s,now)==now+w12::SyncSnapshotMaxUs);
  assert(w12::synchronized_until(s,now+w12::SyncSnapshotMaxUs)==0);
  const auto valid=s;
  for(unsigned failure=0;failure<10;++failure){
    s=valid;
    if(failure==0)s.following=false;
    if(failure==1)s.g1_retained_estimate=true;
    if(failure==2)s.root_boot=0;
    if(failure==3)s.fault=true;
    if(failure==4)s.rc7_upstream_mode_pending=true;
    if(failure==5)s.follower_snapshot_us=now+1;
    if(failure==6)s.service_until_us=now;
    if(failure==7)s.g1_anchor_deadline_us=now;
    if(failure==8)s.clock_valid=false;
    if(failure==9)s.publication=0;
    assert(w12::synchronized_until(s,now)==0);
  }
#ifdef PTOB_W12_LORA_RX
  w12_lora_view l{};l.active=l.requested=l.time_valid=true;l.state=2;l.emitter=1;l.root=2;l.root_boot=3;l.source_epoch=4;l.utc_us=1700000000000000;l.sampled_us=now;l.until_us=now+12000000;
  v12::R2Status lost;uint64_t utc=0;
  assert(w12::lora_time(l,now,utc)&&utc==l.utc_us);
  assert(w12::synchronized_until(lost,l,now)==now+250000);
  assert(!w12::synchronized_until(lost,l,now+250000));
  l.fault=true;assert(!w12::synchronized_until(lost,l,now));l.fault=false;
  l.until_us=now;assert(!w12::synchronized_until(lost,l,now));
#endif
  s=valid;s.service_until_us=now+1;
  assert(w12::synchronized_until(s,now)==now+1);
  assert(w12::synchronized_until(s,now+1)==0);
  // Actual precision lock may coexist with a short, still-live UTC envelope.
  // Its one-second forecast uses original precision/replay deadlines, while
  // an expired coarse source still revokes lock immediately.
  s=valid;s.g1_anchor_deadline_us=s.g1_corrected_until_us=now+5000000;
  auto& q=s.g1_relative_quality;q.original20=q.evaluated20=20000000;q.samples=16;
  q.rms_ns=100;q.max_ns=300;q.rate_guard_ppb=20;
  s.g1_clock_model={20000000,10000000000,0,0,0};
  s.service_until_us=now+100000;
  assert(w12::ble_locked(s,now));
  assert(!w12::ble_locked(s,now+100000));
  s.service_until_us=now+5000000;s.g1_corrected_until_us=now+999999;
  assert(!w12::ble_locked(s,now));
  s.g1_corrected_until_us=now+5000000;q.max_ns=1000;
  assert(!w12::ble_locked(s,now));
}

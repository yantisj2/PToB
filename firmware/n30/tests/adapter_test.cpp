// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "status_adapter.hpp"
#include <cassert>
int main(){
 v12::R2Status s;DisplayState d;d.unit_label="N3-1";d.followed_clock="TC-02";
 s.requested=s.clock_valid=s.following=true;d.valid=d.r2_relative_lock=true;d.rms_ns=65;
 s.root_identity=1;s.root_boot=2;s.source_epoch=3;s.publication=4;
 s.follower_snapshot_us=1000000;s.follower_utc_us=123456789;s.g1_anchor_deadline_us=1200000;s.g1_corrected_until_us=1100000;
 auto m=n30::model(s,d,1000001);assert(m.valid&&m.locked&&m.until_us==1100000);assert(m.delivery==UINT16_MAX);
 uint64_t utc;assert(n30::time(m,1000100,utc));assert(!n30::time(m,1100000,utc));
 s.rc7_bound=true;s.rc7_delivery=950;m=n30::model(s,d,1000001);assert(m.delivery==950);
 s.rc7_server_flags=16;m=n30::model(s,d,1000001);assert(m.serving);
 s.following=false;s.g1_retained_estimate=true;m=n30::model(s,d,1000001);assert(m.valid&&!m.locked&&!m.serving&&m.state==n30::State::Hold&&m.rms_ns==UINT32_MAX);
 s.fault=true;m=n30::model(s,d,1000001);assert(!m.valid&&!m.locked&&m.state==n30::State::Fault);
 s.fault=false;s.requested=false;m=n30::model(s,d,1000001);assert(!m.valid&&m.state==n30::State::Off);
 s.requested=s.following=true;s.root_boot=0;m=n30::model(s,d,1000001);assert(!m.valid&&!m.locked);
 s.root_boot=2;s.rc7_upstream_mode_pending=true;m=n30::model(s,d,1000001);assert(!m.valid&&!m.locked&&m.state==n30::State::Acquire);
}

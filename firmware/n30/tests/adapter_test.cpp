// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "status_adapter.hpp"
#include <cassert>
#include <cstring>
int main(){
 v12::R2Status s;DisplayState d;d.unit_label="N3-1";d.followed_clock="TC-02";
 s.requested=s.clock_valid=s.following=true;d.valid=d.r2_relative_lock=true;d.rms_ns=65;
 s.root_identity=1;s.root_boot=2;s.source_epoch=3;s.publication=4;
 s.follower_snapshot_us=1000000;s.follower_utc_us=123456789;s.g1_anchor_deadline_us=1200000;s.g1_corrected_until_us=1100000;
 auto m=n30::model(s,d,1000001);assert(m.valid&&m.locked&&m.until_us==1100000);assert(m.delivery==UINT16_MAX&&m.source==n30::Source::Ble);
 // Direct GM signal is current-parent RF, independent of delivery/clock fit.
 s.synced=true;s.joined_peer=1;s.n30_signal={1,1000000,7,-45};m=n30::model(s,d,1000001);assert(n30::rf_bars(m,1000001)==4);
 for(unsigned reason=0;reason<6;reason++){
  auto bad=s;if(reason==0)bad.n30_signal.peer=9;if(reason==1)bad.n30_signal.dbm=127;if(reason==2)bad.synced=false;
  if(reason==3)bad.fault=true;if(reason==4)bad.rc7_upstream_mode_pending=true;if(reason==5)bad.n30_signal.at=1000002;
  auto unknown=n30::model(bad,d,1000001);assert(n30::rf_bars(unknown,1000001)==0);
 }
 auto hold=s;hold.following=false;hold.g1_retained_estimate=true;auto hm=n30::model(hold,d,1000001);assert(!hm.locked&&n30::rf_bars(hm,1000001)==4);
 s.rc7_bound=true;s.joined_peer=5;s.joined_boot=6;s.rc7_depth=1;s.n30_signal={5,1000000,8,-65};m=n30::model(s,d,1000001);assert(n30::rf_bars(m,1000001)==3);
 s.rc7_depth=2;m=n30::model(s,d,1000001);assert(n30::rf_bars(m,1000001)==3);
 s.rc7_bound=false;
 uint64_t utc;assert(n30::time(m,1000100,utc));assert(!n30::time(m,1100000,utc));
 s.rc7_bound=true;s.rc7_delivery=950;m=n30::model(s,d,1000001);assert(m.delivery==950);
 s.joined_peer=5;s.joined_boot=6;s.rc7_depth=1;m=n30::model(s,d,1000001);assert(m.source==n30::Source::Rc1);
 s.rc7_depth=2;m=n30::model(s,d,1000001);assert(m.source==n30::Source::Rc2);
 s.rc7_depth=3;m=n30::model(s,d,1000001);assert(m.source==n30::Source::None);
 s.rc7_depth=1;s.joined_boot=0;m=n30::model(s,d,1000001);assert(m.source==n30::Source::None);s.joined_boot=6;
 s.rc7_relay_only_requested=true;m=n30::model(s,d,1000001);assert(m.relay_only&&m.source==n30::Source::Rc1);
 s.rc7_server_flags=16;m=n30::model(s,d,1000001);assert(m.serving);
 s.following=false;s.g1_retained_estimate=true;m=n30::model(s,d,1000001);assert(m.valid&&!m.locked&&!m.serving&&m.state==n30::State::Hold&&m.rms_ns==UINT32_MAX);
 s.fault=true;m=n30::model(s,d,1000001);assert(!m.valid&&!m.locked&&m.state==n30::State::Fault);
 s.fault=false;s.requested=false;m=n30::model(s,d,1000001);assert(!m.valid&&m.state==n30::State::Off);
 s.requested=s.following=true;s.g1_clock_model.tick=20000000;s.g1_clock_model.rate=-5486.737;s.g1_clock_origin=7;
 s.rms_ns=12345;d.rms_ns=170;m=n30::model(s,d,1000001);
 assert(m.precision.valid&&m.precision.rms_ns==170&&m.rms_ns==170&&!m.precision.relative_known&&m.precision.scope==7);
 s.g1_clock_model.rate=std::nan("");m=n30::model(s,d,1000001);assert(!m.precision.relative_known);
 s.g1_clock_model.rate=50001;m=n30::model(s,d,1000001);assert(!m.precision.relative_known);
 // Root identity and relative residual are independent of the parent/raw trim.
 m=n30::model(s,d,1000001,"ROOT-02");assert(!std::strcmp(m.root,"ROOT-02")&&!std::strcmp(m.parent,"TC-02"));
 n30::relative_rate(m,1,2,3,-123,true,1000000,1000001);assert(m.precision.relative_known&&m.precision.relative_ppb==-123);
 for(unsigned fault=0;fault<7;fault++){
  auto bad=m;if(fault==4)bad.ble.following=false;if(fault==5)bad.precision.valid=false;
  n30::relative_rate(bad,fault==0?9:1,fault==1?9:2,fault==2?9:3,123,fault!=3,fault==6?1000002:1000000,1000001);
  assert(!bad.precision.relative_known);
 }
 n30::relative_rate(m,1,2,3,123,true,1000000,1250000);assert(!m.precision.relative_known);
 m=n30::model(s,d,1000001);assert(!std::strcmp(m.root,"----")&&!m.precision.relative_known);
 s.requested=s.following=true;s.root_boot=0;m=n30::model(s,d,1000001);assert(!m.valid&&!m.locked);
 s.root_boot=2;s.rc7_upstream_mode_pending=true;m=n30::model(s,d,1000001);assert(!m.valid&&!m.locked&&m.state==n30::State::Acquire&&m.mode_pending&&m.source==n30::Source::None);
}

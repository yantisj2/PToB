// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual native UI selection block, actual source freshness gates and state.
#include "sync_watchdog.hpp"
#include "../legacy/display.hpp"
#include <cassert>
#include <cstring>
uint64_t now=1000000;
uint64_t micros(){return now;}
w12_lora_view radio;
void w12_lora_snapshot(w12_lora_view& out){out=radio;}
namespace v12 {struct R2Dashboard {static const char* name(uint64_t){return "ROOT";}};}
void apply(DisplayState& state,const v12::R2Status& mesh){
 [[maybe_unused]] uint64_t selected_utc=0,selected_at=0;
#include "actual_dual_display.inc"
}
int main(){
 radio.active=radio.requested=radio.time_valid=true;radio.state=2;radio.emitter=1;radio.root=2;radio.root_boot=3;radio.source_epoch=4;
 radio.utc_us=1700000000000000;radio.sampled_us=now;radio.until_us=now+12000000;radio.link_samples=8;radio.link_rms_ns=123;
 v12::R2Status mesh;DisplayState s;apply(s,mesh);
 assert(s.precision_lora&&s.precision_live&&s.rms_ns==123&&s.lora_link_samples==8&&s.timing_source==DisplayState::Lora);
 assert(s.valid&&s.utc==1700000000&&!std::strcmp(s.role,"LORA"));
 const auto valid=radio;
 radio.link_samples=2;radio.link_rms_ns=UINT32_MAX;s={};apply(s,mesh);
 assert(s.precision_lora&&s.precision_live&&s.rms_ns==UINT32_MAX&&s.lora_link_samples==2);
 for(unsigned failure=0;failure<5;++failure){
  radio=valid;
  if(failure==0)radio.sampled_us=now-w12::SyncSnapshotMaxUs;
  if(failure==1)radio.until_us=now;
  if(failure==2)radio.active=false;
  if(failure==3)radio.fault=true;
  if(failure==4)radio.state=1;
  s={};apply(s,mesh);assert(!s.valid&&!s.precision_live&&s.rms_ns==UINT32_MAX);
  assert(!std::strchr(s.role,' '));
 }
 radio=valid;radio.root_boot=0;radio.time_valid=radio.metadata_known=false;
 s={};apply(s,mesh);assert(s.precision_lora&&s.precision_live&&s.rms_ns==123&&s.valid);
 radio=valid;mesh.requested=mesh.following=mesh.clock_valid=true;mesh.root_identity=1;mesh.root_boot=2;mesh.source_epoch=3;mesh.publication=4;
 mesh.follower_snapshot_us=now;mesh.service_until_us=mesh.g1_anchor_deadline_us=mesh.g1_corrected_until_us=now+5000000;
 auto& q=mesh.g1_relative_quality;q.original20=q.evaluated20=20000000;q.samples=16;q.rms_ns=100;q.max_ns=300;q.rate_guard_ppb=20;
 mesh.g1_clock_model={20000000,10000000000,0,0,0};assert(w12::ble_locked(mesh,now));
 s={};s.precision_live=true;s.rms_ns=49;s.timing_source=DisplayState::Ble;apply(s,mesh);
 assert(!s.precision_lora&&s.precision_live&&s.rms_ns==49&&s.timing_source==DisplayState::Ble);
}

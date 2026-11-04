// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual native UI selection block, actual source freshness gates and state.
#include "sync_watchdog.hpp"
#include "rc9_source_policy.hpp"
#include "lora_clock_selection.hpp"
#include "display_second_deadline.hpp"
#include "../legacy/display.hpp"
#include <cassert>
#include <cstring>
uint64_t now=1000000;
uint64_t micros(){return now;}
w12_lora_view radio;
w12::LoRaClockSelection display_clock_selection;bool display_lora_selected=false;
void w12_lora_snapshot(w12_lora_view& out){out=radio;}
namespace v12 {struct R2Dashboard {static const char* name(uint64_t){return "ROOT";}};}
#ifdef PTOB_RC9
namespace v12 {Rc9Status child_demand;Rc9Status r2_rc9_status(){return child_demand;}}
struct {bool latched()const{return false;}} update_gate;
#endif
void observe(const v12::R2Status& mesh){
 w12_lora_view lora_time;w12_lora_snapshot(lora_time);
#include "actual_display_observer.inc"
}
void apply(DisplayState& state,const v12::R2Status& mesh){
 [[maybe_unused]] uint64_t selected_utc=0,selected_at=0;
#include "actual_dual_display.inc"
}
int main(){
 radio.active=radio.requested=radio.time_valid=true;radio.state=2;radio.emitter=1;radio.root=2;radio.root_boot=3;radio.source_epoch=4;
 radio.utc_us=1700000000000000;radio.sampled_us=now;radio.until_us=now+12000000;radio.link_samples=8;radio.link_rms_ns=123;
 v12::R2Status mesh;mesh.requested=true;
 observe(mesh);
#ifdef PTOB_RC9
 v12::child_demand.client_until_us=now+360000000;v12::child_demand.clients=1;
#endif
 DisplayState s;apply(s,mesh);
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
 mesh.g1_clock_origin=77;observe(mesh);
 s={};s.precision_live=true;s.rms_ns=49;s.timing_source=DisplayState::Ble;apply(s,mesh);
 assert(s.precision_lora&&s.timing_source==DisplayState::Lora);
 // Main-cadence observation continues while actual display painting is1Hz.
 const auto start=now;w12::DisplaySecondDeadline paint;paint.painted(now,true,radio.utc_us);unsigned paints=0;
 radio.until_us=start+120000000;
 for(now=start+1000;now<=start+31000000;now+=1000){
  mesh.follower_snapshot_us=now;mesh.service_until_us=mesh.g1_anchor_deadline_us=mesh.g1_corrected_until_us=now+5000000;
  radio.sampled_us=now;radio.utc_us=valid.utc_us+now-start;observe(mesh);
  if(paint.due(now)){
   ++paints;s={};s.precision_live=true;s.rms_ns=49;s.timing_source=DisplayState::Ble;apply(s,mesh);
   assert(s.precision_lora==(paints<30));paint.painted(now,true,radio.utc_us);
  }
 }
 now-=1000;assert(paints==31&&!s.precision_lora&&s.precision_live&&s.rms_ns==49&&s.timing_source==DisplayState::Ble);
#ifdef PTOB_RC9
 assert(!s.rc9_serving&&s.rc9_clients==1); // New passive source lacks original envelope.
 radio.edge_us=now-1;radio.until_us=now+10000000;radio.event_ns=1700000000000000000;radio.created_ns=radio.event_ns-100;radio.expires_ns=radio.event_ns+1000000000;
 s={};s.precision_live=true;s.rms_ns=49;s.timing_source=DisplayState::Ble;apply(s,mesh);
 assert(s.rc9_serving&&!s.precision_lora); // Valid legacy source service is independent of local display.
#endif
}

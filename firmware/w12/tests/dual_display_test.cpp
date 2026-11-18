// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual native UI selection block, actual source freshness gates and state.
#include "sync_watchdog.hpp"
#include "rc9_time.hpp"
#include "dual_timing.hpp"
#include "bounded_log.hpp"
#include "display_second_deadline.hpp"
#include "../legacy/display.hpp"
#include <cassert>
#include <cstring>
uint64_t now=1000000,last_ui=1;
uint64_t micros(){return now;}
w12_lora_view radio;
uint64_t ticks(){return now*20;}
bool w12_fine_current(uint64_t* tick){*tick=ticks();return true;}
bool v12::ConsoleFrame::format(const char*,...){return true;}
void w12_lora_snapshot(w12_lora_view& out){out=radio;}
namespace v12 {struct R2Dashboard {static const char* name(uint64_t){return "ROOT";}};}
#ifdef PTOB_RC9
namespace v12 {Rc9Status child_demand;Rc9Status r2_rc9_status(){return child_demand;}}
#endif
void observe(const v12::R2Status& mesh){
 const auto source_now=micros();
#include "actual_display_observer.inc"
}
void apply(DisplayState& state,const v12::R2Status& mesh){
 [[maybe_unused]] uint64_t selected_utc=0,selected_at=0;
#include "actual_dual_display.inc"
}
void page_deadline(DisplayState& state,uint8_t page,uint64_t selected_at,uint64_t selected_utc,w12::DisplaySecondDeadline& ui_deadline){
#include "actual_page_deadline.inc"
}
int main(){
 {DisplayState state;w12::DisplaySecondDeadline d;state.valid=true;state.lora_time_valid=true;state.lora_ui_phase=w12::LoRaUiPhase::Live;
  state.lora_sampled_us=now;state.lora_utc_us=1700000000250000;
  page_deadline(state,display_pages::LoraRx,now,1700000000900000,d);assert(d.deadline()==now+750000);
  page_deadline(state,display_pages::Front,now,1700000000900000,d);assert(d.deadline()==now+100000);
  state.lora_time_valid=false;state.lora_ui_phase=w12::LoRaUiPhase::Off;page_deadline(state,display_pages::LoraRx,now,1700000000900000,d);assert(d.deadline()==now+1000000);}

 radio.active=radio.requested=radio.time_valid=radio.metadata_known=radio.model_valid=true;
 radio.state=2;radio.emitter=1;radio.root=2;radio.root_boot=3;radio.source_epoch=4;
 radio.utc_us=1700000000000000;radio.sampled_us=now;radio.until_us=now+12000000;
 radio.edge_tick=radio.model_tick16=ticks()*4/5;radio.model_ns=radio.utc_us*1000;
 radio.capture_epoch=1;radio.link_samples=8;radio.link_rms_ns=123;
 v12::R2Status mesh;mesh.requested=true;
 observe(mesh);
#ifdef PTOB_RC9
 v12::child_demand.client_until_us=now+360000000;v12::child_demand.clients=1;
#endif
 DisplayState s;apply(s,mesh);
 assert(w12::dual_timing_segment(ticks()).at(ticks())==radio.model_ns+4500);
 assert(s.precision_lora&&s.precision_live&&s.rms_ns==123&&s.lora_link_samples==8&&s.timing_source==DisplayState::Lora);
 assert(s.lora_ui_phase==w12::LoRaUiPhase::Live&&s.lora_utc_us==radio.utc_us&&s.lora_sampled_us==now);
 assert(s.valid&&s.utc==1700000000&&!std::strcmp(s.role,"LORA"));
 const auto valid=radio;
 radio.link_samples=2;radio.link_rms_ns=UINT32_MAX;s={};apply(s,mesh);
 assert(s.precision_lora&&s.precision_live&&s.rms_ns==UINT32_MAX&&s.lora_link_samples==2);
 for(unsigned failure=0;failure<7;++failure){
  now+=20000;radio=valid;radio.sampled_us=now;mesh.fault=false;
  if(failure==0)radio.sampled_us=now-w12::SyncSnapshotMaxUs;
  if(failure==1)radio.until_us=now;
  if(failure==2)radio.active=false;
  if(failure==3)radio.fault=true;
  if(failure==4)radio.state=1;
  if(failure==5){mesh.fault=true;radio.requested=false;}
  if(failure==6){radio.mode=2;radio.requested=false;}
  observe(mesh);s={};apply(s,mesh);
  assert(s.valid&&!s.precision_lora&&!s.precision_live&&s.rms_ns==UINT32_MAX);
  assert(!std::strcmp(s.role,"HOLD")&&!s.lora_utc_us&&s.lora_ui_phase!=w12::LoRaUiPhase::Live);
 }
 now+=20000;mesh.fault=false;radio=valid;radio.sampled_us=now;radio.root_boot=0;
 observe(mesh);s={};apply(s,mesh);assert(s.precision_lora&&s.precision_live&&s.rms_ns==123&&s.valid);
 now+=20000;radio.sampled_us=now;mesh.requested=mesh.following=mesh.clock_valid=mesh.group_member=true;
 mesh.root_identity=mesh.g1_clock_origin=1;mesh.root_boot=2;mesh.source_epoch=3;mesh.publication=4;
 const auto feed=[&]{
  mesh.follower_snapshot_us=now;mesh.service_until_us=mesh.g1_anchor_deadline_us=mesh.g1_corrected_until_us=now+5000000;
  auto& q=mesh.g1_relative_quality;q.original20=q.evaluated20=ticks();q.samples=16;q.rms_ns=100;q.max_ns=300;q.rate_guard_ppb=20;
  mesh.g1_corrected_original20=ticks();mesh.g1_clock_model={ticks(),int64_t(valid.model_ns)+(int64_t(now)-1000000)*1000,0,0,0};
  assert(w12::ble_locked(mesh,now));observe(mesh);
 };
 feed();s={};apply(s,mesh);assert(s.precision_lora);
 now+=100000;radio.sampled_us=now;feed();s={};s.rms_ns=49;apply(s,mesh);
 assert(!s.precision_lora&&s.precision_live&&s.rms_ns==49&&s.timing_source==DisplayState::Ble);
#ifdef PTOB_RC9
 assert(!s.rc9_serving&&s.rc9_clients==1);
 v12::child_demand.serving=true;
 s={};s.precision_live=true;s.rms_ns=49;s.timing_source=DisplayState::Ble;apply(s,mesh);
 assert(s.rc9_serving&&s.timing_source==DisplayState::Ble);
 mesh.following=false;mesh.rc_parent.target=5;
 auto& rc9=v12::child_demand;
 rc9.server=5;rc9.server_boot=6;rc9.root=2;rc9.source_epoch=4;rc9.sequence=7;
 rc9.sampled_us=now;rc9.utc_us=1700000001000000;rc9.until_us=now+1000000;rc9.valid=rc9.following=true;
 s={};apply(s,mesh);
 assert(s.valid&&s.timing_source==DisplayState::Rc9&&!s.precision_lora&&s.utc==1700000001);
#endif
}

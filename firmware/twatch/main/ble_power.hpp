// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include "energy.hpp"
namespace watch {
struct BlePower {
 enum Stage:uint8_t {Active,Draining,Preparing,HostStop,HostExit,Deinit,Off,Init,Sync,Fault};
 static constexpr uint64_t StopUs=2000000,StartUs=5000000;
 Stage stage=Sync;uint8_t failed_stage=0,controller=0,sync_site=0,fault_site=0,episode_stage=0,cleanup_failed_stage=0;
 uint32_t generation=1,stack_min_bytes=UINT32_MAX;int32_t error=0;
 uint64_t started_us=0,deadline_us=0,completed_us=0;
 uint64_t error_at_us=0,cleanup_started_us=0,cleanup_deadline_us=0,cleanup_completed_us=0,off_proven_us=0;
 uint32_t fault_generation=0,cleanup_generation=0,off_generation=0;
 int32_t last_error=0;uint8_t last_failed_stage=0,last_fault_site=0;
 uint64_t last_error_at_us=0,cleanup_error_at_us=0;uint32_t last_fault_generation=0;
 int32_t cleanup_error=0;
 int32_t sync_error=0;uint32_t worker_alloc_failures=0;
 uint8_t sdk_operation=0;uint16_t resource_mask=0;bool off_proven=false;
 bool requested=true,host_running=false,worker_running=false,operating_fault=false,cleanup_active=false,cleanup_attempted=false;
 bool off_ack()const{return stage==Off&&!host_running&&controller==0&&off_proven_us&&!cleanup_error;}
 bool transitioning()const{return stage!=Active&&stage!=Off&&stage!=Fault;}
 void fail(int32_t rc,uint64_t now=0){
  if(stage==Fault)return;
  rc=rc?rc:-1;
  if(!error){failed_stage=stage;error=rc;error_at_us=now;fault_generation=generation;fault_site=sync_site;}
  last_error=rc;last_failed_stage=stage;last_error_at_us=now;last_fault_generation=generation;last_fault_site=sync_site;
  episode_stage=stage;
  if(cleanup_active){cleanup_error=rc;cleanup_failed_stage=stage;cleanup_error_at_us=now;cleanup_active=false;}
  else {operating_fault=stage==Init||stage==Sync;if(!operating_fault){cleanup_error=rc;cleanup_failed_stage=stage;cleanup_error_at_us=now;}}
  stage=Fault;off_proven_us=0;
 }
 void observe(uint64_t now){
  const auto until=cleanup_active?cleanup_deadline_us:deadline_us;
  if(transitioning()&&until&&now>=until)fail(-110,now);
 }
 bool begin_cleanup(uint64_t now){
  if(stage!=Fault||!operating_fault||cleanup_attempted||cleanup_error)return false;
  cleanup_attempted=true;
  if(generation==UINT32_MAX||now>UINT64_MAX-StopUs){cleanup_error=-75;cleanup_failed_stage=Fault;return false;}
  cleanup_generation=++generation;cleanup_started_us=now;cleanup_deadline_us=now+StopUs;
  cleanup_completed_us=0;cleanup_active=true;stage=Draining;return true;
 }
 bool recover(uint64_t now){
  if(!off_ack()||!operating_fault||!requested)return false;
  if(generation==UINT32_MAX||now>UINT64_MAX-StartUs){cleanup_error=-75;cleanup_failed_stage=Off;stage=Fault;return false;}
  ++generation;operating_fault=false;cleanup_attempted=false;stage=Init;
  started_us=now;deadline_us=now+StartUs;off_proven_us=0;return true;
 }
 bool request(bool on,uint64_t now){
  observe(now);
  if(stage==Fault){if(!on)requested=false;return !on&&operating_fault&&!cleanup_error;}
  if(!on&&off_ack()){requested=false;return true;} // Desired OFF changes no closed resource generation.
  if(on&&off_ack()&&operating_fault){requested=true;return recover(now);}
  if(requested==on)return true;
  if(generation==UINT32_MAX||now>UINT64_MAX-(on?StartUs:StopUs)){fail(-75,now);return false;}
  ++generation;requested=on;
  if(!on&&(stage==Active||stage==Sync||stage==Init)){stage=Draining;started_us=now;deadline_us=now+StopUs;}
  else if(on&&stage==Off){stage=Init;started_us=now;deadline_us=now+StartUs;off_proven_us=0;}
  return true; // A toggle cannot cancel/extend an already owned stop.
 }
 void stopped(uint64_t now){
  observe(now);if(stage==Fault)return;
  completed_us=now;controller=0;host_running=false;stage=Off;off_proven_us=now;off_generation=generation;
  if(cleanup_active){cleanup_active=false;cleanup_completed_us=now;}
  if(requested&&!operating_fault){stage=Init;started_us=now;deadline_us=now+StartUs;off_proven_us=0;}
 }
 void synced(uint64_t now){
  observe(now);if(stage!=Sync)return;
  completed_us=now;stage=Active;
  if(!requested){stage=Draining;if(!deadline_us){started_us=now;deadline_us=now+StopUs;}}
 }
};
}

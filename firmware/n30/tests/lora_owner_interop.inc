// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Real SX1262 owner + RC8/HMAC + display selector; synthetic peripherals only.
extern "C" {
#include "lora_rc8.h"
}
#include "dual_time.hpp"
#include <cstdio>
namespace {
const r2_group_config interop_group={.psk={1,2,3},.id=11,.epoch=1};
const rc8_scope interop_scope={.emitter=1,.emitter_boot=22,.stream=1,.root=3,.root_boot=44,.parent=0,.parent_boot=0,
 .source_epoch=5,.scale=RC8_UTC,.kind=RC8_SELECTED_BLE};
void run_for(uint64_t us){while(us){const auto delta=std::min<uint64_t>(us,2000);step(false,delta);us-=delta;}}
uint64_t packet_edge(unsigned size,bool crc_bad=false){
 fake::packet_size=size;fake::irq=crc_bad?0x42:2;fake::gpio[14]=1;
 const uint64_t at=fake::us;mcpwm_capture_event_data_t e{fake::capture()};on_edge(edge_channel,&e,nullptr);step(false);return at;
}
}
int main(){
 cold();fake::us=900000000;step(true,0);step(false,1000);step(false,n30::BleAcquireUs);
 assert(owned.enabled&&published.active&&published.state==1);run_for(10000);
 rc8_companion meta{};meta.scope=interop_scope;meta.sequence=1;meta.time_valid=true;
 assert(!rc8_companion_encode(fake::packet.data(),&meta,&interop_group));packet_edge(128);
 uint64_t previous_us=0,previous_ns=0;uint32_t sequence=0;
 const auto generation=core_generation;
 for(unsigned i=1;i<=90;i++){
  run_for(4000000);rc8_packet p{};p.sequence=++sequence;p.root=interop_scope.root;
  p.source_epoch=interop_scope.source_epoch;p.scale=interop_scope.scale;p.kind=interop_scope.kind;
  assert(!rc8_context_id(p.context,&interop_scope,&interop_group));
  if(i>1){p.corrected=true;p.correction={.event_ns=previous_ns,.created_ns=previous_ns-1000000,
    .expires_ns=previous_ns+1500000000,.sequence=sequence-1,.map_generation=7,.map_age_us=0,.upstream_bound_ns=RC8_UNKNOWN};}
  assert(!rc8_encode(fake::packet.data(),&p,&interop_group));
  const auto edge_us=packet_edge(80,i==8);
  if(i>=2&&i!=9){
   assert(published.active&&!published.fault&&published.state==2&&published.utc_us&&core_generation==generation);
   if(i!=8)assert(published.edge_us==previous_us&&published.utc_us==previous_ns/1000+fake::us-previous_us);
   n30::Model m;m.sampled_us=m.utc_at_us=fake::us;m.utc_us=1;m.until_us=fake::us+250000;
   m.valid=true;m.locked=false;m.state=n30::State::Follow;m.source=n30::Source::Ble;
   n30::select_lora(m,published,fake::us);assert(m.source==n30::Source::LoRa&&m.valid&&m.locked);
  }
  previous_us=edge_us;previous_ns=UINT64_C(1700000000000000000)+edge_us*1000;
 }
 assert(published.accepted>=85&&published.crc_bad==1&&published.invalid_capture==0&&published.map_rejected==0);
 run_for(13000000);assert(published.state==3&&!published.utc_us&&!published.until_us);
 // Keep RX alive through total RF loss, then reacquire using new physical pairs.
 for(unsigned i=0;i<2;i++){
  run_for(4000000);rc8_packet p{};p.sequence=++sequence;p.root=interop_scope.root;
  p.source_epoch=interop_scope.source_epoch;p.scale=interop_scope.scale;p.kind=interop_scope.kind;
  assert(!rc8_context_id(p.context,&interop_scope,&interop_group));p.corrected=true;
  p.correction={.event_ns=previous_ns,.created_ns=previous_ns-1000000,.expires_ns=previous_ns+1500000000,
    .sequence=sequence-1,.map_generation=7,.map_age_us=0,.upstream_bound_ns=RC8_UNKNOWN};
  assert(!rc8_encode(fake::packet.data(),&p,&interop_group));previous_us=packet_edge(80);
  previous_ns=UINT64_C(1700000000000000000)+previous_us*1000;
 }
 assert(published.state==2&&published.utc_us&&core_generation==generation&&!published.fault);
 assert(v12::lora_request(false,0));worker_step(true);assert(owner.drained());
 std::puts("PASS late AUTO RX, 6min capture wraps, real authenticated pairs, CRC/loss recovery and LOR preference; synthetic peripherals");
}

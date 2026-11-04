// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Actual SX1262 owner + schedule8 HMAC + map/selection. Synthetic peripherals.
#include "dual_time.hpp"
#include <cstdio>
extern "C" void n30_test_packet(uint8_t*,unsigned,uint32_t,uint64_t);
namespace {
void run_for(uint64_t us){while(us){const auto delta=std::min<uint64_t>(us,2000);step(false,delta);us-=delta;}}
uint64_t packet_edge(unsigned size,bool bad=false){
 fake::packet_size=size;fake::irq=0x10;fake::gpio[14]=1;
 const auto at=fake::us;mcpwm_capture_event_data_t e{fake::capture()};on_edge(edge_channel,&e,nullptr);step(false);
 run_for(100000);fake::irq=bad?0x42:2;fake::gpio[14]=1;e.cap_value=fake::capture();on_edge(edge_channel,&e,nullptr);step(false);return at;
}
}
int main(){
 cold();fake::us=900000000;step(true,0);assert(owned.enabled&&published.active);run_for(10000);
 constexpr uint64_t base=UINT64_C(1700000000000000000);
 n30_test_packet(fake::packet.data(),2,0,base+fake::us*1000);packet_edge(112);
 const auto gen=core_generation;
 for(unsigned i=0;i<10;i++){
  run_for(30000000);const auto original=fake::us;
  n30_test_packet(fake::packet.data(),3,2*i,0);packet_edge(24,i==2);
  run_for(2400000);n30_test_packet(fake::packet.data(),4,2*i,base+original*1000-99328000);packet_edge(32);
  if(i>=7){assert(published.state==2&&published.utc_us&&published.link_rms_ns<=1&&core_generation==gen);assert(published.utc_us==base/1000+fake::us);}
 }
 assert(published.accepted==9&&published.crc_bad==1&&!published.invalid_capture&&!published.map_rejected&&!published.fault);
 run_for(120000000);assert(published.state==3&&!published.utc_us&&!published.until_us&&owned.enabled);
 assert(v12::lora_request(false,0));worker_step(true);assert(owner.drained());
 std::puts("PASS schedule8 real HMAC/HeaderValid+RXdone matching, no-DISC, trained map, 6min counter wraps, CRC/expiry and continuous RX; synthetic peripherals");
}

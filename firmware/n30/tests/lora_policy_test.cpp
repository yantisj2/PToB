// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "lora_policy.hpp"
#include "dual_time.hpp"
#include <cassert>
#include <cstring>
int main(){
 using namespace n30;LoRaPolicy p;const uint64_t start=1000000;
 p.update(false,start);assert(!p.requested()&&p.remaining_ms(start)==180000);
 p.update(false,start+BleAcquireUs-1);assert(!p.requested());p.update(false,start+BleAcquireUs);assert(p.requested());
 p.toggle();assert(p.mode()==LoRaMode::KeepOff&&!p.requested());p.toggle();assert(p.mode()==LoRaMode::Auto&&p.requested());
 const auto good=start+BleAcquireUs+10;p.update(true,good);p.update(true,good+BleRecoverUs-1);assert(p.requested());
 p.update(false,good+BleRecoverUs-1);p.update(true,good+BleRecoverUs);p.update(true,good+2*BleRecoverUs-1);assert(p.requested());
 p.update(true,good+2*BleRecoverUs);assert(!p.requested());p.toggle();assert(p.mode()==LoRaMode::KeepOn&&p.requested());p.toggle();assert(p.mode()==LoRaMode::Auto&&!p.requested());
 p.update(false,good+2*BleRecoverUs+1);p.update(true,good+2*BleRecoverUs+100);p.update(false,good+2*BleRecoverUs+101);p.update(false,good+3*BleRecoverUs);assert(!p.requested());p.update(false,good+3*BleRecoverUs+101);assert(p.requested());
 p.update(false,1);assert(!p.requested()); // Monotonic clock rollback resets dwell.
 Model m;m.sampled_us=m.utc_at_us=1000000;m.utc_us=10000000;m.until_us=1250000;m.valid=m.locked=true;m.state=State::Follow;m.source=Source::Ble;
 m.ble={1000000,10000000,1250000,3,44,5,true,true};
 n30_lora_view l{};l.sampled_us=1000000;l.utc_us=10000001;l.until_us=13000000;l.state=2;l.active=l.requested=l.metadata_known=true;l.root=3;l.root_boot=44;l.source_epoch=5;l.capture_epoch=7;l.generation=1;l.corrected_sequence=1;l.event_ns=123;l.context[0]=1;
 select_lora(m,l,1000000);assert(m.source==Source::Ble&&m.utc_us==10000000);
 DualComparison c;c.update(m,1000000);assert(m.delta_valid&&m.delta_ns==-1123&&!m.drift_valid);
 m.lora.root_boot=45;c.update(m,1000001);assert(!m.delta_valid&&!m.drift_valid);m.lora=l;
 c.update(m,1000000);m.sampled_us=m.utc_at_us=5000000;m.utc_us=14000001;m.until_us=5250000;m.ble={5000000,14000001,5250000,3,44,5,true,true};m.lora.sampled_us=5000000;m.lora.utc_us=14000001;m.lora.corrected_sequence=2;
 c.update(m,5000000);assert(m.delta_valid&&m.delta_ns==-123&&m.drift_valid&&m.drift_ppb==250);
 m.lora.root=9;c.update(m,5000001);assert(!m.delta_valid&&!m.drift_valid);m.lora.root=3;
 m.until_us=4999999;m.valid=m.locked=false;m.state=State::Acquire;m.ble.valid=false;select_lora(m,m.lora,5000001);assert(m.valid&&m.source==Source::LoRa&&!m.serving&&m.until_us==13000000);uint64_t utc;assert(time(m,5000001,utc)&&utc==14000002);assert(!m.ble.valid);
 m.lora.fault=true;assert(!lora_time(m.lora,5000001,utc));m.lora.fault=false;assert(!lora_time(m.lora,5250000,utc));
}

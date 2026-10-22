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
 p.update(true,good+2*BleRecoverUs);assert(!p.requested());p.toggle();assert(p.mode()==LoRaMode::KeepOn&&p.requested());p.toggle();assert(p.mode()==LoRaMode::KeepOff&&!p.requested());p.toggle();assert(p.mode()==LoRaMode::Auto&&!p.requested());
 p.update(false,good+2*BleRecoverUs+1);p.update(true,good+2*BleRecoverUs+100);p.update(false,good+2*BleRecoverUs+101);p.update(false,good+3*BleRecoverUs);assert(p.requested());
 p.update(false,1);assert(!p.requested()); // Monotonic clock rollback resets dwell.
 Model m;m.sampled_us=m.utc_at_us=1000000;m.utc_us=10000000;m.until_us=1250000;m.valid=m.locked=true;m.state=State::Follow;m.source=Source::Ble;
 m.ble={1000000,10000000,1250000,3,44,5,true,true};
 n30_lora_view l{};l.sampled_us=1000000;l.utc_us=10000001;l.until_us=13000000;l.state=2;l.active=l.requested=l.metadata_known=true;l.root=3;l.root_boot=44;l.source_epoch=5;l.capture_epoch=7;l.generation=1;l.corrected_sequence=1;l.event_ns=123;l.context[0]=1;
 select_lora(m,l,1000000);assert(m.source==Source::Ble&&m.utc_us==10000000);
 // A valid but unsettled/marginal BLE model must not replace a working LOR
 // dial/source/saver. BLE pending-mode presentation also cannot hide LOR.
 for(auto state:{State::Search,State::Join,State::Acquire,State::Follow,State::Hold})for(bool pending:{false,true}){
  auto marginal=m;marginal.state=state;marginal.locked=false;marginal.mode_pending=pending;
  select_lora(marginal,l,1000000);assert(marginal.source==Source::LoRa&&marginal.valid&&marginal.locked);
  assert(!std::strcmp(source_label(marginal,1000000),"LOR"));uint64_t clock;assert(analog_time(marginal,1000000,clock));assert(pps_on(marginal,1000000));
 }
 // Continuous marginal BLE snapshots retain the LOR analog/saver, without
 // entering the500ms loss cache or resetting the moving saver position.
 ClockHold hold;View saver;saver.saver=true;saver.saver_position=2;uint8_t pixels[FrameBytes];
 for(unsigned i=0;i<100;i++){
  const uint64_t now=1000000+i*10000;auto marginal=m;marginal.sampled_us=now;marginal.utc_at_us=now;marginal.until_us=now+250000;marginal.locked=false;marginal.mode_pending=i%2;
  auto current=l;current.sampled_us=now;current.utc_us=l.utc_us+now-1000000;
  select_lora(marginal,current,now);hold.update(marginal,saver,now,true);hold.draw(pixels,marginal,saver,now);hold.presented(marginal,saver,now,pixels);
  assert(saver.saver&&saver.saver_position==2&&!saver.clock_frozen&&marginal.source==Source::LoRa);
 }
 auto locked=m;select_lora(locked,l,1000000);assert(locked.source==Source::Ble); // Only settled actual BLE returns.
 DualComparison c;c.update(m,1000000);assert(m.delta_valid&&m.delta_ns==-1123&&!m.drift_valid);
 m.lora.root_boot=45;c.update(m,1000001);assert(!m.delta_valid&&!m.drift_valid);m.lora=l;
 c.update(m,1000000);m.sampled_us=m.utc_at_us=5000000;m.utc_us=14000001;m.until_us=5250000;m.ble={5000000,14000001,5250000,3,44,5,true,true};m.lora.sampled_us=5000000;m.lora.utc_us=14000001;m.lora.corrected_sequence=2;
 c.update(m,5000000);assert(m.delta_valid&&m.delta_ns==-123&&m.drift_valid&&m.drift_ppb==250);
 m.lora.root=9;c.update(m,5000001);assert(!m.delta_valid&&!m.drift_valid);m.lora.root=3;
 m.until_us=4999999;m.valid=m.locked=false;m.state=State::Acquire;m.ble.valid=false;select_lora(m,m.lora,5000001);assert(m.valid&&m.source==Source::LoRa&&!m.serving&&m.until_us==13000000);uint64_t utc;assert(time(m,5000001,utc)&&utc==14000002);assert(!m.ble.valid);
 m.lora.fault=true;assert(!lora_time(m.lora,5000001,utc));m.lora.fault=false;assert(!lora_time(m.lora,5250000,utc));
}

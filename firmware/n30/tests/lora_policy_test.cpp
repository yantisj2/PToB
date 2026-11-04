// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "lora_policy.hpp"
#include "dual_time.hpp"
#include <cassert>
#include <cstring>
int main(){
 using namespace n30;LoRaPolicy p;const uint64_t start=1000000;
 assert(p.requested());
 for(bool locked:{false,true})for(bool parked:{false,true}){p.update(locked,start,parked);assert(p.requested()&&p.remaining_ms(start)==0);}
 p.toggle();assert(p.mode()==LoRaMode::KeepOff&&!p.requested());
 p.update(false,start+1,true);assert(!p.requested());p.toggle();assert(p.mode()==LoRaMode::Auto&&p.requested());
 p.set(LoRaMode::KeepOn);p.update(true,start+UINT64_C(600000000));assert(p.requested());
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
 // Production selection substitutes the actual LoRa timestamp, retains it
 // through brief BLE returns, resets on scope/loss and switches at exactly30s.
 ClockSelection choice;
 auto fresh=[&](uint64_t at,bool locked=true,uint32_t epoch=5){auto b=locked?m:Model{};b.sampled_us=b.utc_at_us=at;b.utc_us=90000000+at;b.until_us=at+SnapshotMaxUs;b.valid=true;b.locked=locked;b.state=State::Follow;b.source=Source::Ble;b.ble={at,b.utc_us,b.until_us,3,44,epoch,true,true};b.precision.scope=9;b.precision.valid=true;return b;};
 auto lv=[&](uint64_t at){auto v=l;v.sampled_us=at;v.utc_us=80000000+at;v.until_us=at+120000000;return v;};
 auto first=fresh(start,false);choice.select(first,lv(start),start);assert(first.source==Source::LoRa&&first.utc_us==81000000&&!first.serving);
 for(uint64_t dt=10000;dt<30000000;dt+=10000){auto b=fresh(start+dt);choice.select(b,lv(start+dt),start+dt);assert(b.source==Source::LoRa&&b.utc_us==80000000+start+dt);}
 auto reset=fresh(start+30000000,false);choice.select(reset,lv(start+30000000),start+30000000);assert(reset.source==Source::LoRa);
 uint64_t resumed=start+30010000;
 for(uint64_t dt=0;dt<=30000000;dt+=10000){auto b=fresh(resumed+dt);choice.select(b,lv(resumed+dt),resumed+dt);assert(b.source==(dt<30000000?Source::LoRa:Source::Ble));}
 auto lost=fresh(resumed+30010000,false);choice.select(lost,lv(resumed+30010000),resumed+30010000);assert(lost.source==Source::LoRa);
 auto changed=fresh(resumed+30020000,true,6);choice.select(changed,lv(resumed+30020000),resumed+30020000);assert(changed.source==Source::LoRa);
 auto refused=fresh(resumed+30030000);choice.select(refused,lv(resumed+30030000),resumed+30030000,false);assert(refused.source==Source::LoRa);
 auto after_gap=fresh(resumed+60040000);choice.select(after_gap,lv(resumed+60040000),resumed+60040000);assert(after_gap.source==Source::LoRa);
 auto expiry=fresh(resumed+60050000);expiry.until_us=expiry.utc_at_us;choice.select(expiry,lv(resumed+60050000),resumed+60050000);assert(expiry.source==Source::LoRa);
}

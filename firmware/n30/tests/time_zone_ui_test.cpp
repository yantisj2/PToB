// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ui.hpp"
#include "display_time_zone.hpp"
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
static void save(const char* dir,const char* name,const uint8_t* f){
 std::string path=std::string(dir)+"/"+name+".pgm";FILE* p=std::fopen(path.c_str(),"wb");assert(p);
 std::fprintf(p,"P5\n64 128\n255\n");for(unsigned y=0;y<128;y++)for(unsigned x=0;x<64;x++)std::fputc(f[y*8+x/8]&(0x80>>(x%8))?255:0,p);std::fclose(p);
}
int main(int argc,char** argv){
 assert(argc==2);constexpr uint64_t at=1000000;
 n30::Model m;m.sampled_us=m.utc_at_us=at;m.until_us=at+250000;m.utc_us=7200125000;
 m.valid=m.locked=true;m.state=n30::State::Follow;m.source=n30::Source::Ble;m.tz={2,-300};m.tz_until_us=at+200000;
 uint64_t local=0;assert(!std::strcmp(n30::clock_zone(m,at,m.utc_us,local),"CDT")&&local==75600125000);
 uint8_t a[1024],b[1024];n30::View v;
 n30::render(a,m,v,at);save(argv[1],"n30-clock-cdt",a);
 auto reference=m;reference.tz_until_us=0;reference.utc_us=local;
 n30::render(b,reference,v,at);
 // Exact digital and analog pixels equal an independently shifted UTC clock.
 assert(!std::memcmp(a+23*8,b+23*8,69*8));
 n30::render(b,reference,v,at);assert(std::memcmp(a+12*8,b+12*8,11*8)); // Zone strip changed.
 assert(m.utc_us==7200125000); // Immutable UTC model.
 // First-page digital time and footer do not become an observer status panel.
 // Valid acquisition/HOLD and truly unknown time keep the established geometry.
 {
  uint8_t expected[1024],actual[1024];n30::View clock;
  for(auto status:{n30::State::Follow,n30::State::Acquire,n30::State::Hold}){
   auto primary=m;primary.state=status;primary.locked=status==n30::State::Follow;
   n30::render(expected,primary,clock,at);
   primary.lora.sampled_us=at;primary.lora.requested=primary.lora.active=true;
   primary.lora.metadata_known=primary.lora.time_valid=true;primary.lora.link_samples=4;primary.lora.fit_span_ms=90000;
   n30::render(actual,primary,clock,at);assert(!std::memcmp(expected,actual,1024));
  }
  auto unknown=m;unknown.valid=unknown.locked=false;unknown.source=n30::Source::None;unknown.state=n30::State::Acquire;
  n30::render(expected,unknown,clock,at);
  unknown.lora.sampled_us=at;unknown.lora.requested=unknown.lora.active=true;
  n30::render(actual,unknown,clock,at);assert(!std::memcmp(expected,actual,1024));
  unknown.source=n30::Source::LoRa;n30::render(actual,unknown,clock,at);
  assert(!std::memcmp(expected+23*8,actual+23*8,17*8)); // Same honest dashes.
  auto selected=unknown;selected.lora.requested=selected.lora.active=false;
  n30::render(expected,selected,clock,at);
  assert(!std::memcmp(expected,actual,42*8)&&!std::memcmp(expected+91*8,actual+91*8,37*8));
  assert(std::memcmp(expected+42*8,actual+42*8,49*8)); // Sync stays in analog area.
 }
 for(unsigned pos=0;pos<3;pos++){
  v.saver=true;v.saver_position=pos;n30::render(a,m,v,at);n30::render(b,reference,v,at);
  assert(!std::memcmp(a+(17+22*pos)*8,b+(17+22*pos)*8,49*8));
  if(!pos)save(argv[1],"n30-saver-cdt",a);
 }
 v={};n30::render(a,m,v,at+200000);auto utc=m;utc.tz_until_us=0;n30::render(b,utc,v,at+200000);assert(!std::memcmp(a,b,1024));save(argv[1],"n30-clock-expired-utc",a);
 for(auto source:{n30::Source::LoRa,n30::Source::Rc9}){m.source=source;assert(!std::strcmp(n30::clock_zone(m,at,m.utc_us,local),"CDT")&&local!=m.utc_us);}
 m.source=n30::Source::Ble;m.tz={42,345};assert(!std::strcmp(n30::clock_zone(m,at,0,local),"LOC")&&local==20700000000);
 m.tz={2,-360};assert(!std::strcmp(n30::clock_zone(m,at,0,local),"CST")&&local==64800000000);
 // Local pixels cannot remain in the original 500 ms timing-loss cache after TZ expiry.
 n30::ClockHold hold;m.tz={2,-300};m.tz_until_us=at+200000;
 hold.update(m,v,at);hold.draw(a,m,v,at);hold.presented(m,v,at,a);
 m.valid=m.locked=false;m.state=n30::State::Acquire;m.sampled_us=at+100000;
 hold.update(m,v,at+100000);assert(v.clock_frozen);
 m.sampled_us=at+200000;hold.update(m,v,at+200000);assert(!v.clock_frozen);
 // A stalled UI publisher also cannot keep a local label after snapshot freshness ends.
 n30::ClockHold stale;m.valid=m.locked=true;m.state=n30::State::Follow;m.sampled_us=at;m.tz_until_us=at+750000;
 stale.update(m,v,at);stale.draw(a,m,v,at);stale.presented(m,v,at,a);
 m.valid=m.locked=false;m.state=n30::State::Acquire;
 stale.update(m,v,at+100000);assert(v.clock_frozen);
 stale.update(m,v,at+250000);assert(!v.clock_frozen);
 // Source/context switches never restore pixels from the previous clock.
 for(unsigned change=0;change<3;change++){
  auto old=m;old.sampled_us=old.utc_at_us=at;old.until_us=at+n30::SnapshotMaxUs;old.valid=old.locked=true;old.state=n30::State::Follow;old.source=n30::Source::LoRa;old.lora.context[0]=1;old.lora.generation=1;old.lora.capture_epoch=1;
  n30::ClockHold pixels;n30::View page;pixels.update(old,page,at);pixels.draw(a,old,page,at);pixels.presented(old,page,at,a);
  old.valid=old.locked=false;old.state=n30::State::Acquire;
  if(change==0)old.source=n30::Source::Ble;if(change==1)++old.lora.context[0];if(change==2)++old.lora.capture_epoch;
  pixels.update(old,page,at+1);assert(!page.clock_frozen);
 }
 // RF diagnostics retain UTC and its difference calculations.
 m.valid=m.locked=true;m.state=n30::State::Follow;m.sampled_us=at;m.tz_until_us=at+200000;v.page=1;
 n30::render(a,m,v,at);m.tz_until_us=0;n30::render(b,m,v,at);assert(!std::memcmp(a,b,1024));
 // Learn signed LoRa META while BLE is parked. Expiry and context turnover
 // never erase a boot-session display preference or extend timing authority.
 n30::DisplayTimeZone cache;n30_lora_view l{};n30::Model c;
 c.sampled_us=at;c.source=n30::Source::LoRa;l.sampled_us=l.metadata_us=at;
 l.metadata_known=true;l.tz_id=2;l.tz_offset_min=-360;cache.update(c,0,0,0,l,at);
 assert(c.tz.offset_min==-360&&c.tz_until_us==UINT64_MAX&&!c.valid&&!c.until_us);
 assert(!std::strcmp(n30::clock_zone(c,at,0,local),"CST")&&local==64800000000);
 l={};c={};c.sampled_us=at+1000000000;
 cache.update(c,0,0,0,l,c.sampled_us);assert(c.tz.offset_min==-360&&!c.valid&&!c.until_us);
 c.tz={2,-300};c.tz_until_us=c.sampled_us+100000;cache.update(c,1,12,13,l,c.sampled_us);
 assert(c.tz.offset_min==-300);
 // Repeated old META cannot overwrite the newly learned BLE value.
 l.sampled_us=c.sampled_us;l.metadata_us=at;l.metadata_known=true;l.tz_id=2;l.tz_offset_min=-360;
 cache.update(c,1,12,13,l,c.sampled_us);assert(c.tz.offset_min==-300);
 l.metadata_us=c.sampled_us;c.tz=r2_tz_unknown();c.tz_until_us=0;
 cache.update(c,0,0,0,l,c.sampled_us);assert(c.tz.offset_min==-360);
 n30::DisplayTimeZone reboot;c={};cache.update(c,0,0,0,{},at);assert(c.tz.offset_min==-360);
 c={};reboot.update(c,0,0,0,{},at);assert(!r2_tz_valid(c.tz));
 puts("PASS N30 local digital/analog/saver, midnight, boot-session BLE/LoRa TZ cache, fresh metadata replacement, reboot, timing isolation and UTC diagnostics");
}

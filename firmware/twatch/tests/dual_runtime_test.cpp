// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "../main/dual_radio.cpp"
#include <cassert>
#include <cstdarg>
#include <cstdio>
namespace v12 {
R2Status test_status;
R2Status r2_status(){return test_status;}
bool ConsoleFrame::format(const char* fmt,...){va_list a;va_start(a,fmt);const int n=std::vsnprintf(bytes+length,sizeof bytes-length,fmt,a);va_end(a);if(n<0||unsigned(n)>=sizeof bytes-length){failed=true;return false;}for(int i=0;i<n;++i)rows+=bytes[length+i]=='\n';length+=uint16_t(n);return true;}
}
static uint64_t reads=0;static n30_lora_view lor;
static bool read20(uint64_t* t){*t=shadow_fake::now*20+(++reads%10);return true;}
extern "C" bool n30_lora_reference_read(uint64_t* t){*t=shadow_fake::now*20+1000;return true;}
bool watch_clock_bridge(bool (*)(uint64_t*),WatchClockBridge& out){out={shadow_fake::now*20,shadow_fake::now*20+999,shadow_fake::now*20+1001,1,true};return true;}
void watch_lora_dual_snapshot(n30_lora_view& out,watch::dual::LoRaBridge& b){out=lor;out.sampled_us=shadow_fake::now;b={shadow_fake::now*20+999,shadow_fake::now*20+1001,shadow_fake::now*16+16000000,shadow_fake::now,1};}
int main(){
 // A single racing publication produced a scope-less rejected PPS in the
 // observed run. It must not create ten extra seconds of missing evidence.
 // No rejected pulse gains eligibility; a real new scope still settles anew.
 {
  watchdual::Settler settle;watchdual::Row row;
  row.epoch=1;row.reference=row.capture=true;
  auto pulse=[&](uint32_t n,uint64_t scope,bool eligible){
   row.number=n;row.seen=uint64_t(n)*1000000;
   watchdual::RadioRow radio;radio.scope=scope;radio.eligible=eligible;
   settle.apply(radio,row);return radio;
  };
  assert(!pulse(1,99,true).eligible);
  assert(pulse(11,99,true).eligible==false); // missing physical sequence resets
  for(unsigned n=12;n<=21;++n)(void)pulse(n,99,true);
  assert(pulse(22,99,true).eligible);
  assert(!pulse(23,0,false).eligible);
  assert(pulse(24,99,true).eligible);
  assert(!pulse(25,100,true).eligible);
  for(unsigned n=26;n<=35;++n)(void)pulse(n,100,true);
  row.reference=false;assert(!pulse(36,0,false).eligible);
  row.reference=true;assert(!pulse(37,100,true).eligible);
  for(unsigned n=38;n<=47;++n)(void)pulse(n,100,true);
  row.capture=false;assert(!pulse(48,0,false).eligible);
  row.capture=true;assert(!pulse(49,100,true).eligible);
  for(unsigned n=50;n<=59;++n)(void)pulse(n,100,true);
  row.epoch=2;assert(!pulse(60,100,true).eligible);
 }
 constexpr uint64_t utc=UINT64_C(1790000000000000000);
 watchdual::init(1,read20);assert(watchdual::state);auto& s=v12::test_status;
 s.requested=s.following=s.clock_valid=true;s.g1_clock_origin=s.root_identity=10;s.root_boot=20;s.source_epoch=s.publication=1;
 s.g1_relative_quality.scope_generation=s.rx_capture.epoch=s.client.generation=1;s.client.running=true;s.g1_clock_model.tick=20000000;s.g1_clock_model.epoch=int64_t(utc+1000000100);
 s.g1_anchor_deadline_us=s.g1_corrected_until_us=s.service_until_us=40000000;
 lor.requested=lor.active=lor.time_valid=lor.metadata_known=lor.model_valid=true;lor.state=2;lor.model_kind=1;
 lor.capture_epoch=lor.model_epoch=lor.request_generation=1;lor.emitter=11;lor.emitter_boot=21;lor.root=12;lor.root_boot=22;
 lor.model_tick16=lor.edge_tick=32000000;lor.model_ns=utc+1000035000;lor.until_us=40000000;
 for(unsigned i=0;i<16;++i){
  shadow_fake::now=975000+uint64_t(i)*1000000;reads=0;s.g1_corrected_original20=shadow_fake::now*20-100;
  watchdual::poll(shadow_fake::now);
  const auto at=shadow_fake::now+25000;
  WatchPpsCapture e{at*20,at,i+1,1,50,true};watchdual::edge(e,{shadow_fake::now*20,1,1,true},true);
 }
 auto& st=*watchdual::state;const auto& last=st.rows[(st.rhead+31)%32];

 assert(last.reference&&last.capture&&last.ble.eligible&&last.lora.eligible);
 assert(last.ble.phase==100+watchdual::profile.ble_incremental_ns);assert(std::abs(last.lora.phase-(35000+watchdual::profile.lora_correction_ns))<300);assert(last.lora.enclosure>=163);
 assert(last.ble.utc_ns&&last.lora.utc_ns);
 assert(std::abs(int64_t(last.lora.utc_ns)-int64_t(last.ble.utc_ns)-
  (34900+watchdual::profile.lora_correction_ns-watchdual::profile.ble_incremental_ns))<300);
 #ifdef PTOB_DUAL_TEST_ENABLED
 assert(last.clock.valid&&last.clock.mode==watch::dual::Mode::LoRa&&watchdual::clock().enabled);
#else
 assert(!last.clock.valid&&!watchdual::clock().enabled);
#endif
 v12::ConsoleFrame frame;watchdual::frame(frame,15);assert(!frame.failed&&frame.rows==1&&frame.length<3500);
 std::puts(frame.bytes);
 lor.watch_valid=true;lor.state=0;shadow_fake::now+=1000000;reads=0;watchdual::poll(shadow_fake::now);
 auto at=shadow_fake::now+25000;watchdual::edge({at*20,at,17,1,50,true},{shadow_fake::now*20,1,1,true},true);
 assert(!st.rows[(st.rhead+31)%32].lora.eligible); // human watch never a timing source
 watchdual::reference_lost();assert(!st.settlers[0].start&&!st.settlers[1].start);
 // No fix is retained as a rejected physical pulse with both denominators.
 at+=1000000;watchdual::edge({at*20,at,18,1,50,true},{},false);
 assert(!st.rows[(st.rhead+31)%32].reference&&!st.rows[(st.rhead+31)%32].ble.eligible);
 st.~State();std::free(watchdual::state);watchdual::state=nullptr;
 std::puts("PASS actual dual-radio runtime: simultaneous physical PPS, strict LoRa, no GNSS feedback, bounded export");
}

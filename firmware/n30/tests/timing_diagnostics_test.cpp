// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual recorder, task adapter and six-row response against explicit OS fakes.
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <cstdint>
#define V12_R2_ENABLED 1
uint64_t fake_now=1000000;
#include "../main/timing_diagnostics.cpp"
namespace v12 {
bool ConsoleFrame::format(const char* fmt,...){
 va_list args;va_start(args,fmt);const int n=std::vsnprintf(bytes+length,sizeof(bytes)-length,fmt,args);va_end(args);
 if(n<=0||unsigned(n)>=sizeof(bytes)-length){failed=true;return false;}
 for(int i=0;i<n;i++)rows+=bytes[length+i]=='\n';length+=uint16_t(n);return true;
}
}
v12::periodic_rx::Stamp stamp(unsigned n){
 v12::periodic_rx::Stamp s{};s.actual=s.association_observed=true;s.epoch=3;s.sequence=n;
 s.tick20=fake_now*20;s.available20=s.tick20+200;s.coarse=3200+n;s.fine=uint16_t(n%625);
 s.channel=uint8_t(n%37);s.event=uint16_t(n);s.phy=0;s.map_enclosure_ns=1000;return s;
}
adv_rx::BleAnchor anchor{20000000,20000004,{3200,40}};
int main(){
 n30_timing_init();assert(v12::n30_probe::capture_sink&&v12::n30_probe::pair_sink);
 auto s=stamp(1);v12::n30_probe::capture_sink(s,anchor,-72);assert(recorder.snapshot(fake_now).seen==0);
 for(auto ms:{0u,999u,600001u})assert(!n30_timing_start(1,fake_now,ms,1));
 assert(!n30_timing_start(0,fake_now,1000,1));assert(!n30_timing_start(1,0,1000,1));
 assert(!n30_timing_start(1,UINT64_MAX-100,1000,1));
 for(auto every:{0u,65u})assert(!n30_timing_start(1,fake_now,1000,every));
 assert(n30_timing_start(1,fake_now,1000,1));assert(!n30_timing_start(2,fake_now,600000,1));
 const auto deadline=recorder.snapshot(fake_now).until_us;
 assert(!recorder.start(2,fake_now-1,600000,1,fake_now+1));assert(recorder.snapshot(fake_now+1).until_us==deadline);
 fake_now+=2;
 for(unsigned i=1;i<=20;i++){
  s=stamp(i);v12::n30_probe::capture_sink(s,anchor,-72);
  v12::n30_probe::Pair p{111,55,s.tick20,s.available20,123456789,123456700,2,3,s.epoch,i,s.event,s.channel,true,100000000,200000000};
  v12::n30_probe::pair_sink(p);fake_now+=1000;
 }
 auto q=recorder.snapshot(fake_now);assert(q.seen==20&&q.sampled==20&&q.matched==20&&q.overwritten==4&&q.oldest==5&&q.queued==16);
 n30::timing::Sample out[4]{};assert(recorder.read(0,out,4,fake_now)==4&&out[0].number==5&&out[3].number==8);
 assert(out[0].matched&&out[0].rssi==-72&&out[0].pair.remote_ns==123456789&&out[0].capture.fine==5);
 assert(recorder.read(0,out,4,fake_now)==4&&out[0].number==5); // CRC retry is non-consuming.
 static v12::ConsoleFrame frame;frame={};n30_timing_frame(frame,4,fake_now);
 assert(!frame.failed&&frame.rows==6&&frame.length<6500);
 assert(std::strstr(frame.bytes,"N30_TIMING_SAMPLE0 valid=1 n=5")&&std::strstr(frame.bytes,"clock_update=NONE"));
 fake_now=deadline-1;assert(recorder.snapshot(fake_now).active);fake_now=deadline;
 v12::n30_probe::capture_sink(stamp(21),anchor,-80);assert(!recorder.snapshot(fake_now).active&&recorder.snapshot(fake_now).seen==20);
 assert(n30_timing_start(3,fake_now,2000,4));
 for(unsigned i=1;i<=9;i++){s=stamp(i);v12::n30_probe::capture_sink(s,anchor,-50);fake_now+=1000;}
 assert(recorder.snapshot(fake_now).seen==9&&recorder.snapshot(fake_now).sampled==3);
 assert(recorder.read(0,out,4,fake_now)==0); // Pending association stays unavailable.
 fake_now+=250000;assert(recorder.read(0,out,4,fake_now)==3&&!out[0].matched&&!out[0].pair.causal);
 const auto until=recorder.snapshot(fake_now).until_us;frame={};n30_timing_frame(frame,3,fake_now);
 assert(frame.rows==6&&std::strstr(frame.bytes,"N30_TIMING_SAMPLE0 valid=0")&&recorder.snapshot(fake_now).until_us==until);
 n30_timing_stop();assert(!recorder.snapshot(fake_now).active);assert(recorder.read(0,out,4,fake_now)==3);
 assert(n30_timing_start(4,fake_now,1000,1));--fake_now;v12::n30_probe::capture_sink(stamp(1),anchor,-60);
 assert(!recorder.snapshot(fake_now).active&&recorder.snapshot(fake_now).seen==0);
 std::printf("PASS timing adapter: default OFF, finite bounds, exact expiry, no extension,16-row overwrite, nonconsuming cursor, decimation, missing pairs, backward clock; recorder_bytes=%zu sample_bytes=%zu\n",sizeof(n30::timing::Recorder),sizeof(n30::timing::Sample));
}

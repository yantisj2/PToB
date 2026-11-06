// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "sync_watchdog.hpp"
#include "bounded_log.hpp"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <cinttypes>
n30::ClockSelection clock_selection;
v12::G1ParentJournal parent_history;
namespace v12 {
bool ConsoleFrame::format(const char* fmt,...){
 va_list args;va_start(args,fmt);const int n=std::vsnprintf(bytes+length,sizeof(bytes)-length,fmt,args);va_end(args);
 if(n<=0||unsigned(n)>=sizeof(bytes)-length){failed=true;return false;}
 for(int i=0;i<n;++i)if(bytes[length+i]=='\n')++rows;length+=uint16_t(n);return true;
}
bool r2_parent_next(uint32_t after,G1ParentEvent& e){return parent_history.next(after,e);}
}
#include "actual_dual_frame.inc"
int main(int argc,char** argv){
 assert(argc==2);uint64_t now=1000000;
 n30::Model m{};m.sampled_us=now;
 n30_lora_view l{};l.requested=l.active=true;l.state=2;l.sampled_us=now;l.utc_us=1700000000000000;l.until_us=UINT64_MAX;
 clock_selection.select(m,l,now);
 v12::R2Status status{};status.root_identity=status.g1_clock_origin=1;status.root_boot=2;status.source_epoch=3;status.publication=4;status.g1_relative_quality.scope_generation=5;
 n30_display_observe_source(status,++now,0);
 n30_display_observe_source(status,++now,n30::BleCoarseExpiry);
 const auto first=clock_selection.view(now).first_problem;assert(first.sequence&&first.refusal==n30::BleCoarseExpiry);
 for(unsigned i=0;i<32;++i){
  now+=1000;clock_selection.observe(i%2,UINT64_MAX,UINT64_MAX,UINT32_MAX,UINT64_MAX,UINT32_MAX,UINT32_MAX,now,i%2?0:n30::BleQuality);
  v12::G1ParentScope scope{};scope.peer=UINT64_MAX-i;scope.boot=scope.root=scope.root_boot=UINT64_MAX;
  scope.source=scope.train=UINT32_MAX;scope.depth=2;scope.relay=true;
  parent_history.observe(scope,now,v12::G1ParentReason::ClockUnavailable,11,INT16_MIN,INT16_MAX,true);
 }
 assert(clock_selection.view(now).first_problem.sequence==first.sequence&&clock_selection.view(now).overwritten>0);
 static v12::ConsoleFrame a,b;
 n30_display_dual_frame(a,0,0,now);n30_display_dual_frame(b,0,0,now);
 assert(!a.failed&&a.rows==6&&a.length<6400&&a.length==b.length&&!std::memcmp(a.bytes,b.bytes,a.length));
 assert(std::strstr(a.bytes,"parent_lost=24")&&std::strstr(a.bytes,"first_refusal=64"));
 auto* f=std::fopen(argv[1],"wb");assert(f&&std::fwrite(a.bytes,1,a.length,f)==a.length);std::fclose(f);
 std::puts("PASS actual CRC diagnostic payload formatter, retained first reset, overwritten histories, worst-width identities and nonconsuming retry");
}

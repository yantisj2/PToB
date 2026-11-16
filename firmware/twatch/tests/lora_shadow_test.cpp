// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "lora_shadow.hpp"
#include <cassert>
#include <cstdio>
using watch::LoRaShadow;
static uint8_t ctx[16]={1};
static void pair(LoRaShadow& s,unsigned i,uint64_t edge){s.observe(3,ctx,1,i*2,edge,edge,true);s.observe(4,ctx,1,i*2,edge+2500000,edge,true);s.tick(edge+2500000);}
static LoRaShadow trained(){
 LoRaShadow s;s.tick(1000000);s.observe(2,ctx,1,0,1000000,0,true);
 for(unsigned i=0;i<12;++i)pair(s,i,8000000+30000000ULL*i);
 assert(!s.v.tracking); // Cannot guess META phase from a single packet.
 s.observe(2,ctx,1,24,361000000,0,true);s.tick(361000000);
 assert(s.v.tracking&&s.v.qualified==4&&s.v.meta_phase);return s;
}
int main(){
 auto s=trained();assert(s.v.next_sync==368000000);s.tick(366000000);assert(s.v.would_rx);
 pair(s,12,368000000);assert(s.v.tracking&&s.v.outside==0);
 s.tick(377000000);assert(!s.v.would_rx);for(uint64_t t=377020000;t<=393000000;t+=20000)s.tick(t);assert(s.v.sleep_us>=16000000);
 s.tick(394000000);assert(s.v.would_rx);
 pair(s,13,398000000);assert(s.v.tracking&&s.v.outside==0);
 s.tick(437000001);assert(!s.v.tracking&&s.v.misses==1&&s.v.would_rx);
 s=trained();pair(s,12,377000000);assert(s.v.outside>0&&!s.v.tracking); // late FOLLOW outside frozen window
 s=trained();uint8_t other[16]={2};s.observe(2,other,2,0,362000000,0,true);assert(!s.v.tracking&&!s.v.qualified&&!s.v.meta_phase);
 s=trained();s.tick(362000000,false);assert(!s.v.tracking&&s.v.would_rx&&!s.v.meta_phase);
 s.tick(363000000,false);assert(s.v.inactive_us==1000000);
 s=trained();s.observe(2,ctx,1,24,362000000,0,false);assert(!s.v.tracking);
 s=trained();s.tick(1);assert(!s.v.tracking&&s.v.reason==7);
 s=trained();pair(s,13,368000000);assert(!s.v.tracking&&s.v.qualified==1); // skipped cursor
 s=trained();s.observe(2,ctx,1,24,362000000,0,true);assert(!s.v.tracking); // irregular META => relearn
 puts("PASS shadow-only windows: four pairs plus observed META phase, frozen pre-event coverage, full pair tail, virtual residency, missing/late/context/OFF/withdrawal/rollback/skip recovery");
}

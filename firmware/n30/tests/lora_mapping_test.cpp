// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Focused production CounterMap regression; synthetic counters only.
#include "ptob_gnss_reference.hpp"
#include "lora_mapping.hpp"
#include <cassert>
#include <cstdio>
using ptob_gnss::CounterMap;
static uint32_t capture(uint64_t tick){return uint32_t(tick*4+0xfff00000u);}
static CounterMap trained(uint64_t& last,uint64_t& available){
 CounterMap map;for(unsigned i=0;i<8;i++){
  last=1000000+uint64_t(i)*5000000;available=last+100;
  assert(map.observe({last,last+8,available,capture(last+4)}));assert(map.release(available));
 }
 return map;
}
int main(){
 // Asymmetric software calls can move a single midpoint by several us.
 // Known locked physical counters provide an independent synthetic oracle.
 n30::LoRaCounterMap intersection;CounterMap single;
 uint64_t final=0;
 for(unsigned i=0;i<8;i++){
  const uint64_t physical=1000000+uint64_t(i)*5000000;
  const uint64_t lead=i%2?396:4,trail=i%2?4:396;
  CounterMap::Anchor a{physical-lead,physical+trail,physical+1000,capture(physical)};
  assert(intersection.observe(a)&&intersection.release(a.available));
  assert(single.observe(a)&&single.release(a.available));final=physical;
 }
 const auto sharp=intersection.convert(capture(final+10000),final+11000);
 const auto noisy=single.convert(capture(final+10000),final+11000);
 assert(sharp.valid&&noisy.valid&&sharp.tick==final+10000);
 assert(noisy.tick!=sharp.tick&&sharp.bracket_ns<500&&noisy.bracket_ns>9000);
 assert(!intersection.convert(capture(final+10000),final+9999).valid);
 assert(!intersection.convert(capture(final+20000000),final+40001001).valid);
 // A later anchor cannot change an earlier event, including across wrap.
 assert(intersection.observe({final+5000000-4,final+5000000+396,final+5001000,capture(final+5000000)}));
 assert(intersection.release(final+5001000));
 const auto prior=intersection.convert(capture(final+10000),final+5010000);
 assert(prior.valid&&prior.tick==sharp.tick&&prior.bracket_ns==sharp.bracket_ns);
 uint64_t last,available;auto map=trained(last,available);
 const uint64_t next=last+5000000,edge=next+100000,consumed=edge+1000;
 const auto before=map.convert(capture(edge),consumed);assert(before.valid&&before.tick==edge&&before.anchor_available==available);
 auto existing=map;assert(!existing.observe({next,next+401,next+1000,capture(next+200)}));existing.reset();assert(!existing.convert(capture(edge),consumed).valid);
 auto retained=map;assert(!retained.observe({next,next+401,next+1000,capture(next+200)}));
 const CounterMap::Anchor q{last,last+8,available,capture(last+4)},wide{next,next+401,next+1000,capture(next+200)};
 assert(n30::recoverable_map_bracket(wide,q));
 for(unsigned kind=0;kind<8;kind++){auto a=wide;
  if(kind==0)a.before=0;
  if(kind==1)a.after=a.before-1;
  if(kind==2)a.available=a.after-1;
  if(kind==3)a.before=q.after;
  if(kind==4){a.before=q.after+20000001;a.after=a.before+401;a.available=a.after+100;}
  if(kind==5)a.available=q.available+40000001;
  if(kind==6)a.capture+=1000000;
  if(kind==7)a.after=a.before+400;
  assert(!n30::recoverable_map_bracket(a,q));
 }
 const auto after=retained.convert(capture(edge),consumed);assert(after.valid&&after.tick==before.tick&&after.bracket_ns==before.bracket_ns&&after.anchor_available==before.anchor_available);
 // Rejection cannot move the original anchor's two-second availability expiry.
 assert(retained.convert(capture(last+20000000),available+40000000).valid);
 assert(!retained.convert(capture(last+20000000),available+40000001).valid);
 // Event modulo age, causality and anchors available after consumption refuse.
 assert(!retained.convert(capture(last+40000001),last+40000101).valid);
 assert(!retained.convert(capture(edge),edge-1).valid);
 CounterMap fresh;assert(fresh.observe({last,last+8,available,capture(last+4)}));assert(!fresh.convert(capture(last+4),available-1).valid);
 // A valid later anchor is checked against the retained rate/continuity floor.
 assert(retained.observe({next+5000000,next+5000008,next+5000100,capture(next+5000004)}));
 assert(retained.release(next+5000100));
 for(unsigned kind=0;kind<4;kind++){
  auto bad=trained(last,available);
  if(kind==0)assert(!bad.observe({last-1,last+7,last+100,capture(last+3)}));
  if(kind==1)assert(!bad.observe({last+20000009,last+20000017,last+20000100,capture(last+20000013)}));
  if(kind==2)assert(!bad.observe({last+5000000,last+5000008,last+5000100,uint32_t(capture(last+5000004)+1000)}));
  if(kind==3)assert(!bad.release(last+7));
  assert(!bad.convert(capture(last+10000),last+20000).valid);
 }
 std::puts("PASS: actual owner reset loses otherwise-valid edge after wide soft bracket; retained map preserves same original timestamp/bound/expiry; hard time/rate/release/event-age/causality fences refuse; no hardware access");
}

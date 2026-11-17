// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstring>
namespace watch {
struct LoRaSleepView {uint64_t sleep_us=0,rx_us=0,at=0,last_transition=0;uint32_t sleeps=0,wakes=0,refused=0;bool sleeping=false,active=false;};
// Authenticated receive schedule. No estimator update or permission grant.
class LoRaShadow {
 public:
 static constexpr uint64_t Beat=30000000,Meta=360000000,Lead=2000000,Tail=8000000,WakeLead=0;
 struct View {
  uint64_t next_sync=0,next_meta=0,rx_us=0,sleep_us=0,unknown_us=0,inactive_us=0,last_us=0;
  uint32_t pairs=0,qualified=0,covered=0,outside=0,misses=0,resets=0,meta_seen=0;
  uint8_t reason=0;bool tracking=false,meta_phase=false,would_rx=true,active=false;
 } v;
 private:
 uint8_t context_[16]{};uint64_t epoch_=0,last_sync_=0,last_meta_=0;
 uint32_t cursor_=0;bool context_known_=false;
 static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
 static uint64_t gap(uint64_t a,uint64_t b){return a>b?a-b:b-a;}
 bool covered(uint64_t t)const {
  if(!v.tracking)return true;
  const auto inside=[](uint64_t t,uint64_t x,uint64_t pre,uint64_t post){return x&&t>=(x>pre?x-pre:0)&&(t<=x||t-x<=post);};
  // A completed authenticated pair/META advances its deadline immediately.
  // No trailing dwell after receipt; an outstanding window stays open.
  return inside(t,v.next_sync,Lead,Tail)||inside(t,v.next_meta,Lead,Lead);
 }
 void fence(uint8_t why){if(v.tracking||v.qualified)inc(v.resets);v.tracking=false;v.qualified=0;v.reason=why;v.would_rx=true;v.next_sync=0;v.next_meta=0;v.meta_phase=false;last_sync_=last_meta_=0;}
 public:
 // Drop the complete forecast: a single later packet cannot resume sleep.
 void lose(uint8_t why){fence(why);}
 // Wake two seconds before the physical event; startup fits inside this lead.
 bool receive_due(uint64_t at)const {return covered(at)||at>UINT64_MAX-WakeLead||covered(at+WakeLead);}
 // Reasons:0 learning,1 tracking,2 OFF/fault,3 context,4 missing pair,
 // 5 outside window,6 cadence,7 clock reversal,8 META stale/phase unknown,9 worker/IRQ discontinuity,10 map unavailable.
 void tick(uint64_t now,bool healthy=true){
  if(v.last_us&&now<v.last_us){fence(7);context_known_=false;last_sync_=last_meta_=0;v.meta_phase=false;v.next_meta=0;v.last_us=now;return;}
  if(v.last_us){const auto dt=now-v.last_us;if(!v.active)v.inactive_us+=dt;else if(dt>250000)v.unknown_us+=dt;else if(v.would_rx)v.rx_us+=dt;else v.sleep_us+=dt;}
  v.last_us=now;v.active=healthy;
  if(!healthy){fence(2);context_known_=false;last_sync_=last_meta_=0;v.meta_phase=false;v.next_meta=0;return;}
  if(v.tracking&&v.next_sync&&now>v.next_sync&&now-v.next_sync>Tail){inc(v.misses);fence(4);}
  if(v.meta_phase&&last_meta_&&now>last_meta_&&now-last_meta_>Meta+Lead){v.meta_phase=false;fence(8);}
  v.would_rx=covered(now);
 }
 void observe(uint8_t kind,const uint8_t* context,uint64_t epoch,uint32_t cursor,
              uint64_t at,uint64_t pair_at,bool valid){
  if(!epoch||!context)return;
  // Test the frozen forecast BEFORE this packet can move its windows.
  if(v.tracking){if(covered(at))inc(v.covered);else{inc(v.outside);fence(5);}}
  if(!context_known_||epoch_!=epoch||std::memcmp(context_,context,16)){
   fence(3);std::memcpy(context_,context,16);epoch_=epoch;context_known_=true;
   last_sync_=last_meta_=0;v.meta_phase=false;v.next_meta=0;cursor_=0;
  }
  if(kind==2){ // Authenticated admitted META.
   inc(v.meta_seen);
   v.meta_phase=last_meta_&&at>last_meta_&&gap(at-last_meta_,Meta)<=Lead;
   last_meta_=at;v.next_meta=at<=UINT64_MAX-Meta?at+Meta:0;
   if(!valid){fence(8);v.meta_phase=false;last_sync_=0;}
  }else if(kind==4&&valid&&pair_at){ // Completed authenticated SYNC/FOLLOW pair.
   inc(v.pairs);
   if(last_sync_&&cursor==cursor_+2&&pair_at>last_sync_&&gap(pair_at-last_sync_,Beat)<=Lead){if(v.qualified<4)++v.qualified;}
   else {if(last_sync_)fence(6);v.qualified=1;}
   last_sync_=pair_at;cursor_=cursor;v.next_sync=pair_at<=UINT64_MAX-Beat?pair_at+Beat:0;
  }
  v.tracking=v.qualified>=4&&v.meta_phase&&v.next_sync&&v.next_meta;
  if(v.tracking)v.reason=1;
  v.would_rx=covered(at);
 }
};
}

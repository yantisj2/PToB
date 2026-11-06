// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "lora_clock_selection.hpp"
#include "display_second_deadline.hpp"
#include <cassert>
#include <initializer_list>
int main(){
 w12::LoRaClockSelection s;uint64_t t=1000000;
 auto choose=[&](bool ble,bool lora=true,uint64_t root=1,uint64_t boot=2,uint32_t epoch=3,uint64_t origin=4){return s.select(ble,lora,root,boot,epoch,origin,t);};
 assert(!choose(true));assert(choose(false));
 for(unsigned i=0;i<300;++i){t+=100000;assert(choose(true));}
 t+=100000;assert(!choose(true));
 assert(choose(false));t+=100000;assert(choose(true));
 t+=29900000;assert(choose(true)); // An observation gap cannot complete dwell.
 for(unsigned i=0;i<299;++i){t+=100000;assert(choose(true));}
 t+=100000;assert(!choose(true));
 assert(choose(false));t+=100000;assert(choose(true));
 for(unsigned i=0;i<200;++i){t+=100000;assert(choose(true));}
 t+=100000;assert(choose(true,true,1,9)); // New source boot starts fresh dwell.
 for(unsigned i=0;i<299;++i){t+=100000;assert(choose(true,true,1,9));}
 t+=100000;assert(!choose(true,true,1,9));
 assert(choose(false));t+=100000;assert(!choose(true,false)); // Expired LoRa cannot hide usable BLE.
 t+=100000;assert(!choose(true)); // Expired LoRa retired its latch; recovered BLE retains priority.
 --t;assert(!choose(true));
 // Actual one-second display scheduler consumes a main-cadence observation.
 w12::LoRaClockSelection observed,paint_only;w12::DisplaySecondDeadline paint;
 assert(observed.select(false,true,1,2,3,4,0));assert(paint_only.select(false,true,1,2,3,4,0));
 paint.painted(0,true,1700000000000000);unsigned frames=0;bool displayed=true;
 for(uint64_t now=1000;now<=31000000;now+=1000){
  displayed=observed.select(true,true,1,2,3,4,now);
  if(paint.due(now)){
   ++frames;assert(displayed==(frames<=30));
   assert(paint_only.select(true,true,1,2,3,4,now)); // Negative control: painting alone cannot complete dwell.
   paint.painted(now,true,1700000000000000+now);
  }
 }
 assert(frames==31&&!displayed);
 // CRC diagnostics retain the first interrupted return even after later
 // successful reception; a pause earns no qualification time.
 w12::LoRaClockSelection diagnostic;
 assert(diagnostic.select(false,true,1,2,3,4,1000000));
 assert(diagnostic.select(true,true,1,2,3,4,1001000,false,7,9));
 for(uint64_t at=1002000;at<1376000;at+=1000)
  assert(diagnostic.select(false,true,0,0,0,4,at,true,7,0,216));
 const auto denied=diagnostic.view(1375000);
 assert(!denied.qualified&&denied.first_loss==1002000&&denied.first_root==1&&denied.first_boot==2&&denied.first_refusal==216);
 assert(denied.pause==374000&&denied.remaining==30000000);
 assert(diagnostic.select(true,true,1,2,3,4,1376000,false,7,9));
 const auto resumed=diagnostic.view(1376000);assert(resumed.qualified&&resumed.pause==0&&resumed.first_loss==denied.first_loss&&resumed.resets==1);
 // Repeated bounded permission gaps cannot starve the primary return. Output
 // remains on LoRa in every denied interval, including after qualification.
 for(uint64_t period:{1000000u,6000000u,20000000u})for(uint64_t gap:{15000u,150000u,248000u,300000u,374000u,1200000u}){
  if(gap>=period)continue;
  w12::LoRaClockSelection recurring;assert(recurring.select(false,true,1,2,3,4,1000000));
  uint64_t qualified=0;
  for(uint64_t at=1001000;at<=71000000;at+=1000){
   const bool denied=(at-1000000)%period<gap;
   if(!denied)qualified+=1000;
   const bool selected=recurring.select(!denied,true,denied?0:1,denied?0:2,denied?0:3,4,at,denied,7,9);
   if(denied)assert(selected);
   if(!denied&&qualified>31000000)assert(!selected);
  }
 }
}

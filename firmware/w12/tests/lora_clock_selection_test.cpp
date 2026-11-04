// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "lora_clock_selection.hpp"
#include "display_second_deadline.hpp"
#include <cassert>
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
 t+=100000;assert(choose(true)); // A brief BLE return does not erase selected-LoRa history.
 --t;assert(choose(true));
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
}

// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "dual_clock.hpp"
#include <cassert>

int main(){
 using namespace w12;
 DualClock clock;
 const uint64_t start=20000000,epoch=1700000000000000000;
 auto at=[&](uint64_t tick){return epoch+(tick-start)*50;};
 auto make=[&](uint64_t tick,uint64_t id,uint64_t scope,int32_t phase,int32_t rate=0){
  return ClockInput{uint64_t(int64_t(at(tick))+phase),tick+200000000,id,scope,rate};
 };
 clock.update(start,make(start,1,1,0),make(start,1,2,5000,4000));
 assert(clock.segment(start).mode==ClockMode::Ble);
 auto tick=start+1000000;
 clock.update(tick,{},make(tick,1,2,5000,4000));
 assert(clock.segment(tick).mode==ClockMode::LoRa&&clock.segment(tick).rate==0);
 assert(clock.segment(tick).at(tick)==at(tick));
 assert(clock.segment(tick).at(tick+20000000)==at(tick+20000000)+5000);
 // A changed BLE source starts its own dwell. The LoRa model keeps running.
 tick+=200000;
 clock.update(tick,make(tick,3,9,-3000),make(tick,1,2,5000,4000));
 assert(clock.segment(tick).mode==ClockMode::LoRa);
 tick+=1000000;
 clock.update(tick,{},make(tick,1,2,5000,4000));
 tick+=1100000;
 const auto prior=clock.segment(tick).at(tick);
 clock.update(tick,make(tick,4,9,-3000),make(tick,1,2,5000,4000));
 assert(clock.segment(tick).mode==ClockMode::Ble&&clock.segment(tick).at(tick)==prior);
 // Out-of-epoch BLE cannot replace an admitted coarse clock.
 tick+=100000;
 clock.update(tick,make(tick,5,9,1100000000),make(tick,1,2,5000,4000));
 assert(clock.segment(tick).mode==ClockMode::LoRa);
 tick+=600000000;
 clock.update(tick,{},make(tick,2,2,5000,4000));
 assert(clock.segment(tick).rate==500);
 const auto end=clock.segment(tick).until;
 assert(clock.segment(end).mode_at(end)==ClockMode::Hold);
 assert(clock.segment(end+1).at(end+1)>clock.segment(end).at(end));
 clock.update(end+1,{},{});
 assert(clock.segment(end+1).mode==ClockMode::Hold);
 assert(clock.segment(tick).mode_at(tick)==ClockMode::LoRa);
}

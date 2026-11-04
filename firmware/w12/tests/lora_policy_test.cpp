// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "lora_policy.hpp"
#include <cassert>
int main(){
 w12::LoRaPolicy p;
 for(unsigned i=0;i<1000;i++){p.update(i%2,i*1000000);assert(p.requested()&&p.remaining_ms(i*1000000)==0);}
 p.set(w12::LoRaMode::KeepOff);p.update(false,0,true);assert(!p.requested());
 p.toggle();assert(p.mode()==w12::LoRaMode::Auto&&p.requested());
 p.toggle();assert(p.mode()==w12::LoRaMode::KeepOff&&!p.requested());
 p.set(w12::LoRaMode::KeepOn);p.update(true,1);assert(p.requested());p.toggle();assert(!p.requested());
}

// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "display_second_deadline.hpp"
#include <cassert>
int main(){
 w12::DisplaySecondDeadline d;assert(d.due(0));
 d.painted(4000000,true,1700000000900000ULL);
 assert(d.deadline()==4100000&&!d.due(4099999)&&d.due(4100000));
 // A forced repaint at a different phase retains the UTC boundary.
 d.painted(4050000,true,1700000000950000ULL);assert(d.deadline()==4100000);
 // A delayed frame is immediately due if its transfer crossed the boundary.
 assert(d.due(4110000));d.painted(4110000,true,1700000001010000ULL);assert(d.deadline()==5100000);
 // New clock/rollback uses the newly selected time, not the old cadence.
 d.painted(4200000,true,1700000000350000ULL);assert(d.deadline()==4850000);
 d.painted(1,false,0);assert(d.deadline()==1000001);
 d.painted(UINT64_MAX-10,true,1);assert(d.deadline()==UINT64_MAX);
 for(uint64_t phase=0;phase<1000000;phase+=7919){
  d.painted(10000000,true,1700000000000000ULL+phase);
  assert((phase+d.deadline()-10000000)%1000000==0);
 }
}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#include "g1_parent_selection.hpp"
#include <cassert>
#include <cstdio>
using v12::G1ParentSelection;
int main(){
 constexpr uint64_t start=1000000,minute=60000000,failed=start+minute;
 G1ParentSelection p{minute};p.seen(11,false,-20,start);p.bind(11,111,1,start);
 p.locked(false,failed);assert(p.needs_alternative());
 p.seen(11,false,-20,failed);assert(p.choose(failed)==0&&p.parent()==11);
 assert(!p.retry_allowed(11,false,failed+minute-1));
 p.seen(22,true,-85,failed);assert(p.choose(failed)==22&&p.retry_allowed(22,true,failed));
 // A JOIN renewal/epoch change cannot reset the failed-parent retry bound.
 p.bind(11,111,2,failed+1);assert(!p.retry_allowed(11,false,failed+minute-1));
 assert(p.retry_allowed(11,false,failed+minute));p.attempted(11,false,failed+minute);
 assert(!p.retry_allowed(11,false,failed+2*minute-1));assert(p.retry_allowed(11,false,failed+2*minute));
 p.unavailable(failed+minute+1);assert(!p.retry_allowed(11,false,failed+2*minute-1));
 p.bind(11,111,3,failed+minute+2);assert(!p.retry_allowed(11,false,failed+2*minute-1));
 // Explicit operator choices bypass automatic cooldown, never source validity.
 p.target(11,false);assert(p.retry_allowed(11,false,failed+minute+3));p.target(0);
 assert(!p.retry_allowed(11,false,failed+minute+3));
 // Sub-two-second lock pulses cannot turn retries back into a tight loop.
 for(uint64_t t=failed+minute+100000;t<=failed+minute+1100000;t+=62500)p.locked(true,t);
 p.locked(false,failed+minute+1200000);assert(p.needs_alternative()&&!p.retry_allowed(11,false,failed+minute+1200000));
 for(uint64_t t=failed+minute+2000000;t<=failed+minute+4000000;t+=62500)p.locked(true,t);
 assert(!p.needs_alternative()&&p.retry_allowed(11,false,failed+minute+4000000));
 // Reset keeps the configured retry policy. Default W12 policy stays unchanged.
 p.reset();p.seen(11,false,-20,start);p.bind(11,111,1,start);p.locked(false,failed);
 assert(!p.retry_allowed(11,false,failed));
 G1ParentSelection legacy;legacy.seen(11,false,-20,start);legacy.bind(11,111,1,start);legacy.locked(false,failed);
 legacy.seen(11,false,-20,failed);assert(legacy.retry_allowed(11,false,failed)&&legacy.choose(failed)==11);
 std::puts("PASS failed-parent retry boundary, renewal, unavailable/rebind, manual choice, lock recovery, reset and unchanged W12 default");
}

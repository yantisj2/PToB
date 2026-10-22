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
 // Source/replay withdrawal clears the binding, not the identity of its
 // bounded retry. A failed trial must not blacklist the sole returning GM
 // indefinitely, even after the advisory five-minute history expires.
 G1ParentSelection returning{minute};returning.seen(11,false,-30,start);returning.bind(11,111,1,start);
 returning.locked(false,failed);returning.unavailable(failed+1);
 returning.seen(11,false,-30,failed+minute-1);assert(returning.choose(failed+minute-1)==0);
 returning.seen(11,false,-30,failed+minute);assert(returning.choose(failed+minute)==11&&returning.parent()==0);
 returning.attempted(11,false,failed+minute);assert(!returning.retry_allowed(11,false,failed+2*minute-1));
 returning.seen(11,false,-30,failed+2*minute-1);assert(returning.choose(failed+2*minute-1)==0);
 returning.seen(11,false,-30,failed+2*minute);returning.seen(22,true,-85,failed+2*minute);
 assert(returning.choose(failed+2*minute)==22); // Fresh alternatives still win.
 const auto late=failed+10*minute;returning.seen(11,false,-30,late);assert(returning.choose(late)==11);
 assert(returning.choose(late+G1ParentSelection::FreshUs)==0); // No stale locator reuse.
 returning.target(22,true);returning.seen(11,false,-20,late+1);assert(returning.choose(late+1)==0);
 returning.target(0);returning.diagnostic(true);assert(returning.choose(late+2)==0); // Relay-only stays relay-only.
 G1ParentSelection legacy;legacy.seen(11,false,-20,start);legacy.bind(11,111,1,start);legacy.locked(false,failed);
 legacy.seen(11,false,-20,failed);assert(legacy.retry_allowed(11,false,failed)&&legacy.choose(failed)==11);
 std::puts("PASS failed-parent retry boundary, renewal, unavailable/rebind, manual choice, lock recovery, reset and unchanged W12 default");
}

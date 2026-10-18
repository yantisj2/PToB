// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "g1_parent_selection.hpp"
#include <cassert>
#include <cstdio>
using v12::G1ParentSelection;
using v12::G1ParentHistory;
namespace {
unsigned checks=0;
#define CHECK(x) do{assert(x);++checks;}while(false)
void hints(G1ParentSelection& p,uint64_t now,bool relay=true){
 p.seen(11,false,-20,now);if(relay)p.seen(22,true,-85,now,true);
}
void bind(G1ParentSelection& p,uint64_t now,uint64_t id=11,bool relay=false){
 p.seen(id,relay,relay?-85:-20,now,relay);p.bind(id,100+id,1,now);
}
void traffic(G1ParentSelection& p,uint64_t start,uint64_t end,bool locked){
 for(auto t=start;t<=end;t+=62500){
  p.usable(true,t);p.tick(t,true,false);p.delivered(3,uint16_t(t/62500),t);p.locked(locked,t);
 }
}
}
int main(){
 constexpr uint64_t Start=1000000,Minute=60000000;
 // The field failure: authenticated corrected delivery and FOLLOW are good,
 // while raw precision lock never arrives. Neither renewals nor a new source
 // epoch may give the same GM a fresh minute forever.
 {G1ParentSelection p;bind(p,Start);
  traffic(p,Start,Start+Minute-62500,false);
  CHECK(p.quality().healthy&&!p.quality().poor&&!p.needs_alternative());
  hints(p,Start+Minute-1);p.bind(11,111,2,Start+Minute-1);
  p.locked(false,Start+Minute-1);CHECK(!p.needs_alternative());
  CHECK(p.choose(Start+Minute-1)==11);
  p.locked(false,Start+Minute);hints(p,Start+Minute);
  CHECK(p.needs_alternative()&&p.seeking_relay());CHECK(p.deferred(11,false,Start+Minute));
  CHECK(p.choose(Start+Minute)==22); // Try even the weaker fresh relay.
  CHECK(p.selection_view(Start+Minute).reason==G1ParentSelection::Decision::LockTrial);
  p.attempted(22,true,Start+Minute+1);CHECK(p.needs_alternative()); // Failed or pending relay probes cannot re-protect the GM.
  // Packet/usable samples cannot erase precision failure.
  traffic(p,Start+Minute,Start+Minute+1000000,false);
  hints(p,Start+Minute+1000000);CHECK(p.choose(Start+Minute+1000000)==22);
 }
 // No eligible relay means keep the current authorized source while seeking;
 // do not invent a stale locator or throw away its timing map to force a trial.
 {G1ParentSelection p;bind(p,Start);p.locked(false,Start+Minute);
  hints(p,Start+Minute,false);CHECK(p.needs_alternative());CHECK(p.choose(Start+Minute)==11);
  p.seen(22,true,-30,Start+Minute-200000);CHECK(p.choose(Start+Minute)==11);
  p.seen(22,true,-30,Start+Minute+1);CHECK(p.choose(Start+Minute+1)==22);
 }
 // A relay gets a real trial. A reachable failed GM cannot immediately pull
 // it back after the old10s dwell or its30s re-observation opportunity.
 {G1ParentSelection p;bind(p,Start);p.locked(false,Start+Minute);hints(p,Start+Minute);
  CHECK(p.choose(Start+Minute)==22);bind(p,Start+Minute+1,22,true);
  traffic(p,Start+Minute+62500,Start+Minute+30000000,true);
  hints(p,Start+Minute+30000000);CHECK(!p.needs_alternative());CHECK(p.choose(Start+Minute+30000000)==22);
  hints(p,Start+Minute+299000000);CHECK(p.choose(Start+Minute+299000000)==22);
  hints(p,Start+Minute+300000000);CHECK(!p.deferred(11,false,Start+Minute+300000000));
  CHECK(p.choose(Start+Minute+300000000)==11);
 }
 // A brief dip does not trigger discovery; a full minute since actual lock
 // does. Recovering that raw lock clears the failure without an admission.
 {G1ParentSelection p;bind(p,Start);traffic(p,Start+8000000,Start+10000000,true);
  p.locked(false,Start+Minute);CHECK(!p.needs_alternative());
  p.locked(false,Start+Minute+9999999);CHECK(!p.needs_alternative());
  p.locked(false,Start+Minute+10000000);CHECK(p.needs_alternative());
  traffic(p,Start+Minute+10000001,Start+Minute+12000001,true);
  CHECK(!p.needs_alternative());CHECK(!p.deferred(11,false,Start+Minute+12000001));
 }
 // Sub-two-second pulses and unobserved gaps must not hide an unlocked GM.
 {G1ParentSelection p;bind(p,Start);
  for(unsigned i=0;i<20;i++){
   const auto at=Start+i*3000000;traffic(p,at,at+1000000,true);p.locked(false,at+1062500);
  }
  p.locked(false,Start+Minute);CHECK(p.needs_alternative());
 }
 {G1ParentSelection p;bind(p,Start);
  for(unsigned i=0;i<=60;i++)p.locked(true,Start+i*1000000);
  CHECK(p.needs_alternative()); // No continuous observed lock from one sample/second.
 }
 // Source/replay withdrawal is not a new GM trial. Repeated unavailable/
 // rebinds cannot erase the original no-lock minute.
 {G1ParentSelection p;bind(p,Start);
  for(unsigned i=1;i<6;i++){p.unavailable(Start+i*10000000);hints(p,Start+i*10000000);p.bind(11,111,i+1,Start+i*10000000);p.locked(false,Start+i*10000000);}
  p.locked(false,Start+Minute);hints(p,Start+Minute);CHECK(p.choose(Start+Minute)==22);
 }
 // Unusable authenticated JOIN replies can leave no parent binding at all.
 // An actual attempted GM still has a bounded trial, never an infinite GM-first prior.
 {G1ParentSelection p;p.bind(99,199,1,1);p.reset();hints(p,Start);p.attempted(11,false,Start);
  p.locked(false,Start+Minute-1);hints(p,Start+Minute-1);CHECK(p.choose(Start+Minute-1)==11);
  p.locked(false,Start+Minute);hints(p,Start+Minute);CHECK(p.needs_alternative());CHECK(p.choose(Start+Minute)==22);
 }
 // A failed relay can also try another, rather than clinging to its stronger
 // RSSI. Explicit named parents and relay-only constraints still apply.
 {G1ParentSelection p;bind(p,Start,22,true);p.locked(false,Start+Minute);
  p.seen(22,true,-20,Start+Minute);p.seen(33,true,-90,Start+Minute,true);
  CHECK(p.choose(Start+Minute)==33);
  p.target(22,true);CHECK(!p.needs_alternative());CHECK(p.choose(Start+Minute)==22);
 }
 {G1ParentSelection p;bind(p,Start);p.target(11,false);p.locked(false,Start+Minute);hints(p,Start+Minute);
  CHECK(!p.needs_alternative());CHECK(p.choose(Start+Minute)==11);
 }
 // Advisory deferral consumes existing row padding, survives locator resets,
 // expires, and never becomes a clock validity or admission permission.
 {G1ParentHistory h;h.lock_failed(11,false,0);CHECK(h.lock_deferred(11,false,299999999));
  CHECK(!h.lock_deferred(11,false,300000000));h.lock_failed(11,false,400000000);
  h.sample(11,false,1000,true,1000,401000000);CHECK(h.lock_deferred(11,false,401000000));
  h.locked(11,false,401000000);CHECK(!h.lock_deferred(11,false,401000000));
  CHECK(sizeof(G1ParentHistory::Row)==40&&sizeof(G1ParentHistory)==656);
 }
 {G1ParentSelection p;bind(p,Start);p.locked(false,Start+Minute);p.reset();hints(p,Start+Minute+1);
  CHECK(p.deferred(11,false,Start+Minute+1));CHECK(p.choose(Start+Minute+40001)==22);
 }
 std::printf("PASS %u lock-dwell/fallback assertions; synthetic policy, not RF qualification\n",checks);
}

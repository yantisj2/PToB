// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include <cassert>
#include <cstdio>
#include <initializer_list>
extern "C" {
#include "group_join.h"
}

int main(){
 r2_join_record r{};r.kind=R2_JOIN_ACCEPT;r.sender=501;r.receiver=777;
 r.sender_boot=601;r.receiver_boot=877;r.client_nonce=1;r.server_nonce=2;
 r.sequence=2;r.created_us=100000000;r.source_flags=1;r.source_bound_us=40000;
 r.root=501;r.root_boot=601;r.source_epoch=2;r.publication=3;
 r.utc_us=1700000000000000;std::snprintf(r.name,sizeof r.name,"GM");
 // Measured delayed admission shapes: valid inside250ms, too wide for the
 // legacy100ms budget. Coarse uncertainty must remain fully represented.
 struct Row{uint64_t rtt;uint32_t remaining,uncertainty,guard;};
 const Row rows[]={{160761,1042399,120767,201147},{138695,1173566,109690,179037},
                   {139290,1290439,109988,179633},{148146,838849,114434,188507}};
 constexpr uint64_t sent=100000000;
 for(const auto& row:rows){
  r.remaining_us=row.remaining;r2_join_time t{};
  assert(!r2_group_join_time(&t,&r,sent,sent+row.rtt,40000));
  assert(r2_group_join_time_limit(&t,&r,sent,sent+row.rtt,40000,200000));
  assert(t.uncertainty_us==row.uncertainty&&t.utc_us==r.utc_us+row.rtt/2);
  assert(t.until==sent+row.rtt+row.remaining-row.guard);
  const auto at=sent+row.rtt,deadline=r2_group_anchor_deadline(at,t.uncertainty_us);
  uint64_t upper=0;assert(deadline>at&&r2_group_coarse_upper(&t,at,deadline-1,&upper));
  assert(!r2_group_coarse_upper(&t,at,deadline+100000,&upper));
  r.remaining_us=row.guard;assert(!r2_group_join_time_limit(&t,&r,sent,at,40000,200000));
 }
 r.remaining_us=1500000;r2_join_time t{};
 assert(!r2_group_join_time_limit(&t,&r,sent,sent+250000,40000,200000));
 assert(!r2_group_join_time_limit(&t,&r,sent,sent-1,40000,200000));
 for(uint32_t limit:{0u,200001u})assert(!r2_group_join_time_limit(&t,&r,sent,sent+140000,40000,limit));
 for(uint32_t bound:{0u,50001u})assert(!r2_group_join_time_limit(&t,&r,sent,sent+140000,bound,200000));
 // A tiny local declared bound cannot hide the signed source's50ms bound.
 r.source_bound_us=50000;assert(r2_group_join_time_limit(&t,&r,sent,sent+249999,1,200000));
 assert(t.uncertainty_us==175564&&t.until==sent+1500000-50564);
 r.source_flags=0;assert(!r2_group_join_time_limit(&t,&r,sent,sent+140000,40000,200000));
 std::puts("PASS delayed coarse JOIN retains actual uncertainty/full source expiry,250ms RTT and bounded replay; legacy100ms policy unchanged");
}

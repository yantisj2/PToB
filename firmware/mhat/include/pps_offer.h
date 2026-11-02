/* Copyright (c) 2026 Jonathan Yantis. MIT. Semantic immutable-offer identity. */
#pragma once
#include "rc_unicast.h"
#include <string.h>
static inline bool mhat_pps_revision_current(uint32_t snapshot,uint32_t current){
 /* Saturation is a permanent withdrawal fence, never a valid ABA revision. */
 return snapshot!=UINT32_MAX&&snapshot==current;
}
static inline bool mhat_pps_offer_same(const struct r2_uc_clock *a,const struct r2_uc_clock *b){
 /* Never compare C padding: identical owner polls must not invalidate planning.
  * Include authority/expiry as well as phase; a lease change is not a no-op. */
#define SAME(member) (a->member==b->member)
 return SAME(valid)&&SAME(estimate)&&SAME(capture_epoch)&&SAME(depth)&&
  SAME(until_tick)&&SAME(session_until)&&SAME(discipline_tick)&&!memcmp(a->scope,b->scope,16)&&
  SAME(map.effective)&&SAME(map.anchor_tick)&&SAME(map.anchor_ns)&&SAME(map.ppb)&&
  SAME(map.phase_ns)&&SAME(map.generation)&&SAME(map.age_us)&&SAME(map.bound)&&
  SAME(map.rate_bound)&&SAME(map.capture_bound)&&SAME(map.continuous)&&
  SAME(origin.root)&&SAME(origin.boot)&&SAME(origin.epoch)&&SAME(origin.issued)&&
  SAME(origin.expires)&&SAME(origin.utc_offset)&&SAME(origin.frame)&&
  SAME(origin.hold_us)&&SAME(origin.hold_ns);
#undef SAME
}

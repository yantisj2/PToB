/* Copyright (c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Transport continuity only; no time, source permission or uncertainty bound.
 * The owner clears this timer when its actual train/path is drained. */
struct rc_transport_hold {uint64_t deadline,recovered,last;bool armed;};
_Static_assert(sizeof(struct rc_transport_hold)<=32,"RC transport hold RAM budget");
#define RC_TRANSPORT_HOLD_TICKS UINT64_C(480000000)
#define RC_TRANSPORT_RECOVERY_TICKS UINT64_C(32000000)
#define RC_TRANSPORT_OBSERVATION_TICKS UINT64_C(4000000)
static inline void rc_transport_hold_clear(struct rc_transport_hold *h){
 *h=(struct rc_transport_hold){0};
}
static inline bool rc_transport_hold_update(struct rc_transport_hold *h,
    uint64_t tick,bool established,bool timing){
 if(!established||!tick||(h->last&&tick<h->last)){
  rc_transport_hold_clear(h);return false;
 }
 if(h->last&&tick-h->last>RC_TRANSPORT_OBSERVATION_TICKS)h->recovered=0;
 h->last=tick;
 if(h->deadline&&tick>=h->deadline){rc_transport_hold_clear(h);return false;}
 if(timing){
  h->armed=true;
  if(h->deadline){
   if(!h->recovered)h->recovered=tick;
   if(tick-h->recovered>=RC_TRANSPORT_RECOVERY_TICKS)h->deadline=h->recovered=0;
  }
  return true;
 }
 h->recovered=0;
 if(!h->armed)return false;
 if(!h->deadline){
  if(tick>UINT64_MAX-RC_TRANSPORT_HOLD_TICKS){rc_transport_hold_clear(h);return false;}
  h->deadline=tick+RC_TRANSPORT_HOLD_TICKS;
 }
 return true;
}

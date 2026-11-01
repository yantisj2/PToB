/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "gm7_owner.h"
/* Called before commit clears the prior selection. Blackout is insufficient. */
static inline bool lp_gm_handoff(const struct gm7_owner *o,uint64_t primary,uint64_t action,uint64_t now){
 const struct gm7_control *s=&o->selected;
 if(!primary||s->sender!=primary||s->active!=primary||s->boot!=s->active_boot||
    !o->armed||s->hot!=o->identity||s->hot_boot!=o->boot)return false;
 bool withdrawn=false;
 for(unsigned i=0;i<GM7_PEERS;i++){
  const struct gm7_peer *p=&o->peer[i];
  if(p->used&&!p->quarantined&&p->identity==primary&&p->boot==s->boot&&
     p->kind==GM7_WITHDRAW&&now<p->received.control_until&&p->received.control.active==primary&&
     p->received.control.term==s->term&&p->received.control.birth_us==s->birth_us&&
     p->received.control.founder==s->founder)withdrawn=o->withdrawn;
 }
 bool yielded=s->yield_utc_us&&o->remote_yield_sequence==s->sequence&&
              o->remote_yield_after&&action>=o->remote_yield_after;
 return withdrawn||yielded;
}
/* A returning primary may not restart LoRa from a cold partition election.
 * Fresh authenticated non-serving backup evidence admits this new train once.
 * This evidence only restricts admission; it cannot create GM authority. */
static inline bool lp_gm_readmitted(const struct gm7_owner *o,uint64_t backup,uint64_t now){
 if(!backup)return true;
 for(unsigned i=0;i<GM7_PEERS;i++){
  const struct gm7_peer *p=&o->peer[i];
  if(p->used&&!p->quarantined&&p->identity==backup&&p->boot&&p->heard&&
     !p->heard_rc&&p->heard_at<=now&&now-p->heard_at<UINT64_C(2500000)&&
     p->reported.known&&now<p->reported.until&&p->reported.role!=GM7_W_ACTIVE&&
     p->observed.birth==o->scope.birth&&p->observed.founder==o->scope.founder&&
     p->observed.term==o->scope.term&&p->observed.active==o->identity&&
     p->observed.boot==o->boot)return true;
 }
 return false;
}

/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 * Phase-blind startup qualification for the UI witness statistics only.
 * Raw PPS records and the delivered clock are never modified or suppressed. */
#pragma once
#include "rc_shadow.h"
#include <string.h>
#include <limits.h>
#define R2_SHADOW_SETTLE_TICKS UINT64_C(160000000) /* Ten seconds at 16 MHz. */
#define R2_SHADOW_SETTLE_GAP_TICKS UINT64_C(24000000) /* At most 1.5 s between PPS. */
struct r2_shadow_settle {
 uint64_t boundary,epoch,first,last,root,root_boot,parent,parent_boot;
 uint32_t cursor,root_epoch,parent_epoch,train,seen,skipped,missing,resets;
 bool collecting,tracking;
};
static inline void r2_shadow_settle_add(uint32_t *n,uint32_t add){*n=add>UINT32_MAX-*n?UINT32_MAX:*n+add;}
static inline void r2_shadow_settle_reset(struct r2_shadow_settle *s){
 if(s->tracking)r2_shadow_settle_add(&s->resets,1);
 s->tracking=false;s->first=s->last=0;
}
static inline void r2_shadow_settle_begin(struct r2_shadow_settle *s,const struct rc_shadow_status *st){
 memset(s,0,sizeof *s);s->collecting=true;s->boundary=st->now_tick;s->epoch=st->epoch;s->cursor=st->edges;
}
static inline bool r2_shadow_settle_observe(struct r2_shadow_settle *s,const struct rc_shadow_record *r,bool usable){
 if(!s->collecting||r->sequence<=s->cursor)return false;
 uint32_t gap=r->sequence-s->cursor-1;s->cursor=r->sequence;
 r2_shadow_settle_add(&s->seen,1);r2_shadow_settle_add(&s->missing,gap);
 if(gap)r2_shadow_settle_reset(s);
 if(!usable||r->arrival_us<0||r->edge.epoch!=s->epoch||r->edge.tick<=s->boundary){
  r2_shadow_settle_add(&s->skipped,1);r2_shadow_settle_reset(s);return false;
 }
 if(s->tracking&&(r->edge.tick<=s->last||r->edge.tick-s->last>R2_SHADOW_SETTLE_GAP_TICKS||
    s->root!=r->map.root||s->root_boot!=r->map.root_boot||s->root_epoch!=r->map.root_epoch||
    s->parent!=r->map.parent||s->parent_boot!=r->map.parent_boot||
    s->parent_epoch!=r->map.parent_epoch||s->train!=r->map.train))r2_shadow_settle_reset(s);
 if(!s->tracking){s->tracking=true;s->first=r->edge.tick;s->root=r->map.root;s->root_boot=r->map.root_boot;
  s->root_epoch=r->map.root_epoch;s->parent=r->map.parent;s->parent_boot=r->map.parent_boot;
  s->parent_epoch=r->map.parent_epoch;s->train=r->map.train;}
 s->last=r->edge.tick;
 return s->last-s->first>=R2_SHADOW_SETTLE_TICKS;
}
static inline uint32_t r2_shadow_settle_ms(const struct r2_shadow_settle *s){
 return s->tracking?(uint32_t)((s->last-s->first)/16000):0;
}

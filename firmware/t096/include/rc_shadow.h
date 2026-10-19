/* SPDX-License-Identifier: Apache-2.0
 * Measurement only. A bounded history of immutable map availability intervals.
 * No source/servo/admission calls are made from witness inputs. */
#pragma once
#include "rc_boundary.h"
#include "precision.h"
#include "clock.h"
#define RC_SHADOW_HISTORY 16u
#define RC_SHADOW_RECORDS 16u
#define RC_SHADOW_QUEUE 8u
#define RC_SHADOW_HOLD_TICKS UINT64_C(8000000)
enum rc_shadow_reason { RC_SHADOW_OK,RC_SHADOW_NO_MAP,RC_SHADOW_ACTIVATION_RACE,RC_SHADOW_HISTORY_MISSING,RC_SHADOW_EXPIRED,RC_SHADOW_BAD_CAPTURE,RC_SHADOW_ARITHMETIC };
struct rc_shadow_map {
#ifdef R2_PC_PROFILE
 bool experimental; /* PC-only projection; never operating permission. */
#endif
 struct rc5_map map;uint64_t lo,active,epoch,parent,parent_boot,root,root_boot,issued,source_expires;
 int64_t utc_offset;uint32_t root_epoch,parent_epoch,train,accepted;
 r2_servo_map_t normal_map;uint64_t normal_until;bool normal;
};
struct rc_shadow_history {struct rc_shadow_map map[RC_SHADOW_HISTORY];unsigned head,count;};
static inline enum rc_shadow_reason rc_shadow_select(const struct rc_shadow_history *h,struct r2_capture edge,struct rc_shadow_map *out){
 if(!edge.epoch||edge.kind!=R2_CAPTURE_PPS)return RC_SHADOW_BAD_CAPTURE;
 for(unsigned i=0;i<h->count;i++){
  const struct rc_shadow_map *m=&h->map[(h->head+RC_SHADOW_HISTORY-1-i)%RC_SHADOW_HISTORY];
  if(m->epoch!=edge.epoch)continue;
  /* The actual publication is between these two hardware reads. Do not claim
   * an ordering for a simultaneous edge, even if it could use the old map. */
  if(edge.tick>=m->lo&&edge.tick<=m->active)return RC_SHADOW_ACTIVATION_RACE;
  if(m->active<edge.tick){*out=*m;return edge.tick<m->map.effective||(m->normal?edge.tick>=m->normal_until:edge.tick-m->map.effective>=RC_SHADOW_HOLD_TICKS)?RC_SHADOW_EXPIRED:RC_SHADOW_OK;}
 }
 return h->count==RC_SHADOW_HISTORY?RC_SHADOW_HISTORY_MISSING:RC_SHADOW_NO_MAP;
}
struct rc_shadow_record {
 struct r2_capture edge;struct rc_shadow_map map;uint64_t relay_ns;int64_t utc_ns,phase_ns,rmc_utc_us,rmc_at_us,arrival_us;
 uint32_t sequence,reason,association,reference_reason;bool valid,pll,reference_fix,reference_pps,reference_seen;
};
struct rc_shadow_status {uint32_t edges,drops,overwritten,disabled,duplicates;bool enabled;uint64_t now_tick,epoch;};
void r2_shadow_map(const struct rc5_clock *,const struct rc5_state *,uint64_t,uint64_t);
/* Corrected normal follower map, frozen by its owning PT worker only. */
void r2_shadow_normal_map(const struct rc_shadow_map *);
void r2_shadow_normal_withdraw(void);
void r2_shadow_edge(struct r2_capture,int64_t); /* IRQ: bounded copy only */
void r2_shadow_poll(void); /* RC worker: may evaluate the RC witness map */
void r2_shadow_poll_normal(void); /* Normal owner: refuses RC witness maps */
void r2_shadow_reference(struct r2_capture,const struct r2_clock *);
void r2_shadow_enable(bool);
bool r2_shadow_enabled(void);
void r2_shadow_serving(bool); /* Actual serving role fences collection. */
bool r2_shadow_read(uint32_t,struct rc_shadow_record *,struct rc_shadow_status *);

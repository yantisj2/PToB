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
 r2_servo_map_t normal_map;uint64_t normal_until;bool normal,lora,retired,local_hold;
 uint32_t lora_delay_ns; /* Uses prior trailing padding; observation only. */
};
struct rc_shadow_history {struct rc_shadow_map map[RC_SHADOW_HISTORY];unsigned head,count;};
static inline enum rc_shadow_reason rc_shadow_select(const struct rc_shadow_history *h,struct r2_capture edge,struct rc_shadow_map *out){
 if(!edge.epoch||edge.kind!=R2_CAPTURE_PPS)return RC_SHADOW_BAD_CAPTURE;
 for(unsigned i=0;i<h->count;i++){
  const struct rc_shadow_map *m=&h->map[(h->head+RC_SHADOW_HISTORY-1-i)%RC_SHADOW_HISTORY];
  if(m->retired||m->epoch!=edge.epoch)continue;
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
struct rc_shadow_status {uint32_t edges,drops,overwritten,disabled,duplicates;bool enabled;uint8_t request_mask;uint64_t now_tick,epoch;};
/* A statistical population, never a clock-selection or admission key. Changes
 * split UI sessions; ordinary map refreshes/accepted counts do not. */
static inline uint8_t rc_shadow_input_kind(const struct rc_shadow_map *m){
 return m->lora?3:m->local_hold?2:m->normal?1:0;
}
struct rc_shadow_population {
 uint64_t root,root_boot,parent,parent_boot;
 uint32_t root_epoch,parent_epoch,train,lora_delay_ns;
 uint8_t input;bool valid;
};
static inline void rc_shadow_population_set(struct rc_shadow_population *p,const struct rc_shadow_map *m){
 *p=(struct rc_shadow_population){m->root,m->root_boot,m->parent,m->parent_boot,
  m->root_epoch,m->parent_epoch,m->train,m->lora_delay_ns,rc_shadow_input_kind(m),true};
}
static inline bool rc_shadow_population_same(const struct rc_shadow_population *p,const struct rc_shadow_map *m){
 return p->root==m->root&&p->root_boot==m->root_boot&&p->parent==m->parent&&p->parent_boot==m->parent_boot&&
  p->root_epoch==m->root_epoch&&p->parent_epoch==m->parent_epoch&&p->train==m->train&&
  p->lora_delay_ns==m->lora_delay_ns&&p->input==rc_shadow_input_kind(m);
}
_Static_assert(sizeof(struct rc_shadow_status)==40,"Witness request mask must use existing padding");
void r2_shadow_map(const struct rc5_clock *,const struct rc5_state *,uint64_t,uint64_t);
/* Corrected normal follower map, frozen by its owning PT worker only. */
void r2_shadow_normal_map(const struct rc_shadow_map *);
void r2_shadow_normal_withdraw(void);
/* Withdraw only this observer input; never retire a newer BLE map. */
void r2_shadow_lora_withdraw(void);
void r2_shadow_edge(struct r2_capture,int64_t); /* IRQ: bounded copy only */
void r2_shadow_poll(void); /* RC worker: may evaluate the RC witness map */
void r2_shadow_poll_normal(void); /* Normal owner: refuses RC witness maps */
void r2_shadow_reference(struct r2_capture,const struct r2_clock *);
void r2_shadow_enable(bool);
/* Independent explicit diagnostic request. UI changes cannot cancel it.
 * Bit1 UI and bit2 manual are reported; serving-GM exclusion remains final. */
void r2_shadow_manual(bool);
bool r2_shadow_enabled(void);
void r2_shadow_serving(bool); /* Actual serving role fences collection. */
bool r2_shadow_read(uint32_t,struct rc_shadow_record *,struct rc_shadow_status *);

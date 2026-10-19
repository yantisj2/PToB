/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "pc_experiment.h"
#include <errno.h>
#include <limits.h>
#include <string.h>

void pc_init(struct pc_experiment *p){memset(p,0,sizeof *p);}
bool pc_ingress_live(bool selected,bool synced,const struct rc5_state *s,uint64_t tick){
 return selected&&synced&&s&&s->bound&&tick<s->context_until&&s->reference.valid&&
  s->reference.completed_at&&tick>=s->reference.completed_at&&
  tick-s->reference.completed_at<UINT64_C(8000000);
}
static void fail(struct pc_experiment *p){p->phase=PC_FAILED;if(p->failures<UINT32_MAX)++p->failures;}
int pc_begin(struct pc_experiment *p,unsigned mode,uint32_t listen_ms,uint32_t hold_ms,uint64_t tick,uint64_t epoch){
 if(mode<PC_WARM_SHADOW||mode>PC_COLD_RADIO||!listen_ms||listen_ms>120000||!hold_ms||hold_ms>300000||!tick||!epoch)return -EINVAL;
 if(p->phase==PC_ACQUIRE||p->phase==PC_LISTEN||p->phase==PC_HOLD)return -EBUSY;
 if(p->trial==UINT32_MAX||tick>UINT64_MAX-UINT64_C(600000)*PC_TICKS_MS)return -EOVERFLOW;
 ++p->trial;p->mode=mode;p->phase=PC_ACQUIRE;p->listen_ms=listen_ms;p->hold_ms=hold_ms;
 p->requested=p->last_tick=tick;p->epoch=epoch;p->first_pair=p->listen_until=p->hold_from=p->hold_until=p->rf_closed=0;
 p->pairs=p->training_pairs=p->first_return_sequence=0;p->have_frozen=false;
 p->cycle=p->cycles=p->missed_bursts=p->burst_pairs=p->cycle_generation=0;
 p->reset_pending=mode==PC_COLD_SHADOW||mode==PC_COLD_RADIO;
 p->reset_from=p->reset_pending?tick:0;
 if(p->reset_pending)p->have_map=false;
 return 0;
}
int pc_begin_cadence(struct pc_experiment *p,uint32_t cycles,uint32_t listen_ms,uint32_t hold_ms,uint64_t tick,uint64_t epoch){
 if(!cycles||cycles>128||!listen_ms||listen_ms>2000||!hold_ms||hold_ms>60000||
    ((uint64_t)listen_ms+hold_ms)*cycles>900000)return -EINVAL;
 if(tick>UINT64_MAX-((uint64_t)listen_ms+hold_ms)*cycles*PC_TICKS_MS)return -EOVERFLOW;
 if(!p->have_map||p->latest.epoch!=epoch||p->latest.active>tick||
    p->latest.map.effective>tick||tick-p->latest.map.effective>=UINT64_C(8000000))return -EAGAIN;
 int rc=pc_begin(p,PC_WARM_SHADOW,listen_ms,hold_ms,tick,epoch);if(rc)return rc;
 p->mode=PC_CADENCE_SHADOW;p->phase=PC_LISTEN;p->cycle=1;p->cycles=cycles;
 p->cycle_generation=p->latest.map.generation;
 p->listen_until=tick+(uint64_t)listen_ms*PC_TICKS_MS;
 return 0;
}
void pc_tick(struct pc_experiment *p,uint64_t tick,uint64_t epoch){
 if(!tick||!epoch)return;
 if(p->phase==PC_ACQUIRE||p->phase==PC_LISTEN||p->phase==PC_HOLD){
  if(epoch!=p->epoch||tick<p->last_tick){fail(p);return;}
  if(p->mode==PC_CADENCE_SHADOW){
   /* Absolute boundaries: absent pairs never extend a burst. A delayed poll
    * skips elapsed windows without inventing training or restarting time. */
   for(unsigned transitions=0;transitions<256;transitions++){
    if(p->phase==PC_LISTEN&&tick>=p->listen_until){
     if(!p->have_map||p->latest.epoch!=epoch){fail(p);break;}
     if(!p->burst_pairs||p->latest.map.generation==p->cycle_generation){if(p->missed_bursts<UINT32_MAX)++p->missed_bursts;}
     p->frozen=p->latest;p->have_frozen=true;p->hold_from=p->listen_until;
     p->hold_until=p->hold_from+(uint64_t)p->hold_ms*PC_TICKS_MS;p->phase=PC_HOLD;
    }else if(p->phase==PC_HOLD&&tick>=p->hold_until){
     if(p->cycle==p->cycles){p->phase=PC_DONE;break;}
     ++p->cycle;p->burst_pairs=0;p->cycle_generation=p->latest.map.generation;
     p->listen_until=p->hold_until+(uint64_t)p->listen_ms*PC_TICKS_MS;p->phase=PC_LISTEN;
    }else break;
   }
   p->last_tick=tick;return;
  }
  if(p->phase==PC_ACQUIRE&&tick-p->requested>=UINT64_C(60000)*PC_TICKS_MS){fail(p);return;}
  if(p->phase==PC_LISTEN&&tick>=p->listen_until){
   if(!p->have_map||p->latest.epoch!=epoch||tick<p->latest.active||tick-p->latest.map.effective>=UINT64_C(8000000)){fail(p);return;}
   p->frozen=p->latest;p->have_frozen=true;p->hold_from=tick;
   p->hold_until=tick+(uint64_t)p->hold_ms*PC_TICKS_MS;p->phase=PC_HOLD;
  }else if(p->phase==PC_HOLD&&tick>=p->hold_until)p->phase=PC_DONE;
 }
 p->last_tick=tick;
}
bool pc_quiet(const struct pc_experiment *p){return p->phase==PC_HOLD&&(p->mode==PC_WARM_RADIO||p->mode==PC_COLD_RADIO);}
bool pc_observe(struct pc_experiment *p,const struct rc5_reference *r,uint64_t epoch){
 if(!r||!r->valid||!r->rx||r->completed_at<r->rx)return false;
 if(r->completed_at>=p->last_tick)pc_tick(p,r->completed_at,epoch);
 if(p->reset_from&&r->rx<p->reset_from)return false;
 if(p->phase==PC_ACQUIRE){p->first_pair=r->completed_at;p->listen_until=r->completed_at+(uint64_t)p->listen_ms*PC_TICKS_MS;p->phase=PC_LISTEN;}
 bool train=p->phase!=PC_HOLD&&!(p->mode==PC_CADENCE_SHADOW&&(p->phase==PC_DONE||p->phase==PC_FAILED));
 if(p->mode==PC_CADENCE_SHADOW&&p->phase==PC_LISTEN){
  uint64_t burst_from=p->listen_until-(uint64_t)p->listen_ms*PC_TICKS_MS;
  /* A correction completed inside a burst cannot import an RF capture from
   * before it. Both members of an admitted timing pair fit the RX budget. */
  if(r->rx<burst_from)train=false;
  if(train&&!p->first_pair)p->first_pair=r->completed_at;
 }
 const struct rc_shadow_map *m=p->have_frozen&&(p->phase==PC_HOLD||(p->phase==PC_DONE&&!p->first_return_sequence))?&p->frozen:&p->latest;
 struct pc_record q={.rx=r->rx,.available=r->completed_at,.remote_ns=r->parent_tx_ns,.epoch=epoch,
  .trial=p->trial,.phase=p->phase,.generation=m->map.generation,.cycle=p->cycle,.train=train};
 uint64_t elapsed=0;
 q.valid=(p->have_map||p->have_frozen)&&m->epoch==epoch&&m->root==r->origin.root&&m->root_boot==r->origin.boot&&m->root_epoch==r->origin.epoch&&m->active<r->rx&&rc5_map_project(&m->map,r->rx,&q.predicted_ns,&elapsed)&&q.predicted_ns<=INT64_MAX&&q.remote_ns<=INT64_MAX;
 if(q.valid)q.error_ns=(int64_t)q.remote_ns-(int64_t)q.predicted_ns;
 if(p->produced==UINT32_MAX){fail(p);return false;}
 q.sequence=++p->produced;if(p->produced>PC_RECORDS&&p->overwritten<UINT32_MAX)++p->overwritten;
 p->records[(q.sequence-1)%PC_RECORDS]=q;
 if(p->pairs<UINT32_MAX)++p->pairs;
 if(p->phase==PC_DONE&&p->have_frozen&&!p->first_return_sequence)p->first_return_sequence=q.sequence;
 if(train&&p->training_pairs<UINT32_MAX)++p->training_pairs;
 if(train&&p->mode==PC_CADENCE_SHADOW&&p->phase==PC_LISTEN&&p->burst_pairs<UINT32_MAX)++p->burst_pairs;
 return train;
}
void pc_offer(struct pc_experiment *p,const struct rc_shadow_map *m){
 if(!m||!m->epoch||m->normal||m->active<m->lo||m->map.effective>m->active)return;
 p->latest=*m;p->have_map=true;
}
bool pc_witness_map(const struct pc_experiment *p,struct r2_capture e,struct rc_shadow_map *out){
 if(!p->have_frozen||e.kind!=R2_CAPTURE_PPS||e.epoch!=p->frozen.epoch||e.tick<p->hold_from||e.tick>=p->hold_until||e.tick<=p->frozen.active)return false;
 *out=p->frozen;
#ifdef R2_PC_PROFILE
 out->experimental=true;
#endif
 return true;
}
bool pc_read(const struct pc_experiment *p,uint32_t after,struct pc_record *out){
 uint32_t first=p->produced>PC_RECORDS?p->produced-PC_RECORDS+1:1;
 if(after>=p->produced)return false;
 uint32_t n=after<first?first:after+1;*out=p->records[(n-1)%PC_RECORDS];return true;
}

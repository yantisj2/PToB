/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_passive_core.h"
#include "holdover.h"
#include <limits.h>
#include <string.h>

uint32_t lp_duration(unsigned k){
 switch(k){case LP_DISC:return 454416;case LP_META:return 1142544;
 case LP_SYNC:return 231951;case LP_FOLLOW:return 268814;default:return 0;}
}
static bool add(uint64_t a,uint64_t b,uint64_t *out){if(a>UINT64_MAX-b)return false;*out=a+b;return true;}
static bool commit(struct lp_tx *t,const struct lp_checkpoint *s){
 if(t->save&&!t->save(t->storage,s)){t->ready=false;t->durable.fh_disabled=1;return false;}
 t->durable=*s;return true;
}
bool lp_tx_start(struct lp_tx *t,uint32_t seed,uint64_t now,uint64_t first,uint32_t boot_ms){
 if(!t||!seed||first<now||now/1000>UINT32_MAX||boot_ms>now/1000+1)return false;
 memset(t,0,sizeof *t);t->durable.seed=seed;t->durable.schedule=LP_SCHEDULE;
 t->durable.minute=now/LP_MINUTE_US;t->durable.next_slot_us=first;
 if(lp_hop_order(t->order,seed))return false;
 t->ready=true;return true;
}
bool lp_replay_admit(const struct lp_replay *r,const struct lp_packet *p){
 if(!r||!p||p->kind!=LP_META||!p->scope.boot||!p->serial)return false;
 if(!r->boot)return true;
 if(p->utc_ns<=r->meta_ns)return false;
 if(p->scope.boot==r->boot)return p->scope.seed==r->seed&&p->serial>r->serial&&p->cursor>=r->cursor;
 for(unsigned i=0;i<LP_RETIRED_BOOTS;i++)if(p->scope.boot==r->retired[i])return false;
 return true;
}
void lp_replay_commit(struct lp_replay *r,const struct lp_packet *p){
 if(r->boot&&r->boot!=p->scope.boot){r->retired[r->next]=r->boot;r->next=(r->next+1)%LP_RETIRED_BOOTS;}
 r->boot=p->scope.boot;r->meta_ns=p->utc_ns;r->seed=p->scope.seed;r->cursor=p->cursor;r->serial=p->serial;
}
static void advance(struct lp_checkpoint *s,uint64_t minute){
 if(minute<=s->minute)return;
 if(minute-s->minute>=61)memset(s->buckets,0,sizeof s->buckets);
 else for(uint64_t m=s->minute+1;m<=minute;m++)s->buckets[m%61]=0;
 s->minute=minute;
}
uint64_t lp_charged(const struct lp_checkpoint *s,uint64_t now){
 uint64_t minute=now/LP_MINUTE_US,total=0;
 for(unsigned i=0;i<61;i++){
  uint64_t age=(s->minute%61+61-i)%61;
  if(age>s->minute)continue;
  uint64_t at=s->minute-age;
  if(minute<=at||minute-at<=60)total+=s->buckets[i];
 }
 return total;
}
static bool checkpoint_valid(const struct lp_checkpoint *s){
 return s&&s->minute<=UINT64_MAX/LP_MINUTE_US&&s->seed&&s->cursor<=UINT32_MAX-2&&s->slot!=UINT32_MAX&&
 s->serial!=UINT32_MAX&&s->pending<=2&&s->fh_disabled<=1&&
 (s->schedule==0||s->schedule==LP_FAST||s->schedule==LP_SLOW||s->schedule==LP_SCHEDULE)&&!(s->owed&~(LP_OWED_PAIR|LP_OWED_META))&&
 (!((s->cursor&1)&&!s->pending&&!s->fh_disabled))&&lp_charged(s,s->minute*LP_MINUTE_US)<=LP_HISTORY_CAP_US;
}
bool lp_checkpoint_carry(struct lp_checkpoint *s,uint64_t now,uint64_t first,uint32_t boot_ms){
 if(!checkpoint_valid(s)||first<now||now/1000>UINT32_MAX||!boot_ms||boot_ms>now/1000+1)return false;
 uint64_t debt=0;for(unsigned i=0;i<61;i++)debt+=s->buckets[i];
 if(debt>LP_HISTORY_CAP_US)return false;
 if(s->schedule!=LP_SCHEDULE){s->schedule=LP_SCHEDULE;s->owed=0;}
 if(s->pending){s->fh_disabled=1;s->pending=0;}
 memset(s->buckets,0,sizeof s->buckets);s->minute=now/LP_MINUTE_US;s->buckets[s->minute%61]=(uint32_t)debt;
 for(unsigned i=0;i<64;i++)s->channel_end_ms[i]=boot_ms;
 s->next_slot_us=first;return true;
}
bool lp_tx_restore(struct lp_tx *t,const struct lp_checkpoint *s,lp_save_fn save,void *storage,bool trusted){
 if(!t)return false;
 /* May restore from t->durable itself; never erase that input before reading. */
 t->ready=t->pair=t->manual=false;t->slot_us=t->sync_end_us=0;t->save=NULL;t->storage=NULL;
 if(!save||!trusted||!checkpoint_valid(s))return false;
 t->durable=*s;t->save=save;t->storage=storage;
 if(t->durable.schedule!=LP_SCHEDULE){t->durable.schedule=LP_SCHEDULE;t->durable.owed=0;}
 if(lp_hop_order(t->order,s->seed))return false;
 t->ready=true;
 if(s->pending){struct lp_checkpoint fault=*s;fault.fh_disabled=1;fault.pending=0;return commit(t,&fault);}
 return true;
}
static bool reserve_control(struct lp_tx *t,uint64_t now,struct lp_reservation *out,bool force_meta,bool manual,bool override){
 if(!t||!out||!t->ready||t->durable.pending||now/LP_MINUTE_US<t->durable.minute||(!manual&&now<t->durable.next_slot_us&&!lp_tx_has_deferred(t))||
 now/1000>UINT32_MAX-4000||t->durable.slot==UINT32_MAX||t->durable.serial==UINT32_MAX)return false;
 if(manual&&(t->save||t->durable.owed||now>UINT64_MAX-LP_FRAME_END_US-1000000||
    now+LP_FRAME_END_US+1000000>t->durable.next_slot_us))return false;
 /* No catch-up: skip missed slots on the original phase and META distribution. */
 uint64_t slot=manual?now:t->durable.next_slot_us;
#ifdef R2_LORA_PASSIVE_SLOW
 if(!manual&&t->durable.owed)slot=now;
#endif
 uint64_t skipped=(now-slot)/LP_BEAT_US;
#ifdef R2_LORA_PASSIVE_SLOW
 skipped=0; /* A logical due precision/META event is not discarded by load. */
#endif
 if(skipped>UINT32_MAX-t->durable.slot)return false;
 uint32_t index=t->durable.slot+(uint32_t)skipped;
 slot+=skipped*LP_BEAT_US;
 if(now-slot>50000){
#ifdef R2_LORA_PASSIVE_SLOW
  slot=now;force_meta=true; /* Fresh rendezvous for an overdue event. */
#else
  return false;
#endif
 }
 unsigned control=index%LP_META_BEATS==0?LP_META:
  index%LP_DISC_BEATS==LP_DISC_PHASE?LP_DISC:0;
 struct lp_checkpoint s=t->durable;
#ifdef R2_LORA_PASSIVE_SLOW
 bool retry=s.owed!=0;
 if(!retry){s.slot=index+1;s.owed=LP_OWED_PAIR|(control==LP_META?LP_OWED_META:0);}
 else control=LP_META;
 /* One outstanding periodic event. Resume its recurring timer from the
  * actual attempt, never drain a stale wall-clock backlog as a TX burst. */
 s.next_slot_us=slot+LP_BEAT_US;
 if(force_meta||manual)control=LP_META;
 if(manual){s.slot=t->durable.slot;s.next_slot_us=t->durable.next_slot_us;s.owed=t->durable.owed;}
 s.schedule=LP_SCHEDULE;
#else
 (void)force_meta;
 s.slot=index+1;s.next_slot_us=slot+LP_BEAT_US;
#endif
 if(control==LP_META)++s.serial;
 /* Check old debt at the EARLIEST possible new RF, before aging any bucket
  * at the proposed completion. Otherwise a minute boundary inside the slot
  * could mint credit before the older RF has actually aged out. */
 uint64_t old=lp_charged(&s,slot>=50000?slot-50000:0);
 uint32_t cost=lp_duration(control);
 if(!override&&(old>LP_CAP_US||cost>LP_CAP_US-old))return false;
 unsigned a=lp_channel(t->order,s.cursor),b=lp_channel(t->order,s.cursor+1);
 uint64_t sync_at=slot+LP_SYNC_OFFSET_US-50000;
#ifdef R2_LORA_PASSIVE_SLOW
 uint64_t sync_rf=lp_duration(LP_SYNC)-10000;
#ifdef R2_LORA_BW_EXPERIMENT
 sync_rf=lp_bw_packet_budget_us(s.cursor,24)-10000;
#endif
 uint64_t follow_at=sync_at+sync_rf+LP_FOLLOW_OFFSET_US;
#elif defined(R2_LORA_BW_EXPERIMENT)
 /* RX FOLLOW cannot precede250ms after the actual SYNC completion. Use the
  * earliest per-arm RF duration for cooldown; BW500 is shorter thanA. */
 uint64_t sync_rf=lp_bw_packet_budget_us(s.cursor,24)-10000;
 uint64_t follow_at=sync_at+sync_rf+250000;
#else
 uint64_t follow_at=slot+LP_SYNC_OFFSET_US+450000;
#endif
 bool pair=!s.fh_disabled&&s.cursor<=UINT32_MAX-3&&a!=b&&
 (!s.channel_end_ms[a]||sync_at>=(uint64_t)s.channel_end_ms[a]*1000+20000000)&&
 (!s.channel_end_ms[b]||follow_at>=(uint64_t)s.channel_end_ms[b]*1000+20000000);
 uint32_t pair_cost=lp_duration(LP_SYNC)+lp_duration(LP_FOLLOW);
 if(pair&&!override)pair=pair_cost<=LP_CAP_US-old-cost;
#ifdef R2_LORA_PASSIVE_SLOW
 /* Do not burn META-only retries while the owed precision cannot fit.
  * A genuine latched FH fault still permits separately due META only. */
 if(!pair&&(!s.fh_disabled||control!=LP_META))return false;
 if(s.fh_disabled&&!(s.owed&LP_OWED_META))return false;
#endif
 if(pair){cost+=pair_cost;s.pending=1;}
 advance(&s,(slot+LP_FRAME_END_US)/LP_MINUTE_US);
 if(s.buckets[s.minute%61]>UINT32_MAX-cost)return false;
 s.buckets[s.minute%61]+=cost;
 if(!commit(t,&s))return false;
 t->manual=manual;t->slot_us=slot;t->pair=pair;t->sync_end_us=0;t->unlaunched=true;t->reserved_control=(uint8_t)control;
 *out=(struct lp_reservation){.cursor=s.cursor,.serial=s.serial,.control=(uint8_t)control,.has_pair=pair,.slot_us=slot};
 return true;
}
bool lp_tx_reserve_control(struct lp_tx *t,uint64_t now,struct lp_reservation *out,bool force_meta){
 return reserve_control(t,now,out,force_meta,false,false);
}
bool lp_tx_reserve_utc(struct lp_tx *t,uint64_t now,struct lp_reservation *out,bool override){
#ifdef R2_LORA_PASSIVE_SLOW
 if(!t||t->save)return false;
 /* Extra work keeps the periodic grid. A genuinely due/owed event can be
  * coalesced into this one requested META+pair, including its scoped cap
  * override, using the ordinary debt/timer semantics. Never start in the
  * narrow unsafe gap immediately before a future periodic event. */
 bool extra=t&&!t->durable.owed&&now<=UINT64_MAX-LP_FRAME_END_US-1000000&&
  now+LP_FRAME_END_US+1000000<=t->durable.next_slot_us;
 if(!extra&&(!t||(!t->durable.owed&&now<t->durable.next_slot_us)))return false;
 return reserve_control(t,now,out,true,extra,override);
#else
 (void)t;(void)now;(void)out;(void)override;return false;
#endif
}
bool lp_tx_reserve(struct lp_tx *t,uint64_t now,struct lp_reservation *out){return lp_tx_reserve_control(t,now,out,false);}
bool lp_tx_has_deferred(const struct lp_tx *t){return t&&t->durable.owed;}
void lp_tx_control_complete(struct lp_tx *t){
 if(t)t->unlaunched=false;
#ifdef R2_LORA_PASSIVE_SLOW
 if(t&&!t->manual)t->durable.owed&=(uint8_t)~LP_OWED_META;
#else
 (void)t;
#endif
}
static bool confirm(struct lp_tx *t,unsigned kind,uint64_t end,bool defer){
 if(!t||!t->ready||!t->pair||end<t->slot_us||end>=t->slot_us+LP_FRAME_END_US)return false;
 struct lp_checkpoint s=t->durable;
 uint32_t duration=lp_duration(kind);
#ifdef R2_LORA_BW_EXPERIMENT
 /* Reservations retain narrow charge; completion admits only this packet's
  * actual on-air duration and the original launch/finish window. */
 if(kind==LP_SYNC||kind==LP_FOLLOW)duration=lp_bw_packet_budget_us(s.cursor,(unsigned)lp_size(kind));
#endif
 if(kind==LP_SYNC&&s.pending==1){
  if(end<t->slot_us+LP_SYNC_OFFSET_US-50000+duration-10000||end>t->slot_us+LP_SYNC_OFFSET_US+LP_LAUNCH_PAD_US+duration)return false;
  s.channel_end_ms[lp_channel(t->order,s.cursor)]=(uint32_t)((end+999)/1000);
  ++s.cursor;s.pending=2;t->sync_end_us=end;
 }else if(kind==LP_FOLLOW&&s.pending==2){
  if(end<t->sync_end_us+250000+duration-10000||
     end>t->sync_end_us+LP_FOLLOW_LATEST_US+duration)return false;
  s.channel_end_ms[lp_channel(t->order,s.cursor)]=(uint32_t)((end+999)/1000);
  ++s.cursor;s.pending=0;
#ifdef R2_LORA_PASSIVE_SLOW
  if(!t->manual)s.owed&=(uint8_t)~LP_OWED_PAIR;
#endif
 }else return false;
 t->unlaunched=false;
 if(defer){t->durable=s;return true;}
 return commit(t,&s);
}
bool lp_tx_confirm(struct lp_tx *t,unsigned kind,uint64_t end){return confirm(t,kind,end,false);}
bool lp_tx_capture_sync(struct lp_tx *t,uint64_t end){return confirm(t,LP_SYNC,end,true);}
bool lp_tx_capture_follow(struct lp_tx *t,uint64_t end){return confirm(t,LP_FOLLOW,end,true);}
bool lp_tx_flush(struct lp_tx *t){return t&&t->ready&&!t->durable.pending&&commit(t,&t->durable);}
void lp_tx_fault(struct lp_tx *t){
 if(!t||!t->ready)return;
 t->unlaunched=false;
 struct lp_checkpoint s=t->durable;s.fh_disabled=1;s.pending=0;t->pair=false;
 (void)commit(t,&s);
}
bool lp_tx_retire_quiescent(struct lp_tx *t,uint64_t now){
 if(!t||!t->ready||t->durable.fh_disabled||!t->durable.pending||now/1000>UINT32_MAX-1)return false;
 struct lp_checkpoint s=t->durable;uint32_t cursor=s.cursor&~UINT32_C(1);
 if(cursor>UINT32_MAX-2)return false;
 uint32_t at=(uint32_t)((now+999)/1000);
 for(unsigned i=0;i<2;i++){unsigned channel=lp_channel(t->order,cursor+i);
  if(s.channel_end_ms[channel]<at)s.channel_end_ms[channel]=at;}
 s.cursor=cursor+2;s.pending=0;
#ifdef R2_LORA_PASSIVE_SLOW
 if(!t->manual)s.owed|=LP_OWED_PAIR;
#endif
 t->pair=false;t->sync_end_us=0;t->unlaunched=false;return commit(t,&s);
}
void lp_tx_cancel(struct lp_tx *t,bool no_fh_rf){
 if(!t||!t->ready)return;
 t->unlaunched=false;
 if(!no_fh_rf||t->durable.pending==2){lp_tx_fault(t);return;}
 if(!t->durable.pending){t->pair=false;return;}
 struct lp_checkpoint s=t->durable;s.pending=0;t->pair=false;(void)commit(t,&s);
}

bool lp_tx_refund_no_rf(struct lp_tx *t,const struct lp_reservation *r){
 if(!t||!r||!t->ready||t->save||!t->unlaunched||t->durable.fh_disabled||
    t->slot_us!=r->slot_us||t->durable.serial!=r->serial||t->durable.cursor!=r->cursor||
    t->pair!=r->has_pair||t->reserved_control!=r->control||t->durable.pending!=(r->has_pair?1:0)||t->sync_end_us||
    (r->control&&r->control!=LP_META&&r->control!=LP_DISC)||(r->control==LP_META&&!r->serial))return false;
 uint64_t minute=(r->slot_us+LP_FRAME_END_US)/LP_MINUTE_US;
 uint32_t cost=lp_duration(r->control)+(r->has_pair?lp_duration(LP_SYNC)+lp_duration(LP_FOLLOW):0);
 if(t->durable.minute!=minute||t->durable.buckets[minute%61]<cost)return false;
 t->durable.buckets[minute%61]-=cost;t->durable.pending=0;
 /* An identity that provably never reached RF is reusable. Keep receiver
  * serial continuity; ordinary cancel/possible RF never takes this path. */
 if(r->control==LP_META)--t->durable.serial;
 t->pair=t->unlaunched=false;return true;
}

void lp_rx_clear(struct lp_rx *r){if(r)memset(r,0,sizeof *r);}
bool lp_age_admit(const struct lp_age *a,uint64_t event,uint64_t now,uint64_t *age){
 if(!a||!age||!a->source_bound_known||now>=a->valid_until_us||a->lower_ns>a->upper_ns||
 event<a->source_error_ns||event>UINT64_MAX-a->source_error_ns||
 event+a->source_error_ns>a->lower_ns||a->upper_ns<event-a->source_error_ns)return false;
 uint64_t ns=a->upper_ns-(event-a->source_error_ns);if(ns>UINT64_C(10000000000))return false;
 *age=(ns+999)/1000;return true;
}
static void pending_clear(struct lp_rx *r){r->pending=r->hint=r->prediction=false;}
static void withdraw(struct lp_rx *r,bool keep_display){
 if(!keep_display)r->display_valid=r->display_acquiring=false;
 pending_clear(r);r->have_pair=r->trained=r->bounded=r->hold_selected=false;r->physical_deadline_us=0;r->model_accepted=0;
 r->fit_count=0;r->meta_deadline_us=r->tz_deadline_us=0;
}
bool lp_rx_meta(struct lp_rx *r,const struct lp_packet *p,const struct r2_group_config *g,const struct lp_age *a,uint64_t event,uint64_t now){
 if(!r||!p||p->kind!=LP_META||!p->serial||event>UINT64_MAX-LP_LEASE_US||
 now<event||now-event>LP_CONTROL_PROCESS_US||p->schedule!=LP_SCHEDULE)return false;
 uint8_t context[16];if(lp_context(context,&p->scope,p->schedule,g))return false;
 bool same=r->cached&&memcmp(context,r->context,16)==0;
 /* Within one physical boot the public seed/cursor cannot roll back.
  * Unknown-age new contexts remain observational and cannot replace a live grant. */
 uint64_t age=0;bool bounded=lp_age_admit(a,p->utc_ns,now,&age);
 if(r->cached&&(!same&&r->bounded&&!bounded))return false;
 if(same&&p->serial<=r->serial)return false;
 if(r->cached&&(p->scope.emitter!=r->scope.emitter||p->scope.seed!=r->scope.seed||p->cursor<r->highest||p->utc_ns<=r->last_meta_ns)){
#ifdef R2_LORA_PASSIVE_SLOW
  if(p->scope.emitter==r->scope.emitter&&p->scope.boot==r->scope.boot)return false;
#else
  if(p->scope.emitter!=r->scope.emitter||p->scope.boot==r->scope.boot)return false;
#endif
 }
 if(r->cached&&!same&&p->scope.emitter==r->scope.emitter&&p->scope.boot==r->scope.boot&&
    (p->scope.stream<=r->scope.stream||(p->scope.root_boot==r->scope.root_boot&&p->scope.epoch<r->scope.epoch)))return false;
 if(same&&p->cursor<r->highest)return false;
 if(!same){withdraw(r,
#ifdef R2_LORA_PASSIVE_SLOW
  r->cached&&p->time_valid&&p->scope.emitter==r->scope.emitter&&p->scope.boot==r->scope.boot
#else
  false
#endif
 );r->last_tick=r->last_tx_ns=r->last_epoch=0;r->highest=p->cursor;r->scope=p->scope;memcpy(r->context,context,16);
  if(lp_hop_order(r->order,p->scope.seed))return false;}
 r->cached=true;r->serial=p->serial;r->bounded=bounded;r->last_meta_ns=p->utc_ns;
 if(!add(event,LP_LEASE_US,&r->observed_deadline_us))return false;
 r->meta_deadline_us=r->tz_deadline_us=0;
 if(bounded){
  if(age>=LP_LEASE_US||!add(now,LP_LEASE_US-age,&r->meta_deadline_us))return false;
  if(r->meta_deadline_us>a->valid_until_us)r->meta_deadline_us=a->valid_until_us;
  r->tz_deadline_us=r->meta_deadline_us;
 }
 r->tz=p->tz;
 if(!p->time_valid)withdraw(r,false);
 return true;
}
bool lp_rx_hint(struct lp_rx *r,const struct lp_packet *p,uint64_t end,uint64_t now){
 if(!r||!p||!r->cached||(p->kind!=LP_META&&p->kind!=LP_DISC)||p->schedule!=LP_SCHEDULE||
 now<end||now-end>LP_CONTROL_PROCESS_US||now>=r->observed_deadline_us||p->cursor<r->highest)return false;
 pending_clear(r);
 if(!p->time_valid){withdraw(r,false);return true;}
 if(!p->has_pair)return true;
 uint64_t at;if(!add(end,(uint64_t)p->delay_10ms*10000,&at)||at<250000||now>UINT64_MAX-25000||now+25000>at-250000)return false;
 r->hint=true;r->hint_cursor=p->cursor;r->hint_us=at;return true;
}
bool lp_rx_sync(struct lp_rx *r,uint32_t cursor,uint64_t tick,uint64_t epoch,uint64_t event,uint64_t now){
 uint64_t window=r&&r->prediction?LP_PREDICT_HEADER_WINDOW_US:LP_HEADER_WINDOW_US;
 if(!r||!r->cached||!r->hint||!epoch||event>UINT64_MAX-LP_HOLD_US||cursor!=r->hint_cursor||cursor<r->highest||
 event+window<r->hint_us||event>r->hint_us+window||now<event||now-event>LP_SYNC_PROCESS_US||now>=r->observed_deadline_us){
  if(r)pending_clear(r);
  return false;
 }
 r->pending=true;r->hint=false;
#ifdef R2_LORA_PASSIVE_SLOW
 r->prediction=false;
#endif
 r->pending_cursor=cursor;r->pending_tick=tick;
 r->pending_epoch=epoch;r->pending_us=event;r->pending_deadline_us=event+LP_PENDING_US;
 return true;
}
#ifdef R2_LORA_PASSIVE_SLOW
/* Sparse LoRa adaptation of the shared HOLD selector. Pair slopes use real
 * physical observations and at least60s separation. Select medians by bounded
 * repeated scans rather than allocating a120-slope array on the2KiB worker.
 * Frequency is local-minus-remote ppb, opposite the LoRa correction multiplier.
 * These internal agreement guards are not independently measured uncertainty. */
static bool hold_slope(const struct lp_rx *r,unsigned i,unsigned j,int32_t *rate){
 uint64_t dt=r->fit[j].tick-r->fit[i].tick;
 if(dt<UINT64_C(60000000)*16)return false;
 uint64_t nominal=dt*125/2,remote=r->fit[j].tx_ns-r->fit[i].tx_ns;
 if(!remote||remote>INT64_MAX)return false;
 int64_t error=(int64_t)nominal-(int64_t)remote;
 if(r2_hold_abs(error)>nominal/10000)return false;
 int64_t ppb=error*INT64_C(1000000000)/(int64_t)remote;
 if(ppb< -100000||ppb>100000)return false;
 *rate=(int32_t)ppb;return true;
}
static int32_t hold_order(const struct lp_rx *r,unsigned first,unsigned rank,int32_t center,bool deviation){
 int32_t lo=deviation?0:-100000,hi=deviation?200000:100000;
 while(lo<hi){
  int32_t middle=lo+(hi-lo)/2;unsigned count=0;
  for(unsigned i=first;i<r->fit_count;i++)for(unsigned j=i+1;j<r->fit_count;j++){
   int32_t rate;if(!hold_slope(r,i,j,&rate))continue;
   int32_t value=deviation?(int32_t)r2_hold_abs((int64_t)rate-center):rate;
   if(value<=middle)++count;
  }
  if(count>rank)hi=middle;else lo=middle+1;
 }
 return lo;
}
static struct r2_hold_fit hold_sparse_fit(const struct lp_rx *r,unsigned seconds){
 struct r2_hold_fit f={.guard=UINT32_MAX};unsigned first=0,n=r->fit_count;
 while(first<n&&r->fit[n-1].tick-r->fit[first].tick>(uint64_t)seconds*R2_CAPTURE_HZ)++first;
 if(first==n)return f;
 f.points=(uint16_t)(n-first);f.span_ms=(uint32_t)((r->fit[n-1].tick-r->fit[first].tick)/16000);
 if(f.points<4||f.span_ms<seconds*850)return f;
 for(unsigned i=first;i<n;i++)for(unsigned j=i+1;j<n;j++){
  int32_t rate;if(hold_slope(r,i,j,&rate))++f.pairs;
 }
 if(f.pairs<8)return f;
 f.rate=hold_order(r,first,(f.pairs-1)/2,0,false);
 uint32_t spread=(uint32_t)hold_order(r,first,(f.pairs-1)/2,f.rate,true);
 f.guard=3*spread+1;return f;
}
static void hold_sparse_select(struct lp_rx *r){
 struct r2_hold_fit short_fit=hold_sparse_fit(r,180),long_fit=hold_sparse_fit(r,300);
 r->hold_short_ppb=short_fit.rate;r->hold_long_ppb=long_fit.rate;
 r->hold_short_guard=short_fit.guard;r->hold_long_guard=long_fit.guard;
 struct rc5_map baseline;
 if(!r2_hold_from_lora(r->last_tick,r->last_tx_ns,r->baseline_rate_ppb,&baseline))return;
 uint64_t span=(r->fit[r->fit_count-1].tick-r->fit[0].tick)/16;
 uint64_t guard=((uint64_t)r->rms_ns*3000000+span-1)/span;
 int32_t selected=r2_hold_consensus(baseline.ppb,guard<UINT32_MAX?(uint32_t)guard:UINT32_MAX,
  &short_fit,&long_fit,&r->hold_selected);
 if(r->hold_selected){
  int64_t correction=-(int64_t)selected*INT64_C(1000000000)/(INT64_C(1000000000)+selected);
  if(correction< -100000||correction>100000)r->hold_selected=false;
  else r->rate_ppb=(int32_t)correction;
 }
}
static int64_t hold_phase_residual(const struct lp_rx *r,unsigned i){
 uint64_t predicted;
 if(!r2_hold_lora_project(r->last_tick,r->fit[i].tick,r->fit[i].tx_ns,r->rate_ppb,0,&predicted))return INT64_MAX;
 return predicted>=r->last_tx_ns?(int64_t)(predicted-r->last_tx_ns):-(int64_t)(r->last_tx_ns-predicted);
}
static void hold_phase_fit(struct lp_rx *r){
 int32_t lo=INT32_MIN,hi=INT32_MAX;
 while(lo<hi){
  int32_t middle=(int32_t)((int64_t)lo+((int64_t)hi-lo)/2);unsigned count=0;
  for(unsigned i=0;i<r->fit_count;i++)if(hold_phase_residual(r,i)<=middle)++count;
  if(count>(r->fit_count-1u)/2)hi=middle;else lo=middle+1;
 }
 r->phase_offset_ns=lo;
}
#endif
static void fit(struct lp_rx *r,uint64_t tick,uint64_t ns){
 /* Uniform >=20s sample spacing bounds storage at16 points for300s. Every
  * physical pair is still counted; discarding a fit candidate never renews it. */
 unsigned n=r->fit_count;
 if(n&&(tick<=r->fit[n-1].tick||ns<=r->fit[n-1].tx_ns)){r->fit_count=0;n=0;}
 while(n&&tick-r->fit[0].tick>LP_WINDOW_US*16){memmove(r->fit,r->fit+1,(n-1)*sizeof r->fit[0]);--n;}
 if(n&&tick-r->fit[n-1].tick<UINT64_C(20000000)*16){
  r->fit_count=(uint8_t)n;
#ifdef R2_LORA_PASSIVE_SLOW
  /* FOLLOW has advanced the raw anchor even when this physical pair is too
   * close to enter history. Express the retained fit at that new anchor. */
  if(r->trained)hold_phase_fit(r);
#endif
  return;
 }
 if(n==LP_FIT_CAPACITY){memmove(r->fit,r->fit+1,(--n)*sizeof r->fit[0]);}
 r->fit[n++]=(struct lp_pair){tick,ns};r->fit_count=(uint8_t)n;r->trained=r->hold_selected=false;
 if(n<4||tick-r->fit[0].tick<LP_TRAIN_US*16)return;
 double sx=0,sy=0,sxx=0,sxy=0;
 for(unsigned i=0;i<n;i++){
  double x=(double)(r->fit[i].tick-r->fit[0].tick)/16.0;
  double y=(double)(r->fit[i].tx_ns-r->fit[0].tx_ns)-x*1000.0;
  sx+=x;sy+=y;sxx+=x*x;sxy+=x*y;
 }
 double d=n*sxx-sx*sx;if(d<=0)return;
 double slope=(n*sxy-sx*sy)/d,rate=slope*1000000.0;
 if(rate < -100000.0||rate>100000.0)return;
 double intercept=(sy-slope*sx)/n,variance=0;
 for(unsigned i=0;i<n;i++){
  double x=(double)(r->fit[i].tick-r->fit[0].tick)/16.0;
  double error=(double)(r->fit[i].tx_ns-r->fit[0].tx_ns)-x*1000.0-intercept-slope*x;
  variance+=error*error;
 }
 variance/=n;
 /* Integer ceiling square root: consistency diagnostic only. */
 if(variance>UINT32_MAX*(double)UINT32_MAX)return;
 uint32_t lo=0,hi=UINT32_MAX;
 while(lo<hi){uint32_t mid=lo+(uint32_t)(((uint64_t)hi-lo+1)/2);
  if((double)mid*mid<=variance)lo=mid;else hi=mid-1;}
 r->rms_ns=lo==UINT32_MAX?lo:lo+1;r->rate_ppb=r->baseline_rate_ppb=(int32_t)rate;r->trained=true;
#ifdef R2_LORA_PASSIVE_SLOW
 hold_sparse_select(r);
 hold_phase_fit(r);
#endif
}
bool lp_rx_follow(struct lp_rx *r,const struct lp_packet *p,const struct lp_age *a,uint64_t epoch,uint64_t now){
 if(!r||!p||p->kind!=LP_FOLLOW||p->schedule!=LP_SCHEDULE||!r->pending||p->cursor!=r->pending_cursor||epoch!=r->pending_epoch||
 now<r->pending_us||now>=r->pending_deadline_us||now>=r->observed_deadline_us){if(r){++r->rejected;pending_clear(r);}return false;}
 uint64_t age;bool bounded=lp_age_admit(a,p->utc_ns,now,&age);
 if(r->last_tick&&(p->cursor<=r->highest||p->utc_ns<=r->last_tx_ns||
    (epoch==r->last_epoch&&r->pending_tick<=r->last_tick))){++r->rejected;pending_clear(r);return false;}
 r->physical_deadline_us=r->pending_us+LP_HOLD_US;
 if(now>=r->physical_deadline_us){pending_clear(r);return false;}
 if(epoch!=r->last_epoch){r->fit_count=0;if(epoch!=r->display_epoch)r->display_valid=r->display_acquiring=false;}
 r->last_tick=r->pending_tick;r->last_tx_ns=p->utc_ns;r->last_us=r->pending_us;
 r->highest=p->cursor;r->have_pair=true;r->bounded=r->bounded&&bounded;r->last_epoch=epoch;
 fit(r,r->last_tick,r->last_tx_ns);
 if(r->trained){
#ifndef R2_LORA_PASSIVE_SLOW
  r->display_tick=r->last_tick;r->display_ns=r->last_tx_ns;
  r->display_us=r->last_us;r->display_epoch=r->last_epoch;r->display_rate_ppb=r->rate_ppb;r->display_valid=true;r->display_acquiring=false;}
#else
  /* The native owner commits this causal fit at an actual current capture.
   * Do not step the delivered phase to the newest noisy raw observation. */
 }
#endif
#ifdef R2_LORA_PASSIVE_SLOW
 else if(!r->display_valid){
  r->display_tick=r->last_tick;r->display_ns=r->last_tx_ns;r->display_us=r->last_us;
  r->display_epoch=r->last_epoch;r->display_rate_ppb=0;r->display_valid=r->display_acquiring=true;
 }
#endif
 ++r->accepted;
 r->pending=false;r->prediction=true;r->hint=true;r->hint_cursor=p->cursor+2;r->hint_us=r->pending_us+LP_BEAT_US;
 return true;
}
bool lp_rx_discipline(struct lp_rx *r,uint64_t tick,uint64_t epoch,uint64_t now){
 if(!r||!r->trained||!r->have_pair||epoch!=r->last_epoch||tick<r->last_tick||
    now<r->last_us||now>=r->physical_deadline_us||r->model_accepted==r->accepted)return false;
 uint64_t phase,target,previous;
 if(r->phase_offset_ns<0){uint64_t offset=(uint64_t)(-(int64_t)r->phase_offset_ns);if(r->last_tx_ns<offset)return false;phase=r->last_tx_ns-offset;}
 else{if(r->last_tx_ns>UINT64_MAX-(uint32_t)r->phase_offset_ns)return false;phase=r->last_tx_ns+(uint32_t)r->phase_offset_ns;}
 if(!r2_hold_lora_project(tick,r->last_tick,phase,r->rate_ppb,0,&target))return false;
 int32_t slew=0;
 if(r->display_valid&&!r->display_acquiring&&r->display_epoch==epoch&&
    r2_hold_lora_project(tick,r->display_tick,r->display_ns,r->display_rate_ppb,r->display_phase_ns,&previous)){
  int64_t difference=target>=previous?(int64_t)(target-previous):-(int64_t)(previous-target);
  /* Larger discontinuities reacquire rather than hiding a clock error in an
   * unbounded slew. This threshold does not filter measurements or tails. */
  if(r2_hold_abs(difference)<=1000000){slew=(int32_t)difference;target=previous;}
 }
 r->display_tick=tick;r->display_ns=target;r->display_rate_ppb=r->rate_ppb;r->display_phase_ns=slew;
 r->display_epoch=epoch;r->display_us=r->last_us;r->display_valid=true;r->display_acquiring=false;
 r->model_accepted=r->accepted;return true;
}
void lp_rx_expire(struct lp_rx *r,uint64_t now){
 if(!r)return;
 if(r->pending&&now>=r->pending_deadline_us){++r->missing;pending_clear(r);}
 if(r->hint&&now>r->hint_us+(r->prediction?LP_PREDICT_HINT_RETIRE_US:LP_HINT_RETIRE_US)){++r->missing;pending_clear(r);}
 if(r->have_pair&&now>=r->physical_deadline_us){r->have_pair=r->trained=false;
#ifndef R2_LORA_PASSIVE_SLOW
 r->fit_count=0;
#endif
 }
 if(r->cached&&now>=r->observed_deadline_us)withdraw(r,
#ifdef R2_LORA_PASSIVE_SLOW
 true /* META loss expires authority, not the explicitly stale human watch. */
#else
 false
#endif
 ); /* Keep replay/context floors. */
 if(r->bounded&&now>=r->meta_deadline_us){r->bounded=false;r->meta_deadline_us=r->tz_deadline_us=0;}
}
bool lp_rx_estimate(const struct lp_rx *r,uint64_t tick,uint64_t epoch,uint64_t now,uint64_t *ns){
 if(!r||!ns||!r->have_pair||!r->trained||epoch!=r->last_epoch||now>=r->physical_deadline_us||
 now>=r->observed_deadline_us||tick<r->last_tick||tick-r->last_tick>LP_HOLD_US*16)return false;
#ifdef R2_LORA_PASSIVE_SLOW
 return r->display_valid&&!r->display_acquiring&&r->display_epoch==epoch&&r->model_accepted==r->accepted&&
  r2_hold_lora_project(tick,r->display_tick,r->display_ns,r->display_rate_ppb,r->display_phase_ns,ns);
#else
 uint64_t delta=tick-r->last_tick;
 int64_t nominal=(int64_t)(delta*125/2),correction=(int64_t)delta*r->rate_ppb/16000000;
 if(correction<0&&nominal< -correction)return false;
 return add(r->last_tx_ns,(uint64_t)(nominal+correction),ns);
#endif
}
bool lp_rx_display_estimate(struct lp_rx *r,uint64_t tick,uint64_t epoch,uint64_t now,uint64_t *ns){
 if(!r||!ns||!r->display_valid)return false;
 if(epoch!=r->display_epoch||tick<r->display_tick||now<r->display_us){r->display_valid=r->display_acquiring=false;return false;}
 /* Divide before multiplying: no32-bit uptime wrap or unchecked long-hold
  * tick/rate product. Numeric exhaustion expires presentation only. */
 if(!r2_hold_lora_project(tick,r->display_tick,r->display_ns,r->display_rate_ppb,r->display_phase_ns,ns)){
  r->display_valid=r->display_acquiring=false;return false;}
 return true;
}
bool lp_rx_envelope_current(const struct lp_rx *r,const struct lp_age *a,uint64_t now){
 return r&&a&&r->bounded&&r->trained&&r->have_pair&&now<r->physical_deadline_us&&
 now<r->meta_deadline_us&&now<a->valid_until_us;
}

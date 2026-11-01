/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_passive_core.h"
#include <limits.h>
#include <string.h>

uint32_t lp_duration(unsigned k){
 switch(k){case LP_DISC:return 454416;case LP_META:return 1142544;
 case LP_SYNC:return 231951;case LP_FOLLOW:return 268814;default:return 0;}
}
static bool add(uint64_t a,uint64_t b,uint64_t *out){if(a>UINT64_MAX-b)return false;*out=a+b;return true;}
static bool commit(struct lp_tx *t,const struct lp_checkpoint *s){
 if(!t->save||!t->save(t->storage,s)){t->ready=false;t->durable.fh_disabled=1;return false;}
 t->durable=*s;return true;
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
 (!((s->cursor&1)&&!s->pending&&!s->fh_disabled))&&lp_charged(s,s->minute*LP_MINUTE_US)<=LP_CAP_US;
}
bool lp_checkpoint_carry(struct lp_checkpoint *s,uint64_t now,uint64_t first,uint32_t boot_ms){
 if(!checkpoint_valid(s)||first<now||now/1000>UINT32_MAX||!boot_ms||boot_ms>now/1000+1)return false;
 uint64_t debt=0;for(unsigned i=0;i<61;i++)debt+=s->buckets[i];
 if(debt>LP_CAP_US)return false;
 if(s->pending){s->fh_disabled=1;s->pending=0;}
 memset(s->buckets,0,sizeof s->buckets);s->minute=now/LP_MINUTE_US;s->buckets[s->minute%61]=(uint32_t)debt;
 for(unsigned i=0;i<64;i++)s->channel_end_ms[i]=boot_ms;
 s->next_slot_us=first;return true;
}
bool lp_tx_restore(struct lp_tx *t,const struct lp_checkpoint *s,lp_save_fn save,void *storage,bool trusted){
 if(!t)return false;
 /* May restore from t->durable itself; never erase that input before reading. */
 t->ready=t->pair=false;t->slot_us=t->sync_end_us=0;t->save=NULL;t->storage=NULL;
 if(!save||!trusted||!checkpoint_valid(s))return false;
 t->durable=*s;t->save=save;t->storage=storage;
 if(lp_hop_order(t->order,s->seed))return false;
 t->ready=true;
 if(s->pending){struct lp_checkpoint fault=*s;fault.fh_disabled=1;fault.pending=0;return commit(t,&fault);}
 return true;
}
bool lp_tx_reserve(struct lp_tx *t,uint64_t now,struct lp_reservation *out){
 if(!t||!out||!t->ready||t->durable.pending||now<t->durable.next_slot_us||
 now/1000>UINT32_MAX-4000||t->durable.slot==UINT32_MAX||t->durable.serial==UINT32_MAX)return false;
 /* No catch-up: skip missed slots on the original phase and META distribution. */
 uint64_t slot=t->durable.next_slot_us;
 uint64_t skipped=(now-slot)/LP_BEAT_US;
 if(skipped>UINT32_MAX-t->durable.slot)return false;
 uint32_t index=t->durable.slot+(uint32_t)skipped;
 slot+=skipped*LP_BEAT_US;
 if(now-slot>50000)return false;
 unsigned control=index%3==0?LP_META:LP_DISC;
 struct lp_checkpoint s=t->durable;
 s.slot=index+1;s.next_slot_us=slot+LP_BEAT_US;
 if(control==LP_META)++s.serial;
 /* Check old debt at the EARLIEST possible new RF, before aging any bucket
  * at the proposed completion. Otherwise a minute boundary inside the slot
  * could mint credit before the older RF has actually aged out. */
 uint64_t old=lp_charged(&s,slot>=50000?slot-50000:0);
 uint32_t cost=lp_duration(control);
 if(old>LP_CAP_US||cost>LP_CAP_US-old)return false;
 unsigned a=lp_channel(t->order,s.cursor),b=lp_channel(t->order,s.cursor+1);
 uint64_t sync_at=slot+1950000,follow_at=slot+2450000;
 bool pair=!s.fh_disabled&&s.cursor<=UINT32_MAX-3&&a!=b&&
 (!s.channel_end_ms[a]||sync_at>=(uint64_t)s.channel_end_ms[a]*1000+20000000)&&
 (!s.channel_end_ms[b]||follow_at>=(uint64_t)s.channel_end_ms[b]*1000+20000000);
 uint32_t pair_cost=lp_duration(LP_SYNC)+lp_duration(LP_FOLLOW);
 if(pair)pair=pair_cost<=LP_CAP_US-old-cost;
 if(pair){cost+=pair_cost;s.pending=1;}
 advance(&s,(slot+3000000)/LP_MINUTE_US);
 if(s.buckets[s.minute%61]>UINT32_MAX-cost)return false;
 s.buckets[s.minute%61]+=cost;
 if(!commit(t,&s))return false;
 t->slot_us=slot;t->pair=pair;t->sync_end_us=0;
 *out=(struct lp_reservation){.cursor=s.cursor,.serial=s.serial,.control=(uint8_t)control,.has_pair=pair,.slot_us=slot};
 return true;
}
static bool confirm(struct lp_tx *t,unsigned kind,uint64_t end,bool defer){
 if(!t||!t->ready||!t->pair||end<t->slot_us||end>=t->slot_us+3000000)return false;
 struct lp_checkpoint s=t->durable;
 if(kind==LP_SYNC&&s.pending==1){
  if(end<t->slot_us+1950000+lp_duration(LP_SYNC)-10000||end>t->slot_us+2281951)return false;
  s.channel_end_ms[lp_channel(t->order,s.cursor)]=(uint32_t)((end+999)/1000);
  ++s.cursor;s.pending=2;t->sync_end_us=end;
 }else if(kind==LP_FOLLOW&&s.pending==2){
  if(end<t->sync_end_us+250000+lp_duration(LP_FOLLOW)-10000||
     end>t->sync_end_us+300000+lp_duration(LP_FOLLOW))return false;
  s.channel_end_ms[lp_channel(t->order,s.cursor)]=(uint32_t)((end+999)/1000);
  ++s.cursor;s.pending=0;
 }else return false;
 if(defer){t->durable=s;return true;}
 return commit(t,&s);
}
bool lp_tx_confirm(struct lp_tx *t,unsigned kind,uint64_t end){return confirm(t,kind,end,false);}
bool lp_tx_capture_sync(struct lp_tx *t,uint64_t end){return confirm(t,LP_SYNC,end,true);}
bool lp_tx_capture_follow(struct lp_tx *t,uint64_t end){return confirm(t,LP_FOLLOW,end,true);}
bool lp_tx_flush(struct lp_tx *t){return t&&t->ready&&!t->durable.pending&&commit(t,&t->durable);}
void lp_tx_fault(struct lp_tx *t){
 if(!t||!t->ready)return;
 struct lp_checkpoint s=t->durable;s.fh_disabled=1;s.pending=0;t->pair=false;
 (void)commit(t,&s);
}
void lp_tx_cancel(struct lp_tx *t,bool no_fh_rf){
 if(!t||!t->ready)return;
 if(!no_fh_rf||t->durable.pending==2){lp_tx_fault(t);return;}
 if(!t->durable.pending){t->pair=false;return;}
 struct lp_checkpoint s=t->durable;s.pending=0;t->pair=false;(void)commit(t,&s);
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
static void withdraw(struct lp_rx *r){
 pending_clear(r);r->have_pair=r->trained=r->bounded=false;r->physical_deadline_us=0;
 r->fit_count=0;r->meta_deadline_us=r->tz_deadline_us=0;
}
bool lp_rx_meta(struct lp_rx *r,const struct lp_packet *p,const struct r2_group_config *g,const struct lp_age *a,uint64_t event,uint64_t now){
 if(!r||!p||p->kind!=LP_META||!p->serial||event>UINT64_MAX-LP_LEASE_US||
 now<event||now-event>200000||p->schedule!=LP_FAST)return false;
 uint8_t context[16];if(lp_context(context,&p->scope,p->schedule,g))return false;
 bool same=r->cached&&memcmp(context,r->context,16)==0;
 /* A physical transmitter keeps its public seed/cursor across context changes.
  * Unknown-age new contexts remain observational and cannot replace a live grant. */
 uint64_t age=0;bool bounded=lp_age_admit(a,p->utc_ns,now,&age);
 if(r->cached&&(!same&&r->bounded&&!bounded))return false;
 if(same&&p->serial<=r->serial)return false;
 if(r->cached&&(p->scope.emitter!=r->scope.emitter||p->scope.seed!=r->scope.seed||p->cursor<r->highest||p->utc_ns<=r->last_meta_ns))return false;
 if(r->cached&&!same&&p->scope.boot==r->scope.boot&&
    (p->scope.stream<=r->scope.stream||(p->scope.root_boot==r->scope.root_boot&&p->scope.epoch<r->scope.epoch)))return false;
 if(same&&p->cursor<r->highest)return false;
 if(!same){withdraw(r);r->highest=p->cursor;r->scope=p->scope;memcpy(r->context,context,16);
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
 if(!p->time_valid)withdraw(r);
 return true;
}
bool lp_rx_hint(struct lp_rx *r,const struct lp_packet *p,uint64_t end,uint64_t now){
 if(!r||!p||!r->cached||(p->kind!=LP_META&&p->kind!=LP_DISC)||p->schedule!=LP_FAST||
 now<end||now-end>200000||now>=r->observed_deadline_us||p->cursor<r->highest)return false;
 pending_clear(r);
 if(!p->time_valid){withdraw(r);return true;}
 if(!p->has_pair)return true;
 uint64_t at;if(!add(end,(uint64_t)p->delay_10ms*10000,&at)||at<250000||now>UINT64_MAX-25000||now+25000>at-250000)return false;
 r->hint=true;r->hint_cursor=p->cursor;r->hint_us=at;return true;
}
bool lp_rx_sync(struct lp_rx *r,uint32_t cursor,uint64_t tick,uint64_t epoch,uint64_t event,uint64_t now){
 if(!r||!r->cached||!r->hint||!epoch||event>UINT64_MAX-LP_HOLD_US||cursor!=r->hint_cursor||cursor<r->highest||
 event+250000<r->hint_us||event>r->hint_us+250000||now<event||now-event>500000||now>=r->observed_deadline_us){
  if(r)pending_clear(r);
  return false;
 }
 r->pending=true;r->hint=false;r->pending_cursor=cursor;r->pending_tick=tick;
 r->pending_epoch=epoch;r->pending_us=event;r->pending_deadline_us=event+1000000;
 return true;
}
static void fit(struct lp_rx *r,uint64_t tick,uint64_t ns){
 /* Uniform >=20s sample spacing bounds storage at16 points for300s. Every
  * physical pair is still counted; discarding a fit candidate never renews it. */
 unsigned n=r->fit_count;
 if(n&&(tick<=r->fit[n-1].tick||ns<=r->fit[n-1].tx_ns)){r->fit_count=0;n=0;}
 while(n&&tick-r->fit[0].tick>LP_WINDOW_US*16){memmove(r->fit,r->fit+1,(n-1)*sizeof r->fit[0]);--n;}
 if(n&&tick-r->fit[n-1].tick<UINT64_C(20000000)*16){r->fit_count=(uint8_t)n;return;}
 if(n==LP_FIT_CAPACITY){memmove(r->fit,r->fit+1,(--n)*sizeof r->fit[0]);}
 r->fit[n++]=(struct lp_pair){tick,ns};r->fit_count=(uint8_t)n;r->trained=false;
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
 r->rms_ns=lo==UINT32_MAX?lo:lo+1;r->rate_ppb=(int32_t)rate;r->trained=true;
}
bool lp_rx_follow(struct lp_rx *r,const struct lp_packet *p,const struct lp_age *a,uint64_t epoch,uint64_t now){
 if(!r||!p||p->kind!=LP_FOLLOW||p->schedule!=LP_FAST||!r->pending||p->cursor!=r->pending_cursor||epoch!=r->pending_epoch||
 now<r->pending_us||now>=r->pending_deadline_us||now>=r->observed_deadline_us){if(r){++r->rejected;pending_clear(r);}return false;}
 uint64_t age;bool bounded=lp_age_admit(a,p->utc_ns,now,&age);
 if(r->have_pair&&(p->cursor<=r->highest||p->utc_ns<=r->last_tx_ns||r->pending_tick<=r->last_tick)){++r->rejected;pending_clear(r);return false;}
 r->physical_deadline_us=r->pending_us+LP_HOLD_US;
 if(now>=r->physical_deadline_us){pending_clear(r);return false;}
 if(r->have_pair&&epoch!=r->last_epoch)r->fit_count=0;
 r->last_tick=r->pending_tick;r->last_tx_ns=p->utc_ns;r->last_us=r->pending_us;
 r->highest=p->cursor;r->have_pair=true;r->bounded=r->bounded&&bounded;r->last_epoch=epoch;
 fit(r,r->last_tick,r->last_tx_ns);++r->accepted;
 r->pending=false;r->prediction=true;r->hint=true;r->hint_cursor=p->cursor+2;r->hint_us=r->pending_us+LP_BEAT_US;
 return true;
}
void lp_rx_expire(struct lp_rx *r,uint64_t now){
 if(!r)return;
 if(r->pending&&now>=r->pending_deadline_us){++r->missing;pending_clear(r);}
 if(r->hint&&now>r->hint_us+250000){++r->missing;pending_clear(r);}
 if(r->have_pair&&now>=r->physical_deadline_us){r->have_pair=r->trained=false;r->fit_count=0;}
 if(r->cached&&now>=r->observed_deadline_us)withdraw(r); /* Keep replay/context floors. */
 if(r->bounded&&now>=r->meta_deadline_us){r->bounded=false;r->meta_deadline_us=r->tz_deadline_us=0;}
}
bool lp_rx_estimate(const struct lp_rx *r,uint64_t tick,uint64_t epoch,uint64_t now,uint64_t *ns){
 if(!r||!ns||!r->have_pair||!r->trained||epoch!=r->last_epoch||now>=r->physical_deadline_us||
 now>=r->observed_deadline_us||tick<r->last_tick||tick-r->last_tick>LP_HOLD_US*16)return false;
 uint64_t delta=tick-r->last_tick;
 int64_t nominal=(int64_t)(delta*125/2),correction=(int64_t)delta*r->rate_ppb/16000000;
 if(correction<0&&nominal< -correction)return false;
 return add(r->last_tx_ns,(uint64_t)(nominal+correction),ns);
}
bool lp_rx_envelope_current(const struct lp_rx *r,const struct lp_age *a,uint64_t now){
 return r&&a&&r->bounded&&r->trained&&r->have_pair&&now<r->physical_deadline_us&&
 now<r->meta_deadline_us&&now<a->valid_until_us;
}

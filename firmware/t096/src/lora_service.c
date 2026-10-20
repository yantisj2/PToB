/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * One background owner, below all normal BLE/time owners. No BLE publication,
 * precision training, clock authority, allocation, ranging or listener SetTx. */
#include "lora_service.h"
#include "lora_hw.h"
#include "capture.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define R2_LOCK_UNIT 26u
#include "health_lock.h"
#define ALLOWED 1u
#define PROFILE_RC 2u
#define NORMAL 4u
#define REQUESTED 8u
#define LOCAL_GM 16u
#define SOURCE_PRESENT 32u
#define DIAGNOSTIC_MIN_US UINT64_C(2000000)
#define CONTEXT_REPEAT_US UINT64_C(16000000)
#define CADENCE_US UINT64_C(4000000)
static atomic_t flags,closed=1,faulted;
static struct k_spinlock lock;
static const struct r2_group_config *group;
static struct rc8_enrollment binding;
static uint64_t physical_boot,stop_deadline;
static bool transmitter;
static uint32_t generation;
struct tx_state {
 struct rc8_export maps[2];
 struct rc8_correction previous;
 uint64_t stream,next_us,meta_us,pending_stream,repeat_us,diag_not_before,parent_seen_us;
 uint32_t sequence,pending_sequence;uint16_t repeat_events;
 bool pending,metadata,previous_valid;
};
static union {struct tx_state tx;struct rc8_receiver rx;} role;
struct snapshot {
 struct rc8_scope scope;
 struct rc8_quality quality;
 uint64_t diagnostic_us;uint32_t diagnostic_sequence,snapshot_age_ms,event_revision;uint16_t diagnostic_events;bool time_valid,quality_known;
 struct rc8_event event;
 struct rc8_correction correction;
 struct r2_lora_view view;
 uint8_t context[RC8_CONTEXT_BYTES];
 uint32_t sequence,corrected_sequence,wire_corrected_sequence,accepted,rejected,replayed,unmatched,overflow,metadata,missing_map;
 uint8_t authority;
};
static struct snapshot published;
/* Called with lock held; ordinary source/quality snapshots cannot retain a
 * successfully submitted event bitmap forever under higher-priority updates. */
static void events(uint16_t bits){published.diagnostic_events|=bits;++published.event_revision;}
K_THREAD_STACK_DEFINE(worker_stack,2048);
static struct k_thread worker_thread;
static uint64_t mono(void){return k_ticks_to_us_floor64(k_uptime_ticks());}
static void inc(uint32_t *n){if(*n<UINT32_MAX)++*n;}
static uint32_t age_ms(uint64_t now,uint64_t at){uint64_t age=now>=at?(now-at)/1000:UINT64_MAX;return age>UINT32_MAX?UINT32_MAX:(uint32_t)age;}
/* IRQ observation/poll arrival never supplies a physical event timestamp. */
static bool physical(const struct r2_lora_hw_event *h,struct rc8_event *e){
 struct r2_capture now;uint64_t us;
 if(!h||!e||!r2_capture_now_at(&now,&us)||now.epoch!=h->capture_epoch||now.tick<h->tick)return false;
 uint64_t delta=now.tick-h->tick;
 if(delta>RC8_CORRECTION_US*16||delta>UINT64_MAX-15)return false;
 uint64_t age=(delta+15)/16;if(age>us)return false;
 *e=(struct rc8_event){.tick=h->tick,.capture_epoch=h->capture_epoch,.local_us=us-age,
 .generation=h->generation,.kind=h->kind==R2_LORA_HW_TX_DONE?RC8_EVENT_TX_DONE:RC8_EVENT_RX_DONE};return true;
}
static bool scope_source_equal(struct rc8_scope a,struct rc8_scope b){a.stream=b.stream=1;return rc8_scope_equal(&a,&b);}
bool r2_lora_source_needed(void){
 atomic_val_t f=atomic_get(&flags);
 return transmitter&&!atomic_get(&faulted)&&(f&ALLOWED)&&(f&REQUESTED)&&!(f&PROFILE_RC);
}
void r2_lora_source_publish(const struct rc8_export *input){
 if(!r2_lora_source_needed()||!input)return;
 struct rc8_export map=*input;
 map.scope.emitter=binding.factory;map.scope.emitter_boot=physical_boot;map.scope.stream=1;
 if(!map.valid||!map.authenticated||rc8_scope_validate(&map.scope)){r2_lora_source_withdraw();return;}
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(!r2_lora_source_needed()){k_spin_unlock(&lock,key);return;}
 struct tx_state *t=&role.tx;
 struct rc8_export *last=&t->maps[0];
 if(last->valid&&last->scope.root==map.scope.root&&last->scope.root_boot==map.scope.root_boot&&map.scope.source_epoch<last->scope.source_epoch){
  atomic_or(&faulted,1);k_spin_unlock(&lock,key);return; /* No source-epoch wrap within a root boot. */
 }
 bool same=last->valid&&scope_source_equal(last->scope,map.scope)&&last->capture_epoch==map.capture_epoch;
 if(!same){
  if(t->stream==UINT64_MAX){atomic_set(&faulted,1);k_spin_unlock(&lock,key);return;}
  uint16_t changed=RC8_D_SOURCE;
  if(last->scope.root!=map.scope.root||last->scope.root_boot!=map.scope.root_boot)changed|=RC8_D_ROOT;
  if(last->scope.parent!=map.scope.parent||last->scope.parent_boot!=map.scope.parent_boot)changed|=RC8_D_PARENT;
  ++t->stream;memset(t->maps,0,sizeof t->maps);t->previous_valid=false;
  events(changed);t->repeat_us=mono()+CONTEXT_REPEAT_US;t->repeat_events=published.diagnostic_events;
  published.event=(struct rc8_event){0};published.correction=(struct rc8_correction){0};published.corrected_sequence=published.wire_corrected_sequence=0;
  memset(published.context,0,sizeof published.context);
  published.quality.known&=(uint16_t)~(RC8_Q_PARENT_CLASS|RC8_Q_PARENT_ACCURACY|RC8_Q_PARENT_BOUND|RC8_Q_PARENT_PPS|RC8_Q_PARENT_GNSS);published.quality.values&=published.quality.known&15u;t->parent_seen_us=0;
 }
 map.scope.stream=t->stream;
 if(same&&last->anchor_tick==map.anchor_tick&&last->anchor_ns==map.anchor_ns&&
 last->rate_q16==map.rate_q16&&last->phase_ns==map.phase_ns&&last->rate_ppb==map.rate_ppb&&last->mode==map.mode){
  /* Repeated owner snapshots cannot renew original timing/authority expiry. */
  if(map.until_tick<last->until_tick)last->until_tick=map.until_tick;
  if(map.expires_ns<last->expires_ns)last->expires_ns=map.expires_ns;
 }else{
  /* The locked physical capture is the map publication linearization point.
   * Its original formula already exists; it cannot map an earlier event. */
  struct r2_capture commit;
  if(!r2_capture_now(&commit)||commit.epoch!=map.capture_epoch||commit.tick<map.from_tick||commit.tick>=map.until_tick||
   (commit.tick-map.from_tick)/16>UINT32_MAX-map.map_age_us){
   for(unsigned i=0;i<2;i++)t->maps[i].valid=false;
   k_spin_unlock(&lock,key);return;
  }
  map.map_age_us+=(uint32_t)((commit.tick-map.from_tick)/16);map.from_tick=commit.tick;
  if(same){
   if(map.from_tick<=last->from_tick){k_spin_unlock(&lock,key);return;}
   if(last->until_tick>map.from_tick)last->until_tick=map.from_tick;
   t->maps[1]=*last;
  }
  *last=map;
 }
 if(!(atomic_get(&flags)&SOURCE_PRESENT)){events(RC8_D_RECOVERY);if(published.quality.recoveries<255)++published.quality.recoveries;}
 atomic_or(&flags,SOURCE_PRESENT);
 published.scope=map.scope;published.time_valid=true;published.diagnostic_us=mono();
 k_spin_unlock(&lock,key);
}
void r2_lora_source_withdraw(void){
 if(!transmitter||!(atomic_and(&flags,~SOURCE_PRESENT)&SOURCE_PRESENT))return;
 struct r2_capture now;bool valid=r2_capture_now(&now);
 k_spinlock_key_t key=k_spin_lock(&lock);
 role.tx.previous_valid=false;published.time_valid=false;events(RC8_D_LOSS);
 published.quality.known&=(uint16_t)~(RC8_Q_RATE|RC8_Q_RESIDUAL|RC8_Q_CORRECTION_AGE);
 published.quality.known|=RC8_Q_LOCK|RC8_Q_MAP_LIFE;published.quality.values&=(uint16_t)~RC8_Q_LOCK;published.quality.map_life_ms=0;published.diagnostic_us=mono();
 if(published.quality.losses<255)++published.quality.losses;
 for(unsigned i=0;i<2;i++)if(valid&&role.tx.maps[i].capture_epoch==now.epoch)
  (void)rc8_export_close(&role.tx.maps[i],now.tick,now.epoch);
 else role.tx.maps[i].valid=false;
 k_spin_unlock(&lock,key);
}
static void parent_age(uint64_t now){
 if((published.quality.known&(RC8_Q_PARENT_CLASS|RC8_Q_PARENT_ACCURACY))&&
 (!role.tx.parent_seen_us||now<role.tx.parent_seen_us||now-role.tx.parent_seen_us>=10000000)){
  published.quality.known&=(uint16_t)~(RC8_Q_PARENT_CLASS|RC8_Q_PARENT_ACCURACY);events(RC8_D_QUALITY);
 }
}
void r2_lora_quality_publish(const struct rc8_scope *scope,const struct rc8_quality *input){
 if(!transmitter||!scope||!input||!r2_lora_source_needed())return;
 k_spinlock_key_t key=k_spin_lock(&lock);
 const struct rc8_scope *current=&published.scope;
 if(scope->emitter!=current->emitter||scope->root!=current->root||scope->root_boot!=current->root_boot||
 scope->source_epoch!=current->source_epoch||scope->scale!=current->scale||scope->kind!=current->kind||
 scope->parent!=current->parent||scope->parent_boot!=current->parent_boot){k_spin_unlock(&lock,key);return;}
 parent_age(mono());
 struct rc8_quality q=*input;q.known&=8191u;q.values&=q.known&15u;
 q.losses=published.quality.losses;q.recoveries=published.quality.recoveries;
 /* Parent advertisements are independently supplied by CONTROL, never GNSS. */
 q.known|=published.quality.known&(RC8_Q_PARENT_CLASS|RC8_Q_PARENT_ACCURACY);
 q.parent_class=published.quality.parent_class;q.parent_accuracy=published.quality.parent_accuracy;
 if(((q.known^published.quality.known)&(RC8_Q_LOCK|RC8_Q_HOLDOVER|RC8_Q_ROLE|RC8_Q_HOP))||
 ((q.values^published.quality.values)&(RC8_Q_LOCK|RC8_Q_HOLDOVER))||q.role!=published.quality.role||q.hop!=published.quality.hop)
  events(RC8_D_QUALITY);
 if((q.values^published.quality.values)&RC8_Q_HOLDOVER)events(RC8_D_HOLDOVER);
 published.quality=q;published.quality_known=true;published.diagnostic_us=mono();
 k_spin_unlock(&lock,key);
}
void r2_lora_parent_quality(uint64_t factory,uint64_t boot,uint8_t clock_class,uint8_t accuracy,uint64_t seen){
 if(!transmitter)return;
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(published.scope.parent==factory&&published.scope.parent_boot==boot&&factory&&boot&&seen&&mono()>=seen&&mono()-seen<10000000){
  if(!(published.quality.known&RC8_Q_PARENT_CLASS)||published.quality.parent_class!=clock_class||published.quality.parent_accuracy!=accuracy)
   events(RC8_D_QUALITY);
  published.quality.known|=RC8_Q_PARENT_CLASS|RC8_Q_PARENT_ACCURACY;
  published.quality.parent_class=clock_class;published.quality.parent_accuracy=accuracy;role.tx.parent_seen_us=seen;
 }
 k_spin_unlock(&lock,key);
}
static bool current_map(struct rc8_export *out,uint64_t tick,uint64_t epoch,uint64_t stream){
 bool found=false;k_spinlock_key_t key=k_spin_lock(&lock);
 for(unsigned i=0;i<2;i++){
  const struct rc8_export *m=&role.tx.maps[i];
  if(m->valid&&m->capture_epoch==epoch&&tick>=m->from_tick&&tick<m->until_tick&&(!stream||m->scope.stream==stream)){
   *out=*m;found=true;break;
  }
 }
 k_spin_unlock(&lock,key);return found&&rc8_export_valid_at(out,tick,epoch);
}
static void stop_now(void){
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(!stop_deadline)stop_deadline=mono()+RC8_DRAIN_US;
 uint64_t deadline=stop_deadline;k_spin_unlock(&lock,key);
 r2_lora_hw_stop_begin(deadline);
}
void r2_lora_stop(void){atomic_and(&flags,~ALLOWED);if(!atomic_get(&closed))stop_now();}
void r2_lora_control(bool rc,bool allowed,enum rc8_selected preferred){
 atomic_val_t old=atomic_get(&flags),next;
 do{next=(old&(REQUESTED|SOURCE_PRESENT))|(rc?PROFILE_RC:0)|(allowed?ALLOWED:0)|(preferred!=RC8_SELECT_NONE?NORMAL:0)|(preferred==RC8_SELECT_LOCAL_GM?LOCAL_GM:0);}while(!atomic_cas(&flags,old,next)&&(old=atomic_get(&flags),true));
 if(!allowed||(transmitter&&rc))if(!atomic_get(&closed))stop_now();
}
void r2_lora_request(bool on){
 if(on)atomic_or(&flags,REQUESTED);else{atomic_and(&flags,~REQUESTED);if(!atomic_get(&closed))stop_now();}
}
void r2_lora_toggle(void){r2_lora_request(!(atomic_get(&flags)&REQUESTED));}
int r2_lora_close_result(void){return (atomic_get(&faulted)&2)?-EIO:atomic_get(&closed)?0:-EAGAIN;}
void r2_lora_view(struct r2_lora_view *out){
 if(!out)return;
 k_spinlock_key_t key=k_spin_lock(&lock);*out=published.view;k_spin_unlock(&lock,key);
 uint64_t now=mono();
 if(out->utc_us&&now>=out->sampled_us)out->utc_us+=now-out->sampled_us;
 out->sampled_us=now;
 if(!(atomic_get(&flags)&REQUESTED)||!(atomic_get(&flags)&ALLOWED)||!out->until_us||now>=out->until_us){out->utc_us=0;out->choice=(atomic_get(&flags)&NORMAL)?((atomic_get(&flags)&LOCAL_GM)?RC8_SELECT_LOCAL_GM:RC8_SELECT_BLE):RC8_SELECT_NONE;}
}
static void __attribute__((noinline)) report(uint64_t now,bool active){
 struct snapshot s={0};atomic_val_t f=atomic_get(&flags);
 k_spinlock_key_t key=k_spin_lock(&lock);if(transmitter)parent_age(mono());s=published;k_spin_unlock(&lock,key);
 s.view=(struct r2_lora_view){.requested=(f&REQUESTED)!=0,.active=active,.transmitter=transmitter,.fault=atomic_get(&faulted)!=0,.sampled_us=now};
 if(!transmitter){
  struct rc8_receiver *r=&role.rx;
  s.scope=r->scope;memcpy(s.context,r->context,sizeof s.context);
  s.quality=r->quality;s.quality_known=r->metadata_known&&now<r->metadata_until_us;s.time_valid=r->time_valid;
  s.diagnostic_us=r->diagnostic_us;s.diagnostic_sequence=r->diagnostic_sequence;s.snapshot_age_ms=r->snapshot_age_ms;s.diagnostic_events=r->diagnostic_events;
  s.view.state=rc8_receiver_state(r,now);s.view.assurance=rc8_receiver_assurance(r,now);
  s.authority=rc8_receiver_authority(r,now);
  s.view.choice=rc8_select((f&NORMAL)!=0,s.view.state,s.view.assurance,true);
  if(s.view.choice==RC8_SELECT_BLE&&(f&LOCAL_GM))s.view.choice=RC8_SELECT_LOCAL_GM; /* explicit lower-quality policy */
  s.accepted=r->accepted;s.rejected=r->rejected;s.replayed=r->replayed;s.unmatched=r->unmatched;s.overflow=r->overflow;s.sequence=r->highest_sequence;
  s.view.root_tag=(uint16_t)r->scope.root;s.view.emitter_tag=(uint16_t)r->scope.emitter;
  if(!r->sample.valid){s.event=(struct rc8_event){0};s.correction=(struct rc8_correction){0};s.corrected_sequence=0;}
  if(r->sample.valid){
   s.event=r->sample.rx;s.correction=r->sample.tx;s.corrected_sequence=r->sample.tx.sequence;
   uint64_t age=now>=r->sample.rx.local_us?now-r->sample.rx.local_us:UINT64_MAX;
   s.view.age_ms=age/1000>UINT32_MAX?UINT32_MAX:(uint32_t)(age/1000);
   if(s.view.state==RC8_AVAILABLE&&r->scope.scale==RC8_UTC&&age<RC8_HOLD_US&&r->sample.tx.event_ns<=UINT64_MAX-age*1000){
    s.view.utc_us=r->sample.tx.event_ns/1000+age;
    s.view.until_us=r->sample.rx.local_us+RC8_HOLD_US;
   }
  }
 }else{
  s.view.choice=(f&NORMAL)?((f&LOCAL_GM)?RC8_SELECT_LOCAL_GM:RC8_SELECT_BLE):RC8_SELECT_NONE;
  struct r2_capture capture;struct rc8_export map;
  bool source=r2_capture_now(&capture)&&current_map(&map,capture.tick,capture.epoch,0);
  s.view.state=source?RC8_AVAILABLE:RC8_ACQUIRING;
  s.view.root_tag=(uint16_t)s.scope.root;s.view.emitter_tag=(uint16_t)s.scope.emitter;
  if(s.correction.sequence&&s.scope.scale==RC8_UTC&&now>=s.event.local_us&&now-s.event.local_us<RC8_HOLD_US){
   s.view.utc_us=s.correction.event_ns/1000+now-s.event.local_us;s.view.until_us=s.event.local_us+RC8_HOLD_US;
  }
 }
 s.view.quality_known=s.quality_known;s.view.time_valid=s.time_valid;s.view.parent_tag=(uint16_t)s.scope.parent;
 key=k_spin_lock(&lock);if(transmitter)published.view=s.view;else published=s;k_spin_unlock(&lock,key);
}
static void __attribute__((noinline)) tx_done(const struct r2_lora_hw_event *h,struct rc8_event e){
 struct tx_state *t=&role.tx;
 if(!t->pending||h->generation!=generation)return;
 t->pending=false;
 if(t->metadata)return;
 struct rc8_export map;struct rc8_correction c;
 bool valid=current_map(&map,e.tick,e.capture_epoch,t->pending_stream)&&rc8_export_event(&c,&map,t->pending_sequence,e);
 k_spinlock_key_t key=k_spin_lock(&lock);
 t->previous_valid=valid;
 if(valid){t->previous=c;published.scope=map.scope;published.event=e;published.correction=c;published.corrected_sequence=c.sequence;inc(&published.accepted);}
 else inc(&published.missing_map);
 k_spin_unlock(&lock,key);
}
static int __attribute__((noinline)) transmit(uint64_t now){
 now=mono();
 struct tx_state *t=&role.tx;
 if(t->pending)return 0;
 struct r2_capture capture;if(!r2_capture_now(&capture))return 0;
 struct rc8_export map;bool valid=current_map(&map,capture.tick,capture.epoch,0);
 if(!valid)r2_lora_source_withdraw();
 uint8_t wire[RC8_META_BYTES];size_t length;
 k_spinlock_key_t key=k_spin_lock(&lock);
 bool timing=valid&&now>=t->next_us;
 uint64_t repeat_at=t->repeat_us;bool repeat=repeat_at&&now>=repeat_at;
 bool diagnostic=published.scope.emitter&&now>=t->diag_not_before&&
  (published.diagnostic_events||now>=t->meta_us||repeat);
 uint64_t deadline=valid?t->next_us:UINT64_MAX;
 uint32_t sequence=t->sequence;
 bool previous=t->previous_valid&&valid&&t->pending_stream==map.scope.stream;
 struct rc8_correction correction=t->previous;
 k_spin_unlock(&lock,key);
 /* Timing and its prior physical correction always win. Diagnostics can also
  * report loss with no valid time map, but remain subject to TX controls. */
 if(!timing&&!diagnostic)return 0;
 bool metadata=!timing;
 uint32_t diag_sequence=0,event_revision=0,encoded_age=0;uint16_t event_bits=0;
 if(metadata){
  uint64_t budget=r2_lora_hw_budget_us(RC8_META_BYTES)+R2_LORA_HW_LAUNCH_MARGIN_US;
  if(!budget||now>deadline||budget>deadline-now)return 0;
  struct rc8_companion p;
  key=k_spin_lock(&lock);parent_age(mono());
  if(published.diagnostic_sequence==UINT32_MAX){k_spin_unlock(&lock,key);return -EOVERFLOW;}
  if(valid&&!rc8_scope_equal(&map.scope,&published.scope)){k_spin_unlock(&lock,key);return 0;}
  diag_sequence=published.diagnostic_sequence+1;event_bits=published.diagnostic_events;event_revision=published.event_revision;
  p=(struct rc8_companion){.scope=published.scope,.quality=published.quality,.sequence=diag_sequence,
   .snapshot_age_ms=age_ms(now,published.diagnostic_us),
   .timing_sequence=t->sequence,.events=(uint16_t)(event_bits|(repeat?t->repeat_events:0)|(!event_bits&&!repeat?RC8_D_PERIODIC:0)),.time_valid=valid};
  if((p.quality.known&(RC8_Q_PARENT_CLASS|RC8_Q_PARENT_ACCURACY))&&age_ms(now,t->parent_seen_us)>p.snapshot_age_ms)p.snapshot_age_ms=age_ms(now,t->parent_seen_us);
  encoded_age=p.snapshot_age_ms;
  k_spin_unlock(&lock,key);
  if(rc8_companion_encode(wire,&p,group))return -EINVAL;
  length=RC8_META_BYTES;
 }else{
  if(sequence==UINT32_MAX)return -EOVERFLOW;
  struct rc8_packet p={.sequence=sequence+1,.root=map.scope.root,.source_epoch=map.scope.source_epoch,
   .scale=map.scope.scale,.kind=map.scope.kind,.corrected=previous};
  if(previous){
   p.correction=correction;
   if(rc8_packet_validate(&p)){
    p.corrected=previous=false;p.correction=(struct rc8_correction){0};
    key=k_spin_lock(&lock);t->previous_valid=false;inc(&published.missing_map);k_spin_unlock(&lock,key);
   }
  }
  if(rc8_context_id(p.context,&map.scope,group)||rc8_encode(wire,&p,group))return -EINVAL;
  length=RC8_BYTES;
 }
 if(generation==UINT32_MAX)return -EOVERFLOW;
 /* The driver repeats this deadline check immediately before FEM enable,
  * after packet SPI preparation and any higher-priority BLE preemption. */
 now=mono();uint64_t budget=r2_lora_hw_budget_us(length)+R2_LORA_HW_LAUNCH_MARGIN_US;
 if(metadata&&(now>deadline||budget>deadline-now))return 0;
 if(!metadata){uint64_t stream=map.scope.stream;if(!r2_capture_now(&capture)||!current_map(&map,capture.tick,capture.epoch,stream))return 0;}
 uint32_t next_generation=generation+1;
 int result=r2_lora_hw_tx_before(wire,length,next_generation,metadata?deadline:UINT64_MAX);
 if(result==-EAGAIN&&metadata)return 0;
 if(result)return result;
 generation=next_generation;
 key=k_spin_lock(&lock);t->pending=true;t->metadata=metadata;
 if(metadata){t->meta_us=now+RC8_META_REFRESH_US;t->diag_not_before=now+DIAGNOSTIC_MIN_US;
  if(repeat&&t->repeat_us==repeat_at){t->repeat_us=0;t->repeat_events=0;}
  published.diagnostic_sequence=diag_sequence;published.snapshot_age_ms=encoded_age;if(published.event_revision==event_revision)published.diagnostic_events&=(uint16_t)~event_bits;inc(&published.metadata);
 }else{t->pending_stream=map.scope.stream;t->pending_sequence=++t->sequence;t->next_us=now+CADENCE_US;
  published.scope=map.scope;memcpy(published.context,wire+4,RC8_CONTEXT_BYTES);
  published.wire_corrected_sequence=previous?correction.sequence:0;
 }
 published.sequence=t->sequence;
 k_spin_unlock(&lock,key);return 0;
}
static struct {bool active,stopping;} owner;
static bool cancelled(void){
 atomic_val_t f=atomic_get(&flags);
 k_spinlock_key_t key=k_spin_lock(&lock);bool stopping=stop_deadline!=0;k_spin_unlock(&lock,key);
 return stopping||!(f&ALLOWED)||!(f&REQUESTED)||(transmitter&&(f&PROFILE_RC));
}
static void begin_stop(int error){
 if(error&&!(error==-ECANCELED&&cancelled()))atomic_set(&faulted,1);
 stop_now();owner.stopping=true;
 if(!transmitter)rc8_receiver_switch(&role.rx,false);
 k_spinlock_key_t key=k_spin_lock(&lock);published.view.utc_us=published.view.until_us=0;published.view.active=false;k_spin_unlock(&lock,key);
}
/* Keep packet processing off the simultaneous encode/HMAC call chain. */
static void __attribute__((noinline)) process_event(uint64_t now){
  struct r2_lora_hw_event h;int result=r2_lora_hw_take(&h);
  if(result<0)begin_stop(result);
  else if(result==1){
   struct rc8_event e;
   if(physical(&h,&e)){
    if(transmitter&&h.kind==R2_LORA_HW_TX_DONE)tx_done(&h,e);
    else if(!transmitter&&h.kind==R2_LORA_HW_RX_DONE&&h.generation==generation){
     (void)rc8_receiver_freeze(&role.rx,h.frame,h.length,e);
     (void)rc8_receiver_process(&role.rx,group,now);
     if(h.length==RC8_META_BYTES&&role.rx.metadata_known&&role.rx.diagnostic_us==e.local_us){
      k_spinlock_key_t key=k_spin_lock(&lock);inc(&published.metadata);k_spin_unlock(&lock,key);
     }
    }
   }else if(transmitter){
    role.tx.pending=role.tx.previous_valid=false;k_spinlock_key_t key=k_spin_lock(&lock);inc(&published.missing_map);k_spin_unlock(&lock,key);
   }else inc(&role.rx.rejected);
   if(!transmitter&&h.kind==R2_LORA_HW_RX_DONE){int rc=r2_lora_hw_rx_rearm();if(rc)begin_stop(rc);}
  }
 }
/* One production iteration, also exercised with public fake hardware boundaries. */
static void __attribute__((noinline)) service_step(void){
 uint64_t now=mono();atomic_val_t f=atomic_get(&flags);
 bool desired=!atomic_get(&faulted)&&(f&ALLOWED)&&(f&REQUESTED)&&binding.enrolled&&binding.factory&&binding.transmitter_factory&&group&&group->id&&(!transmitter||!(f&PROFILE_RC));
 if((!desired||cancelled())&&owner.active&&!owner.stopping)begin_stop(0);
 if(owner.stopping){
  struct r2_lora_hw_ack ack={0};r2_lora_hw_stop_poll(&ack);
  k_spinlock_key_t key=k_spin_lock(&lock);uint64_t deadline=stop_deadline;k_spin_unlock(&lock,key);
  if(ack.fault)atomic_or(&faulted,1);
  if(now>=deadline)atomic_or(&faulted,2);
  if(ack.dma_retired&&ack.radio_quiescent&&ack.fem_off&&ack.capture_released){
   owner.active=owner.stopping=false;atomic_set(&closed,1);
   key=k_spin_lock(&lock);stop_deadline=0;
   if(transmitter){role.tx.pending=role.tx.previous_valid=false;role.tx.meta_us=0;role.tx.repeat_us=0;role.tx.repeat_events=0;}
   k_spin_unlock(&lock,key);
  }
 }else if(desired&&!owner.active){
  if(generation==UINT32_MAX)atomic_set(&faulted,1);
  else{
   ++generation;atomic_set(&closed,0);
   if(!transmitter){rc8_receiver_switch(&role.rx,true);role.rx.generation=generation;}
   int result=r2_lora_hw_start(generation,!transmitter);
   if(result==-EAGAIN&&!cancelled()){
    if(!transmitter)rc8_receiver_switch(&role.rx,false);
    atomic_set(&closed,1); /* Capture not ready; no radio/capture ownership. */
   }else{
    owner.active=true;
    if(result)begin_stop(result);
   }
  }
 }else if(owner.active){
  process_event(now);
  if(transmitter&&!owner.stopping){int rc=transmit(now);if(rc)begin_stop(rc);}
 }
 report(now,owner.active&&!owner.stopping);
}
static void worker(void *a,void *b,void *c){
 (void)a;(void)b;(void)c;
 for(;;){service_step();k_sleep(K_MSEC(2));}
}

void r2_lora_init(uint64_t factory,uint64_t boot,uint64_t tx,const struct r2_group_config *g){
 binding=(struct rc8_enrollment){.factory=factory,.transmitter_factory=tx,.enrolled=factory&&boot&&g&&g->id};
 physical_boot=boot;group=g;transmitter=rc8_is_transmitter(&binding);
 published.diagnostic_events=RC8_D_BOOT;published.event_revision=1;
 if(transmitter){
  role.tx.stream=1;role.tx.repeat_us=mono()+CONTEXT_REPEAT_US;role.tx.repeat_events=RC8_D_BOOT;
  published.scope=(struct rc8_scope){.emitter=factory,.emitter_boot=boot,.stream=1,.kind=RC8_NO_SOURCE,.scale=RC8_RELATIVE};
  published.quality.known=RC8_Q_LOCK|RC8_Q_ROLE;published.quality_known=true;published.diagnostic_us=mono();
 }
 struct rc8_requests requests=rc8_requests_default(&binding);
 if(!transmitter)(void)rc8_receiver_init(&role.rx,&binding);
 atomic_set(&flags,(requests.tx||requests.rx)?REQUESTED:0);
 k_thread_create(&worker_thread,worker_stack,K_THREAD_STACK_SIZEOF(worker_stack),worker,NULL,NULL,NULL,11,0,K_NO_WAIT);
 k_thread_name_set(&worker_thread,"rc8-radio");
}
static uint32_t stack_unused(void){
#ifdef CONFIG_INIT_STACKS
 size_t unused;
 if(!k_thread_stack_space_get(&worker_thread,&unused)&&unused<=UINT32_MAX)return (uint32_t)unused;
#endif
 return UINT32_MAX;
}
int r2_lora_json(char *out,size_t capacity,int result){
 struct snapshot s;k_spinlock_key_t key=k_spin_lock(&lock);s=published;uint64_t deadline=stop_deadline;k_spin_unlock(&lock,key);
 r2_lora_view(&s.view);
 struct r2_lora_hw_diag h;r2_lora_hw_diag(&h);
 char context[33];for(unsigned i=0;i<16;i++)snprintf(context+2*i,3,"%02x",s.context[i]);
 return snprintf(out,capacity,
 "{\"rc8\":3,\"result\":%d,\"boot\":\"%016llx\",\"emitter_boot\":\"%016llx\",\"configured_tx\":\"%016llx\",\"transmitter\":%s,\"requested\":%s,\"active\":%s,\"fault\":%s,"
 "\"closed\":%s,\"fault_bits\":%ld,\"drain_deadline_us\":%llu,\"stack_unused\":%u,\"context\":\"%s\",\"emitter\":\"%016llx\",\"stream\":%llu,\"root\":\"%016llx\",\"root_boot\":\"%016llx\",\"source_epoch\":%u,\"kind\":%u,\"scale\":%u,"
 "\"sequence\":%u,\"corrected_sequence\":%u,\"wire_corrected_sequence\":%u,\"event_tick\":%llu,\"capture_epoch\":%llu,\"event_mono_us\":%llu,\"event_ns\":%llu,\"created_ns\":%llu,\"expires_ns\":%llu,\"map_generation\":%u,\"map_age_us\":%u,\"upstream_bound_ns\":%u,"
 "\"accepted\":%u,\"rejected\":%u,\"replayed\":%u,\"unmatched\":%u,\"overflow\":%u,\"metadata\":%u,\"missing_map\":%u,\"hold_age_ms\":%u,\"state\":%u,\"assurance\":%u,\"source_authority\":%u,\"choice\":%u,\"output_bound_ns\":null,\"rc7_authority\":false,"
 "\"companion\":[%u,%u,%u,%u,%s,%s],\"parent\":[\"%016llx\",\"%016llx\"],\"quality\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%u,%u,%u],"
 "\"hw\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%d,%u,%u]}",
 result,(unsigned long long)physical_boot,(unsigned long long)s.scope.emitter_boot,(unsigned long long)binding.transmitter_factory,transmitter?"true":"false",s.view.requested?"true":"false",s.view.active?"true":"false",atomic_get(&faulted)?"true":"false",
 atomic_get(&closed)?"true":"false",(long)atomic_get(&faulted),(unsigned long long)deadline,stack_unused(),context,(unsigned long long)s.scope.emitter,(unsigned long long)s.scope.stream,(unsigned long long)s.scope.root,(unsigned long long)s.scope.root_boot,s.scope.source_epoch,s.scope.kind,s.scope.scale,
 s.sequence,s.corrected_sequence,s.wire_corrected_sequence,(unsigned long long)s.event.tick,(unsigned long long)s.event.capture_epoch,(unsigned long long)s.event.local_us,(unsigned long long)s.correction.event_ns,(unsigned long long)s.correction.created_ns,(unsigned long long)s.correction.expires_ns,s.correction.map_generation,s.correction.map_age_us,s.correction.upstream_bound_ns,
 s.accepted,s.rejected,s.replayed,s.unmatched,s.overflow,s.metadata,s.missing_map,s.view.age_ms,s.view.state,s.view.assurance,s.authority,s.view.choice,
 s.diagnostic_sequence,s.snapshot_age_ms,s.diagnostic_events,age_ms(mono(),s.diagnostic_us),s.quality_known?"true":"false",s.time_valid?"true":"false",
 (unsigned long long)s.scope.parent,(unsigned long long)s.scope.parent_boot,s.quality.known,s.quality.values,s.quality.role,s.quality.hop,s.quality.parent_class,s.quality.parent_accuracy,s.quality.parent_bound_ns,s.quality.correction_age_ms,s.quality.map_life_ms,s.quality.rate_ppb,s.quality.residual_ns,s.quality.losses,s.quality.recoveries,
 h.rx_done,h.tx_done,h.crc_bad,h.overflow,h.spi_ok,h.spi_error,h.cutoffs,h.capture_epoch,h.generation,h.error,h.phase,h.invalid_capture);
}

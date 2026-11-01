/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Explicit per-boot lab selection, passive receivers and one background owner.
 * PPS witnesses never feed this estimator. No normal-source authority is exported. */
#include "lora_service.h"
#include "lora_passive_core.h"
#include "lora_passive_store.h"
#include "lora_hw.h"
#include "lora_watchdog.h"
#include "diagnostic.h"
#include "watchdog.h"
#include "rc_shadow.h"
#include "capture.h"
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#define R2_LOCK_UNIT 26u
#include "health_lock.h"
#define ALLOWED 1u
#define PROFILE_RC 2u
#define NORMAL 4u
#define REQUESTED 8u
#define SOURCE_PRESENT 32u
#define MESH_CONTACT 64u
#define WORKER_PRIORITY 11
#define DRAIN_PRIORITY 9
#define LAB_RUN_US UINT64_C(900000000)
#define REBOOT_CONTROL_GUARD_US UINT64_C(4000000)
#define PRECHARGE_LEAD_US UINT64_C(1200000)
static atomic_t observer_ready,flags,closed=1,faulted,source_revision;
static struct k_spinlock lock;
static const struct r2_group_config *group;
static uint64_t factory,physical_boot,transmitter_factory,stop_deadline,lab_until;
static bool transmitter;
static uint32_t generation;
static union {
 struct {struct lp_tx core;struct lp_store store;struct rc8_export maps[2];
  struct rc8_correction captured;struct r2_lora_hw_edge edge;
  uint64_t context_root_boot;uint32_t context_epoch;struct lp_reservation reservation;
  uint8_t context[16];uint64_t sync_ns;uint32_t stream;bool context_ready,loaded,rebased;} tx;
 struct lp_rx rx;
} role;
static struct r2_tz_lease source_tz;
static struct r2_lora_view published;
static uint64_t shadow_tick;
struct lp_audit {uint64_t tick,epoch,start_us,end_us;uint32_t cursor;uint8_t phy,channel,valid;};
/* Last physical event of each kind. Each kind recurs no faster than4s, so a
 * bounded1Hz console collector can retain every event and its denominator. */
static struct lp_audit audits[4];
struct lp_diagnostics {
 uint64_t emitter,boot;uint32_t stream,source_epoch,seed;
 union {
  struct {uint64_t last_tick,last_ns,epoch,last_us,physical_until,meta_until,root_boot;};
  struct {uint64_t next_slot,charged,tx_root_boot;};
 };
 uint32_t cursor,serial,accepted,rejected,missing,store_sequence,refused,late,missing_map,bad,storage_fail,sent[4];
 union {uint32_t received[4];struct {uint32_t reserve_lead_us;int32_t launch_late_us;uint32_t source_room_us,source_phase_us;};};
 union {int32_t rate;uint32_t storage_max_us;};int error;uint8_t pending,fh_disabled,stage,phy,channel;bool trained;
};
static struct lp_diagnostics diagnostics;
static struct {bool active,stopping,listening;uint8_t phy,channel,tx_kind,stage;
 union {uint64_t next_control_us;struct {uint64_t last_end_us,last_sync_us,quiet_until_us;uint32_t boot_ms,storage_max_us;};};
 uint32_t sent[4];union {uint32_t received[4];struct {uint32_t reserve_lead_us;int32_t launch_late_us;uint32_t source_room_us,source_phase_us;};};
 uint32_t refused,late,missing_map,bad,storage_fail,revision;
 int error;int16_t rssi;int8_t snr;} owner;
K_THREAD_STACK_DEFINE(worker_stack,2048);
static struct k_thread worker_thread;
static uint64_t mono(void){return k_ticks_to_us_floor64(k_uptime_ticks());}
static bool fenced(void);
static void inc(uint32_t *v){if(*v<UINT32_MAX)++*v;}
static void stop_now(void){
 k_thread_priority_set(&worker_thread,DRAIN_PRIORITY);
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(!stop_deadline){stop_deadline=mono()+RC8_DRAIN_US;
  r2_diag_scratch.lora.drain_deadline_us=stop_deadline;r2_diag_scratch.lora.drain_promoted_us=mono();
  r2_diag_scratch.lora.drain_poll_us=r2_diag_scratch.lora.drain_ack_us=0;}
 uint64_t deadline=stop_deadline;k_spin_unlock(&lock,key);
 r2_lora_hw_stop_begin(deadline);k_wakeup(&worker_thread);
}
void r2_lora_name_publish(const char *name){(void)name;}
void r2_lora_tz_publish(struct r2_tz tz,uint64_t until,uint64_t root,uint64_t boot,uint32_t epoch){
 if(!transmitter)return;
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(root!=factory||boot!=role.tx.context_root_boot||epoch!=role.tx.context_epoch||mono()>=until||!r2_tz_valid(tz)){tz=r2_tz_unknown();until=0;}
 source_tz=(struct r2_tz_lease){.value=tz,.until_us=until};k_spin_unlock(&lock,key);
}
void r2_lora_rtt_lab(bool on){(void)on;} /* No RTT frames exist in this wire. */
void r2_lora_control(bool rc,bool allowed,enum rc8_selected preferred){
 atomic_val_t old=atomic_get(&flags),next;
 do{next=(old&(REQUESTED|SOURCE_PRESENT|MESH_CONTACT))|(rc?PROFILE_RC:0)|(allowed?ALLOWED:0)|(preferred!=RC8_SELECT_NONE?NORMAL:0);
 }while(!atomic_cas(&flags,old,next)&&(old=atomic_get(&flags),true));
 if((!allowed||(transmitter&&rc))&&!atomic_get(&closed))stop_now();
}
void r2_lora_mesh_contact(bool present){if(present)atomic_or(&flags,MESH_CONTACT);else atomic_and(&flags,~MESH_CONTACT);}
void r2_lora_request(bool on){
 k_spinlock_key_t key=k_spin_lock(&lock);lab_until=on?mono()+LAB_RUN_US:0;k_spin_unlock(&lock,key);
 if(on)atomic_or(&flags,REQUESTED);else{atomic_and(&flags,~REQUESTED);if(!atomic_get(&closed))stop_now();}
 k_wakeup(&worker_thread);
}
void r2_lora_toggle(void){r2_lora_request(!(atomic_get(&flags)&REQUESTED));}
void r2_lora_stop(void){atomic_and(&flags,~ALLOWED);if(!atomic_get(&closed))stop_now();}
int r2_lora_close_result(void){
 if((atomic_get(&faulted)&2)||r2_diag_scratch.lora.terminal)return -EIO;
 return r2_diag_scratch.lora.drain_late?-ETIMEDOUT:atomic_get(&closed)?0:-EAGAIN;
}
bool r2_lora_source_needed(void){atomic_val_t f=atomic_get(&flags);return transmitter&&!atomic_get(&faulted)&&(f&ALLOWED)&&(f&REQUESTED)&&!(f&PROFILE_RC);}
/* Called under source lock before map turnover, so only a formula that was
 * valid at the original BUSY edge can be frozen. DONE must still validate it. */
static void preserve(void){
 if(!owner.tx_kind||role.tx.captured.sequence)return;
 struct r2_lora_hw_edge edge;if(!r2_lora_hw_tx_edge(&edge)||edge.generation!=generation)return;
 for(unsigned i=0;i<2;i++)if(rc8_export_event(&role.tx.captured,&role.tx.maps[i],1,
   (struct rc8_event){.tick=edge.tick,.capture_epoch=edge.capture_epoch,.kind=RC8_EVENT_TX_START})){
  role.tx.edge=edge;break;}
}
void r2_lora_source_publish(const struct rc8_export *input){
 if(!r2_lora_source_needed()||!input)return;
 struct rc8_export m=*input;m.scope.emitter=factory;m.scope.emitter_boot=physical_boot;m.scope.stream=1;
 if(!m.valid||!m.authenticated||m.scope.kind!=RC8_OWN_GM||m.scope.scale!=RC8_UTC||
 m.scope.root!=factory||!m.scope.root_boot||m.scope.parent||rc8_scope_validate(&m.scope)){r2_lora_source_withdraw();return;}
 k_spinlock_key_t key=k_spin_lock(&lock);preserve();
 struct rc8_export *last=&role.tx.maps[0];
 bool changed=last->valid&&(last->scope.source_epoch!=m.scope.source_epoch||last->scope.root_boot!=m.scope.root_boot);
 if(changed||!(atomic_get(&flags)&SOURCE_PRESENT))atomic_inc(&source_revision);
 if(!last->valid||last->map_generation!=m.map_generation){role.tx.maps[1]=*last;*last=m;}else *last=m;
 atomic_or(&flags,SOURCE_PRESENT);k_spin_unlock(&lock,key);
 if(changed&&!atomic_get(&closed))stop_now();
}
void r2_lora_source_withdraw(void){
 if(!transmitter)return;
 k_spinlock_key_t key=k_spin_lock(&lock);preserve();
 bool was_present=(atomic_and(&flags,~SOURCE_PRESENT)&SOURCE_PRESENT)!=0;
 if(was_present)atomic_inc(&source_revision);
 struct r2_capture now;bool valid=r2_capture_now(&now);
 for(unsigned i=0;i<2;i++)if(valid&&role.tx.maps[i].capture_epoch==now.epoch)(void)rc8_export_close(&role.tx.maps[i],now.tick,now.epoch);else role.tx.maps[i].valid=false;
 k_spin_unlock(&lock,key);
 /* The high-priority source owner can fence an in-flight launch without
  * SPI or flash. The worker performs the original bounded drain. */
 if(was_present&&!atomic_get(&closed))stop_now();
}
void r2_lora_quality_publish(const struct rc8_scope *s,const struct rc8_quality *q){(void)s;(void)q;}
void r2_lora_parent_quality(uint64_t f,uint64_t b,uint8_t c,uint8_t a,uint64_t s){(void)f;(void)b;(void)c;(void)a;(void)s;}
void r2_lora_coordination(struct r2_gm_coordination *out){if(out)memset(out,0,sizeof *out);}
void r2_lora_origin(bool active,uint64_t boot,uint32_t epoch){(void)active;(void)boot;(void)epoch;}
static bool source_now(struct rc8_correction *out,uint32_t *epoch,uint64_t *root_boot,struct r2_capture *limit){
 struct r2_capture at;if(!r2_capture_now(&at))return false;
 bool good=false;k_spinlock_key_t key=k_spin_lock(&lock);
 if(atomic_get(&flags)&SOURCE_PRESENT)for(unsigned i=0;i<2;i++)if(rc8_export_event(out,&role.tx.maps[i],1,
  (struct rc8_event){.tick=at.tick,.capture_epoch=at.epoch,.kind=RC8_EVENT_TX_START})){*epoch=role.tx.maps[i].scope.source_epoch;*root_boot=role.tx.maps[i].scope.root_boot;
   *limit=(struct r2_capture){.tick=role.tx.maps[i].until_tick,.epoch=role.tx.maps[i].capture_epoch,.kind=R2_CAPTURE_NOW};good=true;break;}
 k_spin_unlock(&lock,key);return good;
}
static void begin_stop(int error){
 if(error){owner.error=error;atomic_or(&faulted,1);}
 stop_now();owner.stopping=true;
 if(!transmitter){lp_rx_clear(&role.rx);r2_shadow_lora_withdraw();}
 k_spinlock_key_t key=k_spin_lock(&lock);published.active=false;published.utc_us=published.until_us=published.display_until_us=0;k_spin_unlock(&lock,key);
}
static bool physical(const struct r2_lora_hw_event *h,uint64_t *event){
 struct r2_capture at;uint64_t us;if(!h->capture_epoch||!r2_capture_now_at(&at,&us)||at.epoch!=h->capture_epoch||at.tick<h->tick)return false;
 uint64_t age=(at.tick-h->tick+15)/16;if(age>2000000||age>us)return false;
 *event=us-age;return true;
}
static int tune(unsigned phy,unsigned channel,bool receive){
 if(owner.listening){int rc=r2_lora_hw_idle();if(rc)return rc;owner.listening=false;}
 int rc=r2_lora_hw_passive_configure(phy,channel);
 if(!rc){owner.phy=(uint8_t)phy;owner.channel=(uint8_t)channel;}
 if(!rc&&receive){rc=r2_lora_hw_rx_rearm();if(!rc)owner.listening=true;}
 return rc;
}
static void __attribute__((noinline)) report(uint64_t now){
 struct r2_lora_view v={.requested=(atomic_get(&flags)&REQUESTED)!=0,.active=owner.active&&!owner.stopping,
 .transmitter=transmitter,.fault=atomic_get(&faulted)!=0,.sampled_us=now,.state=RC8_ACQUIRING};
 if(!v.requested||!v.active)v.state=RC8_OFF;
 /* Worker-owned policy is computed with interrupts enabled. Publication
  * only copies bounded scalars;61 bucket divisions must never mask BLE. */
 uint64_t charged=transmitter?lp_charged(&role.tx.core.durable,now):0;
 if(transmitter){v.kind=RC8_OWN_GM;v.time_valid=(atomic_get(&flags)&SOURCE_PRESENT)!=0;
  v.root_tag=v.emitter_tag=(uint16_t)factory;if(v.active&&v.requested&&v.time_valid)v.state=RC8_AVAILABLE;
 }
 if(!transmitter){
  struct lp_rx *r=&role.rx;struct r2_capture at;uint64_t ns;
  v.kind=RC8_OWN_GM;v.link_samples=r->fit_count;v.link_rms_ns=r->rms_ns;v.time_valid=r->have_pair;v.root_tag=(uint16_t)r->scope.root;v.emitter_tag=(uint16_t)r->scope.emitter;
  v.age_ms=v.rx_age_ms=r->last_us&&now>=r->last_us?(uint32_t)((now-r->last_us)/1000):UINT32_MAX;
  v.rssi_half_dbm=owner.rssi;v.snr_quarter_db=owner.snr;v.rssi_valid=r->have_pair;
  v.assurance=RC8_FRESHNESS_UNKNOWN; /* No independent source-age bound installed. */
  if(v.active&&v.requested&&!(atomic_get(&flags)&NORMAL)&&r2_capture_now(&at)&&lp_rx_estimate(r,at.tick,at.epoch,now,&ns)){
   v.utc_us=ns/1000;v.until_us=v.display_until_us=r->physical_deadline_us;v.choice=RC8_SELECT_LORA;v.state=RC8_AVAILABLE;
   /* Unknown freshness cannot issue a TZ authority lease. */
  }
 }
 k_spinlock_key_t key=k_spin_lock(&lock);
 struct lp_diagnostics *d=&diagnostics;
 if(transmitter){d->emitter=factory;d->boot=physical_boot;d->stream=role.tx.stream;d->source_epoch=role.tx.context_epoch;d->seed=role.tx.core.durable.seed;d->cursor=role.tx.core.durable.cursor;d->serial=role.tx.core.durable.serial;
  d->pending=role.tx.core.durable.pending;d->fh_disabled=role.tx.core.durable.fh_disabled;d->next_slot=role.tx.core.durable.next_slot_us;
  d->charged=charged;d->tx_root_boot=role.tx.context_root_boot;d->store_sequence=role.tx.store.sequence;
 }else{struct lp_rx *r=&role.rx;d->emitter=r->scope.emitter;d->boot=r->scope.boot;d->stream=r->scope.stream;d->source_epoch=r->scope.epoch;d->seed=r->scope.seed;d->last_tick=r->last_tick;d->last_ns=r->last_tx_ns;d->epoch=r->last_epoch;
  d->last_us=r->last_us;d->physical_until=r->physical_deadline_us;d->meta_until=r->observed_deadline_us;d->root_boot=r->scope.root_boot;
  d->cursor=r->highest;d->serial=r->serial;d->accepted=r->accepted;d->rejected=r->rejected;d->missing=r->missing;
  d->pending=r->pending;d->trained=r->trained;d->rate=r->rate_ppb;}
 d->refused=owner.refused;d->late=owner.late;d->missing_map=owner.missing_map;d->bad=owner.bad;
 d->storage_fail=owner.storage_fail;if(transmitter)d->storage_max_us=owner.storage_max_us;d->error=owner.error;d->stage=owner.stage;d->phy=owner.phy;d->channel=owner.channel;
 memcpy(d->sent,owner.sent,sizeof d->sent);memcpy(d->received,owner.received,sizeof d->received);
 published=v;k_spin_unlock(&lock,key);
}
void r2_lora_view_named(struct r2_lora_view *v,uint64_t *f,char name[25]){
 if(!v)return;
 k_spinlock_key_t key=k_spin_lock(&lock);*v=published;if(f)*f=transmitter?factory:role.rx.scope.emitter;if(name)name[0]=0;k_spin_unlock(&lock,key);
 uint64_t now=mono();if(now>=v->until_us){v->utc_us=0;v->choice=RC8_SELECT_NONE;v->time_valid=false;}
 else if(v->utc_us)v->utc_us+=now-v->sampled_us;
 v->sampled_us=now;
}
void r2_lora_view(struct r2_lora_view *v){r2_lora_view_named(v,NULL,NULL);}
void __attribute__((noinline)) r2_lora_shadow_publish(void){
 struct rc_shadow_map m={0};struct r2_capture at;uint64_t now,until=0;
 if(transmitter||!r2_shadow_enabled()||!r2_capture_now_at(&at,&now))goto withdraw;
 k_spinlock_key_t key=k_spin_lock(&lock);
 const struct lp_diagnostics *d=&diagnostics;
 bool valid=published.active&&published.requested&&published.choice==RC8_SELECT_LORA&&!published.fault&&d->trained&&d->epoch==at.epoch&&now<d->physical_until&&d->last_ns<=INT64_MAX;
 if(valid){m=(struct rc_shadow_map){.normal=true,.lora=true,.normal_map={.delivery=d->last_tick,.anchor=(int64_t)d->last_ns,.rate=d->rate,.valid=true},
  .map={.effective=d->last_tick,.generation=d->accepted},.lo=at.tick,.epoch=at.epoch,.parent=d->emitter,.parent_boot=d->boot,
  .root=d->emitter,.root_boot=d->root_boot,.root_epoch=d->source_epoch,.accepted=d->accepted,.issued=d->last_ns};until=d->physical_until;}
 k_spin_unlock(&lock,key);
 if(!valid||until<=now)goto withdraw;
 m.normal_until=at.tick+(until-now)*16;if(m.normal_until>m.map.effective+LP_HOLD_US*16)m.normal_until=m.map.effective+LP_HOLD_US*16;
 if(shadow_tick==m.map.effective)return;
 r2_shadow_normal_map(&m);shadow_tick=m.map.effective;return;
withdraw:if(shadow_tick){r2_shadow_lora_withdraw();shadow_tick=0;}
}
/* Synchronous precharge can refuse; it must never erase on the launch path. */
static bool storage_save(void *arg,const struct lp_checkpoint *s){
 uint64_t began=mono();bool ok=lp_store_save(arg,s);uint64_t cost=mono()-began;
 if(cost>owner.storage_max_us)owner.storage_max_us=cost>UINT32_MAX?UINT32_MAX:(uint32_t)cost;
 return ok;
}
static bool restore(uint64_t first_slot){
 if(!role.tx.loaded){role.tx.loaded=true;
  if(!lp_store_load(&role.tx.store,factory,&role.tx.core.durable)){inc(&owner.storage_fail);return false;}
  if(role.tx.store.sequence>1)owner.quiet_until_us=(uint64_t)owner.boot_ms*1000+REBOOT_CONTROL_GUARD_US;}
 if(!role.tx.store.ready)return false;
 if(!role.tx.rebased){
  if(mono()<owner.quiet_until_us)return false;
  /* Default-off boot plus a full4s control guard prevents a restart burst.
   * Unknown prior clock earns NO aging: retain every bucket's debt for a
   * full new hour, with a20s channel cooldown measured from this boot. */
  struct lp_checkpoint *s=&role.tx.core.durable;
  if(!lp_checkpoint_carry(s,mono(),first_slot,owner.boot_ms)||!lp_store_prepare(&role.tx.store)||
     !storage_save(&role.tx.store,s)||!lp_tx_restore(&role.tx.core,s,storage_save,&role.tx.store,true)){inc(&owner.storage_fail);return false;}
  role.tx.rebased=true;
 }
 return role.tx.core.ready;
}
static int send_packet(struct lp_packet *p,uint64_t finish,struct r2_capture limit){
 uint8_t wire[LP_META_BYTES];int rc=lp_encode(wire,sizeof wire,p,role.tx.context,group);if(rc)return rc;
 k_spinlock_key_t key=k_spin_lock(&lock);role.tx.captured=(struct rc8_correction){0};role.tx.edge=(struct r2_lora_hw_edge){0};owner.tx_kind=p->kind;k_spin_unlock(&lock,key);
 rc=r2_lora_hw_tx_before_source(wire,lp_size(p->kind),generation,finish,limit.tick,limit.epoch);
 if(rc)owner.tx_kind=0;
 return rc;
}
static int __attribute__((noinline)) transmit(uint64_t now){
 struct lp_tx *t=&role.tx.core;struct rc8_correction c;uint32_t source_epoch=0;uint64_t root_boot=0;struct r2_capture limit;
 uint32_t revision=(uint32_t)atomic_get(&source_revision);
 if(revision!=owner.revision){
  if(t->durable.pending){if(owner.stage==3&&!owner.tx_kind)lp_tx_cancel(t,true);else lp_tx_fault(t);owner.stage=0;}
  role.tx.context_ready=false;owner.revision=revision;
 }
 if(owner.tx_kind)return 0;
 if(!source_now(&c,&source_epoch,&root_boot,&limit)){
  if(t->durable.pending){if(owner.stage==3&&!owner.tx_kind)lp_tx_cancel(t,true);else lp_tx_fault(t);owner.stage=0;}
  return 0;
 }
 /* Choose the initial fixed monotonic phase200ms after the own source's
  * four-second boundary. The1.143s META then fits its frozen1.5s PPS horizon.
  * Never slew/rephase an admitted stream; service restarts retain next_slot.
  * Boot rebasing preserves the full previous debt for a new hour. */
 uint64_t delay_ns=(UINT64_C(4200000000)-c.event_ns%UINT64_C(4000000000))%UINT64_C(4000000000);
 if(delay_ns<UINT64_C(300000000))delay_ns+=UINT64_C(4000000000);
 if(!restore(now+(delay_ns+999)/1000))return 0;
 if(owner.stage==1){
  uint64_t at=role.tx.reservation.slot_us+2000000;
  if(now<at)return 0;
  if(now>at+45000){inc(&owner.late);lp_tx_fault(t);owner.stage=0;return 0;}
  int rc=tune(R2_LORA_FHSS,lp_channel(t->order,role.tx.reservation.cursor),false);if(rc)return rc;
  struct lp_packet p={.kind=LP_SYNC,.schedule=LP_FAST,.cursor=role.tx.reservation.cursor};
  rc=send_packet(&p,at+50000+lp_duration(LP_SYNC),limit);if(rc)lp_tx_fault(t);return rc;
 }
 if(owner.stage==2){
  uint64_t at=owner.last_sync_us+260000;
  if(now<at)return 0;
  if(now>owner.last_sync_us+295000){inc(&owner.late);lp_tx_fault(t);owner.stage=0;return 0;}
  int rc=tune(R2_LORA_FHSS,lp_channel(t->order,role.tx.reservation.cursor+1),false);if(rc)return rc;
  struct lp_packet p={.kind=LP_FOLLOW,.schedule=LP_FAST,.cursor=role.tx.reservation.cursor,.utc_ns=role.tx.sync_ns};
  rc=send_packet(&p,owner.last_sync_us+300000+lp_duration(LP_FOLLOW),limit);if(rc)lp_tx_fault(t);return rc;
 }
 /* A fresh context may skip DISC slots while awaiting META. Select that
  * future slot BEFORE applying the precharge lead; a stale next_slot must
  * not defer its durable reservation until the RF deadline itself. */
 uint64_t target=t->durable.next_slot_us,skipped=0;
 if(now>target&&now-target>45000)skipped=(now-target+LP_BEAT_US-1)/LP_BEAT_US;
 uint64_t index=(uint64_t)t->durable.slot+skipped;
 if(!role.tx.context_ready&&index%3)skipped+=3-index%3;
 target+=skipped*LP_BEAT_US;
 if(owner.stage!=3&&now+PRECHARGE_LEAD_US<target){
  if(now>owner.last_end_us+100000&&target-now>PRECHARGE_LEAD_US+100000&&!lp_store_prepare(&role.tx.store)){inc(&owner.storage_fail);return -EIO;}
  return 0;
 }
 if(owner.stage!=3){
  uint64_t at=mono();owner.reserve_lead_us=target>at?(uint32_t)(target-at):0;
  if(!lp_tx_reserve(t,target,&role.tx.reservation)){inc(&owner.refused);return 0;}
  owner.stage=3;
 }
 struct lp_reservation *r=&role.tx.reservation;
 owner.launch_late_us=(int32_t)((int64_t)mono()-(int64_t)r->slot_us);
 if(mono()<r->slot_us)return 0;
 if(mono()>r->slot_us+45000){inc(&owner.late);lp_tx_cancel(t,true);owner.stage=0;return 0;}
 /* Precharge may yield through a physical PPS/map update. Refresh the source
  * snapshot after storage, before the guarded launch; never renew a staged TX. */
 if(fenced()||revision!=(uint32_t)atomic_get(&source_revision))return -ECANCELED;
 if(!source_now(&c,&source_epoch,&root_boot,&limit)){lp_tx_cancel(t,true);owner.stage=0;return 0;}
 owner.source_phase_us=(uint32_t)((c.event_ns%UINT64_C(1000000000))/1000);
 if(r->control==LP_META&&!role.tx.context_ready){
  if(role.tx.stream==UINT32_MAX)return -EOVERFLOW;
  role.tx.context_epoch=source_epoch;role.tx.context_root_boot=root_boot;++role.tx.stream;
  struct lp_scope scope={.emitter=factory,.boot=physical_boot,.root=factory,.root_boot=root_boot,.epoch=source_epoch,.stream=role.tx.stream,.seed=t->durable.seed};
  if(lp_context(role.tx.context,&scope,LP_FAST,group))return -EINVAL;
 }
 struct lp_packet p={.kind=r->control,.schedule=LP_FAST,.scope={.emitter=factory,.boot=physical_boot,.root=factory,.root_boot=role.tx.context_root_boot,.epoch=role.tx.context_epoch,.stream=role.tx.stream,.seed=t->durable.seed},.cursor=r->cursor,.serial=r->serial,.utc_ns=c.event_ns,
  .has_pair=r->has_pair,.time_valid=true,.tz=r2_tz_unknown()};
 k_spinlock_key_t key=k_spin_lock(&lock);if(now<source_tz.until_us)p.tz=source_tz.value;k_spin_unlock(&lock,key);
 if(r->has_pair)p.delay_10ms=(uint16_t)((2000000-(lp_duration(p.kind)-10000)+9999)/10000);
 int rc=tune(R2_LORA_DTS,0,false);if(rc)return rc;
 struct r2_capture launch;owner.launch_late_us=(int32_t)((int64_t)mono()-(int64_t)r->slot_us);
 owner.source_room_us=r2_capture_now(&launch)&&launch.epoch==limit.epoch&&launch.tick<limit.tick?(uint32_t)((limit.tick-launch.tick)/16):0;
 rc=send_packet(&p,r->slot_us+50000+lp_duration(p.kind),limit);
 if(rc)lp_tx_cancel(t,rc==-EAGAIN);
 return rc;
}
static int __attribute__((noinline)) receive_event(const struct r2_lora_hw_event *h,uint64_t event,uint64_t now){
 struct lp_rx *r=&role.rx;struct lp_packet p;
 if(lp_decode(&p,h->frame,h->length,r->context,group)){inc(&owner.bad);return 0;}
 k_spinlock_key_t audit_key=k_spin_lock(&lock);
 audits[p.kind-1]=(struct lp_audit){.tick=h->tick,.epoch=h->capture_epoch,.start_us=event,.end_us=now,.cursor=p.cursor,.phy=owner.phy,.channel=owner.channel,.valid=true};
 k_spin_unlock(&lock,audit_key);
 inc(&owner.received[p.kind-1]);owner.rssi=h->rssi_half_dbm;owner.snr=h->snr_quarter_db;
 if(owner.phy==R2_LORA_DTS){
  if(p.kind!=LP_META&&p.kind!=LP_DISC)return 0;
  uint32_t duration=lp_duration(p.kind)-10000;if(event<duration)return 0;
  uint64_t start=event-duration;
  if(p.kind==LP_META){
   if(p.scope.emitter!=transmitter_factory||!lp_rx_meta(r,&p,group,NULL,event,now)){inc(&owner.bad);return 0;}
  }
  if(p.kind==LP_META)r->observed_deadline_us=start+LP_LEASE_US;
  /* The processing bound uses captured RxDone. The observational lease is
   * shortened back to the original RF-start interval; no authority lease. */
  if(!lp_rx_hint(r,&p,event,now))inc(&owner.bad);
  owner.next_control_us=start+LP_BEAT_US;
 }else if(p.kind==LP_SYNC){
  if(lp_rx_sync(r,p.cursor,h->tick,h->capture_epoch,event,now)){
   owner.listening=false;return tune(R2_LORA_FHSS,lp_channel(r->order,p.cursor+1),true);
  }
 }else if(p.kind==LP_FOLLOW){
  (void)lp_rx_follow(r,&p,NULL,h->capture_epoch,now);owner.listening=false;return tune(R2_LORA_DTS,0,true);
 }
 return 0;
}
static int __attribute__((noinline)) tx_complete(const struct r2_lora_hw_event *h,uint64_t event){
 unsigned kind=owner.tx_kind;if(!kind)return -ESTALE;
 bool mapped=false;k_spinlock_key_t key=k_spin_lock(&lock);
 if(kind==LP_SYNC){
  if(role.tx.captured.sequence&&role.tx.edge.tick==h->tick&&role.tx.edge.capture_epoch==h->capture_epoch){role.tx.sync_ns=role.tx.captured.event_ns;mapped=true;}
  else for(unsigned i=0;i<2;i++){struct rc8_correction c;
   if(rc8_export_event(&c,&role.tx.maps[i],1,(struct rc8_event){.tick=h->tick,.capture_epoch=h->capture_epoch,.kind=RC8_EVENT_TX_START})){role.tx.sync_ns=c.event_ns;mapped=true;break;}}
 }
 owner.tx_kind=0;
 audits[kind-1]=(struct lp_audit){.tick=h->tick,.epoch=h->capture_epoch,.start_us=event,.end_us=h->end_local_us,
 .cursor=role.tx.reservation.cursor+(kind==LP_FOLLOW?1:0),.phy=owner.phy,.channel=owner.channel,.valid=true};
 k_spin_unlock(&lock,key);
 inc(&owner.sent[kind-1]);owner.last_end_us=h->end_local_us;
 if(kind==LP_META||kind==LP_DISC){if(kind==LP_META)role.tx.context_ready=true;owner.stage=role.tx.reservation.has_pair?1:0;return 0;}
 bool confirmed=kind==LP_SYNC?lp_tx_capture_sync(&role.tx.core,h->end_local_us):lp_tx_confirm(&role.tx.core,kind,h->end_local_us);
 if(!confirmed){lp_tx_fault(&role.tx.core);owner.stage=0;return 0;}
 if(kind==LP_SYNC){
  if(!mapped){inc(&owner.missing_map);lp_tx_fault(&role.tx.core);owner.stage=0;return 0;}
  owner.last_sync_us=h->end_local_us;owner.stage=2;
 }else owner.stage=0;
 return 0;
}

static int __attribute__((noinline)) process_event(uint64_t now){
 struct r2_lora_hw_event h;int rc=r2_lora_hw_take(&h);if(rc<=0)return rc;
 uint64_t event;if(h.generation!=generation||!physical(&h,&event)){inc(&owner.bad);if(transmitter&&role.tx.core.durable.pending)lp_tx_fault(&role.tx.core);return 0;}
 if(h.kind==R2_LORA_HW_RX_DONE){owner.listening=false;rc=receive_event(&h,event,mono());
  if(!rc&&!owner.listening){rc=r2_lora_hw_rx_rearm();if(!rc)owner.listening=true;}return rc;}
 return tx_complete(&h,event);
}

static int receive_step(uint64_t now){
 struct lp_rx *r=&role.rx;lp_rx_expire(r,now);
 /* Return to DTS at EVERY predicted control, even while the fit is healthy. */
 if(owner.next_control_us&&now+150000>=owner.next_control_us&&owner.phy!=R2_LORA_DTS){
  int rc=tune(R2_LORA_DTS,0,true);if(rc)return rc;
 }
 if(owner.next_control_us&&now>owner.next_control_us+1300000)owner.next_control_us+=LP_BEAT_US;
 if(r->hint&&!r->pending&&now+150000>=r->hint_us&&now<=r->hint_us+200000&&owner.phy==R2_LORA_DTS){
  /* Consume the one saved prediction when control was missed. */
  if(r->prediction)r->prediction=false;
  return tune(R2_LORA_FHSS,lp_channel(r->order,r->hint_cursor),true);
 }
 if(owner.phy==R2_LORA_FHSS&&!r->pending&&!r->hint)return tune(R2_LORA_DTS,0,true);
 return 0;
}
static bool fenced(void){k_spinlock_key_t key=k_spin_lock(&lock);bool value=stop_deadline!=0;k_spin_unlock(&lock,key);return value;}
static void __attribute__((noinline)) service_step(void){
 uint64_t now=mono();atomic_val_t f=atomic_get(&flags);
 k_spinlock_key_t key=k_spin_lock(&lock);uint64_t until=lab_until;k_spin_unlock(&lock,key);
 if((f&REQUESTED)&&now>=until){r2_lora_request(false);f=atomic_get(&flags);}
 bool desired=!atomic_get(&faulted)&&(f&ALLOWED)&&(f&REQUESTED)&&group&&factory&&transmitter_factory&&(!transmitter||(!(f&PROFILE_RC)&&(f&SOURCE_PRESENT)));
 r2_diag_scratch.lora.beat=k_uptime_get_32();r2_diag_scratch.lora.monitor=desired||owner.active||owner.stopping;
 if(((!desired&&owner.active)||fenced())&&!owner.stopping)begin_stop(0);
 if(owner.stopping){
  if(!r2_diag_scratch.lora.drain_poll_us)r2_diag_scratch.lora.drain_poll_us=mono();
  struct r2_lora_hw_ack ack={0};r2_lora_hw_stop_poll(&ack);uint64_t observed=mono();
  bool complete=ack.dma_retired&&ack.radio_quiescent&&ack.fem_off&&ack.capture_released;
  if(ack.fault)atomic_or(&faulted,1);
  if(observed>=stop_deadline&&!complete){atomic_or(&faulted,2);r2_lora_hw_watchdog_trip(-ETIMEDOUT);}
  if(complete){r2_diag_scratch.lora.drain_ack_us=observed;
   if(observed>=stop_deadline){r2_diag_scratch.lora.drain_late=1;inc(&r2_diag_scratch.lora.drain_late_count);}
   bool no_rf=owner.stage==3&&!owner.tx_kind;
   owner.active=owner.stopping=owner.listening=false;owner.tx_kind=owner.stage=0;atomic_set(&closed,1);
   key=k_spin_lock(&lock);stop_deadline=0;k_spin_unlock(&lock,key);k_thread_priority_set(&worker_thread,WORKER_PRIORITY);
   /* Durable pending state already fails closed after reset. Record the
    * fault only AFTER observing peripheral closure, outside the200ms drain. */
   if(transmitter&&role.tx.core.durable.pending){if(no_rf)lp_tx_cancel(&role.tx.core,true);else lp_tx_fault(&role.tx.core);}}
 }else if(desired&&!owner.active){
  if(generation==UINT32_MAX)atomic_or(&faulted,1);
  else{++generation;atomic_set(&closed,0);int rc=r2_lora_hw_start(generation,false);
   if(rc==-EAGAIN)atomic_set(&closed,1);
   else{owner.active=true;if(!rc){r2_diag_scratch.lora.drain_late=0;rc=tune(R2_LORA_DTS,0,!transmitter);}
    /* Source withdrawal may preempt startup's reset/TCXO/SPI waits just as
     * it can preempt an active transaction. Drain the retained fence without
     * promoting that deliberate inhibition to a permanent hardware fault. */
    if(rc)begin_stop(rc==-ECANCELED&&fenced()?0:rc);}}
 }else if(owner.active){
  int rc=process_event(now);
  if(!rc){if(fenced()||(transmitter&&!(atomic_get(&flags)&SOURCE_PRESENT)))begin_stop(0);
   else rc=transmitter?transmit(mono()):receive_step(mono());}
  if(rc==-EAGAIN){inc(&owner.refused);if(transmitter){owner.stage=0;lp_tx_cancel(&role.tx.core,true);}}
  else if(rc)begin_stop(rc==-EINTR||(rc==-ECANCELED&&fenced())?0:rc);
 }
 report(mono());r2_diag_scratch.lora.beat=k_uptime_get_32();
}
static void worker(void *a,void *b,void *c){(void)a;(void)b;(void)c;for(;;){service_step();k_sleep(K_MSEC(2));}}
void r2_lora_init(uint64_t f,uint64_t boot,uint64_t tx,const struct r2_group_config *g){
 atomic_set(&observer_ready,0);memset(&r2_diag_scratch.lora,0,r2_diag_state()>=1?offsetof(struct r2_lora_watchdog_state,retained):sizeof r2_diag_scratch.lora);
 memset((uint8_t *)&r2_diag_scratch.lora+offsetof(struct r2_lora_watchdog_state,retained)+sizeof r2_diag_scratch.lora.retained,0,
  sizeof r2_diag_scratch.lora-(offsetof(struct r2_lora_watchdog_state,retained)+sizeof r2_diag_scratch.lora.retained));
 r2_diag_scratch.lora.journal_result=-ENODATA;factory=f;physical_boot=boot;transmitter_factory=tx;group=g;transmitter=f&&f==tx;
 source_tz.value=r2_tz_unknown();owner.boot_ms=(uint32_t)((mono()+999)/1000);if(!owner.boot_ms)owner.boot_ms=1;
 owner.quiet_until_us=mono()+LP_HOUR_US;
 if(transmitter){role.tx.loaded=true;
  if(!lp_store_load(&role.tx.store,factory,&role.tx.core.durable))inc(&owner.storage_fail);
  else if(role.tx.store.sequence>1)owner.quiet_until_us=(uint64_t)owner.boot_ms*1000+REBOOT_CONTROL_GUARD_US;
 }
 atomic_set(&flags,0);atomic_set(&observer_ready,1);
 k_thread_create(&worker_thread,worker_stack,K_THREAD_STACK_SIZEOF(worker_stack),worker,NULL,NULL,NULL,WORKER_PRIORITY,0,K_NO_WAIT);
 k_thread_name_set(&worker_thread,"lora-passive-lab");
}

void r2_lora_watchdog_check(void){
 if(!atomic_get(&observer_ready))return;
 /* Called by the independent priority4 observer. RF silence/source absence
  * is never a worker fault. A frozen owner cannot be hidden by live USB/UI.
  * Timestamp is read before comparison time, including wrap/preemption. */
 uint32_t beat=r2_diag_scratch.lora.beat,now=k_uptime_get_32();
 if(r2_diag_scratch.lora.monitor&&!r2_diag_scratch.lora.terminal&&
    (uint32_t)(now-beat)>=R2_LORA_WORKER_MS)r2_lora_hw_watchdog_trip(-ETIMEDOUT);
}
void r2_lora_watchdog_poll(void){
 struct r2_lora_watchdog_state *w=&r2_diag_scratch.lora;
 if(atomic_get(&faulted)&&!w->terminal)r2_lora_hw_watchdog_trip(-EIO);
 if(w->terminal&&!atomic_get(&faulted)){
  /* Includes an out-of-band stall: fence timing/normal source immediately;
   * worker may remain blocked. Never claim closure or reuse its buffers. */
  atomic_set(&faulted,1);r2_lora_hw_stop_begin(mono()+RC8_DRAIN_US);
 }
 if(w->journal_generation&&w->journal_result==-EINPROGRESS){
  int rc=r2_diag_record_result(w->journal_generation);
  if(rc!=-EINPROGRESS){w->journal_result=rc;if(!rc)w->saved=w->submitted;}
 }
 uint32_t elapsed=(uint32_t)(k_uptime_get_32()-w->fault_at);
 bool closure_ready=!w->terminal||(w->closure&15u)==15u||elapsed>=RC8_DRAIN_US/1000u;
 if(closure_ready&&w->serial!=w->saved&&w->serial!=w->submitted&&!r2_diag_busy()){
  uint32_t record[16];unsigned key=irq_lock();
  memcpy(record,w->record,sizeof record);record[5]|=(w->closure&31u)<<20;
  uint32_t serial=w->serial;bool terminal=w->terminal!=0;
  irq_unlock(key);
  uint64_t token=0;int rc=r2_diag_lora(record,terminal,&token);
  if(!rc){w->submitted=serial;w->journal_generation=token;w->journal_result=-EINPROGRESS;}
  else if(rc!=-EAGAIN)w->journal_result=rc;
 }
 /* Never wait indefinitely for flash admission. The durable commit result
  * (including failure/missing data), first cause and actual closure bits are
  * also bound into the existing watchdog reset capsule. The30s peripheral
  * reset retires unsafe DMA; no local driver restart clears a real fault. */
 uint32_t now=k_uptime_get_32();
 if(w->terminal&&!w->reset_requested&&
    (w->saved==w->serial||(uint32_t)(now-w->fault_at)>=2000u)){
  w->reset_requested=1;
  r2_watchdog_lora((int32_t)w->record[1],w->record[3],w->record[6],w->closure,w->journal_result);
 }
}
static uint32_t stack_unused(void){
#ifdef CONFIG_INIT_STACKS
 size_t unused;
 if(!k_thread_stack_space_get(&worker_thread,&unused)&&unused<=UINT32_MAX)return (uint32_t)unused;
#endif
 return UINT32_MAX;
}

int r2_lora_json(char *out,size_t capacity,int result){
 struct lp_diagnostics d;struct r2_lora_view v;struct lp_audit audit[4];uint64_t until;
 k_spinlock_key_t key=k_spin_lock(&lock);d=diagnostics;v=published;until=lab_until;memcpy(audit,audits,sizeof audit);k_spin_unlock(&lock,key);
 struct r2_lora_hw_diag h;r2_lora_hw_diag(&h);
 return snprintf(out,capacity,
 "{\"passive\":2,\"profile\":\"LAB_FAST_4_12\",\"cap_us\":1080000000,\"result\":%d,"
 "\"boot\":\"%016llx\",\"configured_tx\":\"%016llx\",\"transmitter\":%s,\"requested\":%s,\"active\":%s,\"closed\":%s,\"fault\":%s,"
 "\"lab_until_us\":%llu,\"quiet_until_us\":%llu,\"stack_unused\":%u,\"error\":%d,"
 "\"emitter\":\"%016llx\",\"emitter_boot\":\"%016llx\",\"stream\":%u,\"source_epoch\":%u,\"root_boot\":\"%016llx\",\"seed\":%u,\"cursor\":%u,\"serial\":%u,"
 "\"event_tick\":%llu,\"event_ns\":%llu,\"capture_epoch\":%llu,\"event_mono_us\":%llu,\"physical_until_us\":%llu,\"meta_until_us\":%llu,"
 "\"trained\":%s,\"link_fit\":[%u,%u],\"rate_ppb\":%d,\"accepted\":%u,\"rejected\":%u,\"missing\":%u,"
 "\"choice\":%u,\"state\":%u,\"time_valid\":%s,\"assurance\":\"FRESHNESS_UNKNOWN\",\"rc7_authority\":false,\"utc_accuracy\":\"UNQUALIFIED\","
 "\"charged_us\":%llu,\"next_slot_us\":%llu,\"pending\":%u,\"fh_disabled\":%u,\"store_sequence\":%u,\"storage_fail\":%u,\"storage_max_us\":%u,"
 "\"refused\":%u,\"late\":%u,\"missing_map\":%u,\"bad\":%u,\"sent\":[%u,%u,%u,%u],\"received\":[%u,%u,%u,%u],"
 "\"at_us\":%llu,\"tx_admission_us\":[%u,%d,%u,%u],"
 "\"audit\":[[%llu,%llu,%llu,%llu,%u,%u,%u,%u],[%llu,%llu,%llu,%llu,%u,%u,%u,%u],[%llu,%llu,%llu,%llu,%u,%u,%u,%u],[%llu,%llu,%llu,%llu,%u,%u,%u,%u]],"
 "\"owner\":[%u,%u,%u],\"hw\":[%u,%u,%u,%u,%u,%u,%u,%d,%u,%u,%u],\"drain_result\":%d}",
 result,(unsigned long long)physical_boot,(unsigned long long)transmitter_factory,transmitter?"true":"false",v.requested?"true":"false",v.active?"true":"false",atomic_get(&closed)?"true":"false",atomic_get(&faulted)?"true":"false",
 (unsigned long long)until,(unsigned long long)owner.quiet_until_us,stack_unused(),d.error,
 (unsigned long long)d.emitter,(unsigned long long)d.boot,d.stream,d.source_epoch,(unsigned long long)(transmitter?d.tx_root_boot:d.root_boot),d.seed,d.cursor,d.serial,
 (unsigned long long)(transmitter?0:d.last_tick),(unsigned long long)(transmitter?0:d.last_ns),(unsigned long long)(transmitter?0:d.epoch),(unsigned long long)(transmitter?0:d.last_us),(unsigned long long)(transmitter?0:d.physical_until),(unsigned long long)(transmitter?0:d.meta_until),
 d.trained?"true":"false",v.link_samples,v.link_rms_ns,transmitter?0:d.rate,d.accepted,d.rejected,d.missing,v.choice,v.state,v.time_valid?"true":"false",
 (unsigned long long)(transmitter?d.charged:0),(unsigned long long)(transmitter?d.next_slot:0),d.pending,d.fh_disabled,d.store_sequence,d.storage_fail,transmitter?d.storage_max_us:0,d.refused,d.late,d.missing_map,d.bad,
 d.sent[0],d.sent[1],d.sent[2],d.sent[3],transmitter?0:d.received[0],transmitter?0:d.received[1],transmitter?0:d.received[2],transmitter?0:d.received[3],
 (unsigned long long)mono(),transmitter?d.reserve_lead_us:0,transmitter?d.launch_late_us:0,transmitter?d.source_room_us:0,transmitter?d.source_phase_us:0,
#define AUDIT_ARGS(i) (unsigned long long)audit[i].tick,(unsigned long long)audit[i].epoch,(unsigned long long)audit[i].start_us,(unsigned long long)audit[i].end_us,audit[i].cursor,audit[i].phy,audit[i].channel,audit[i].valid
 AUDIT_ARGS(0),AUDIT_ARGS(1),AUDIT_ARGS(2),AUDIT_ARGS(3),
#undef AUDIT_ARGS
 d.stage,d.phy,d.channel,
 h.rx_done,h.tx_done,h.crc_bad,h.overflow,h.spi_error,h.cutoffs,h.invalid_capture,h.error,h.phase,h.launch_aborts,h.busy_aborts,r2_lora_close_result());
}

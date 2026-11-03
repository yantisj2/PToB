/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Passive receivers and one background owner.
 * PPS witnesses never feed this estimator. No normal-source authority is exported. */
#include "lora_service.h"
#include "lora_passive_core.h"
#include <zephyr/random/random.h>
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
#define DISPLAY_CLEAR 16u
#define SOURCE_PRESENT 32u
#define MESH_CONTACT 64u
#define WORKER_PRIORITY 11
/* Main/GNSS7, BLE precision8 and GM owner9 always run first. RF deadlines
 * never promote packet preparation above the clocks that supply its time. */
#define DRAIN_PRIORITY 10
#ifdef R2_LORA_PASSIVE_SLOW
/* Closure observation yields to GNSS/BLE/GM work. Inhibit RF immediately,
 * then retain one absolute deadline for the actual quiescence ACK. */
#define PASSIVE_DRAIN_US UINT64_C(2000000)
#else
#define PASSIVE_DRAIN_US RC8_DRAIN_US
#endif
#ifdef R2_LORA_TX_DIAGNOSTIC
#define LAB_RUN_US UINT64_C(36000000)
#define DIAGNOSTIC_CHARGE_US UINT32_C(12000000)
#define DIAGNOSTIC_CAPTURE_US UINT64_C(1500000)
#else
#define LAB_RUN_US UINT64_C(900000000)
#endif
#ifdef R2_LORA_TX_DIAGNOSTIC
#define DIAGNOSTIC_JSON "true"
#else
#define DIAGNOSTIC_JSON "false"
#endif
#define REBOOT_CONTROL_GUARD_US UINT64_C(4000000)
#ifdef R2_LORA_PASSIVE_SLOW
#define PRECHARGE_LEAD_US UINT64_C(2000000)
#define PAIR_PREPARE_LEAD_US UINT64_C(1500000)
#else
#define PRECHARGE_LEAD_US UINT64_C(20000)
#endif
#define ADMISSION_RETRY_US UINT64_C(200000)
#define UTC_PLAN_US (2*PRECHARGE_LEAD_US+LP_FRAME_END_US+UINT64_C(2000000))
/* Unwind event/packet locals before recording the current-boot RAM fault. */
#define EVENT_FH_FAULT 1
static atomic_t observer_ready,flags,closed=1,faulted,source_revision;
static struct k_spinlock lock;
static const struct r2_group_config *group;
static uint64_t factory,physical_boot,transmitter_factory,stop_deadline,lab_until;
static bool transmitter;
#ifdef R2_LORA_PASSIVE_SLOW
static uint64_t retry_at_us;
static uint32_t retry_count;
static uint64_t admission_retry(uint64_t now){
 if(retry_count<UINT32_MAX)++retry_count;
 /* Back off200..302ms before starting the full preparation lead. The RF
  * retry target must not spend that lead on encoding/SPI after its deadline. */
 return now+PRECHARGE_LEAD_US+ADMISSION_RETRY_US+(uint64_t)(retry_count%7)*17000;
}
#endif
static struct lp_replay replay[1];
static uint32_t generation;
static struct {
 struct {struct lp_tx core;struct rc8_export maps[2];
  /* maps[1] freezes the formula owning edge while a packet is outstanding.
   * Projection runs in the preemptible worker, never under this copy lock. */
  struct r2_lora_hw_edge edge;bool pinned;
  struct lp_scope scope;uint64_t context_root_boot;uint32_t context_epoch;struct lp_reservation reservation;
  uint8_t context[16];uint64_t sync_ns;uint32_t stream;bool context_ready,loaded;volatile bool dirty;} tx;
 struct lp_rx rx;
} role;
static uint32_t map_version;
/* READY, transient contention, actual absence, and rejected snapshot versions. */
static uint32_t source_reads[4];
/* Completed, deferred by publication, invalid original map, deadline. */
static uint32_t sync_map[4];
/* Latest/max scheduler call lateness per DISC,META,SYNC,FOLLOW, including
 * rejected attempts. Source PPS phase is deliberately a separate quantity. */
#ifdef R2_LORA_PASSIVE_SLOW
#define tx_due (r2_diag_scratch.lora.tx_due_us)
static void due_lateness(unsigned kind,uint64_t target,uint64_t now){
 if(kind<LP_DISC||kind>LP_FOLLOW)return;
 uint64_t delta=now>target?now-target:0;uint32_t us=delta>UINT32_MAX?UINT32_MAX:(uint32_t)delta;
 k_spinlock_key_t key=k_spin_lock(&lock);tx_due[0][kind-1]=us;
 if(tx_due[1][kind-1]<us)tx_due[1][kind-1]=us;
 k_spin_unlock(&lock,key);
}
#else
static void due_lateness(unsigned kind,uint64_t target,uint64_t now){(void)kind;(void)target;(void)now;}
#endif
static struct r2_tz_lease source_tz;
static struct r2_lora_view published;
static uint64_t shadow_tick;
struct lp_audit {uint64_t tick,epoch,start_us,end_us;uint32_t cursor;uint8_t phy,channel,valid;};
/* Last physical event of each kind. Each kind recurs no faster than4s, so a
 * bounded1Hz console collector can retain every event and its denominator. */
#ifdef R2_LORA_PASSIVE_SLOW
static struct lp_audit audits[1];static uint8_t audit_kind;
#define AUDIT_INDEX(kind) 0u
#else
static struct lp_audit audits[4];
#define AUDIT_INDEX(kind) ((kind)-1)
#endif
struct lp_diagnostics {
 uint64_t emitter,boot;uint32_t stream,source_epoch,seed;
 uint64_t last_tick,last_ns,epoch,last_us,physical_until,meta_until,root_boot;
 uint64_t next_slot,charged,tx_root_boot;
 uint32_t cursor,serial,accepted,rejected,missing,store_sequence,refused,late,missing_map,bad,storage_fail,sent[4];
 uint32_t received[4];uint32_t reserve_lead_us;int32_t launch_late_us;uint32_t source_room_us,source_phase_us;
 uint32_t max_source_room_us,max_source_phase_us,reject_phase_us,no_rf_refunds,fit_span_us;
 int32_t rate;uint32_t storage_max_us;int error;uint8_t pending,fh_disabled,stage,phy,channel;bool trained;
 int32_t baseline_rate,hold_short_rate,hold_long_rate;
 uint32_t hold_short_guard,hold_long_guard;bool hold_selected;
 uint64_t model_tick,model_ns;int32_t model_phase;
};
static struct lp_diagnostics diagnostics;
static struct {bool active,stopping,listening;uint8_t phy,channel,tx_kind,stage,prepared_kind;
 uint64_t next_control_us,last_end_us,last_sync_us,quiet_until_us;uint32_t boot_ms,storage_max_us;
 uint32_t sent[4];uint32_t received[4];uint32_t reserve_lead_us;int32_t launch_late_us;uint32_t source_room_us,source_phase_us;
 uint32_t max_source_room_us,max_source_phase_us,reject_phase_us,no_rf_refunds;
 uint32_t refused,late,missing_map,bad,storage_fail,revision;
 int error;int16_t rssi;int8_t snr;} owner;
static void inc(uint32_t *);
enum utc_state { UTC_IDLE,UTC_PENDING,UTC_RESERVED,UTC_COMPLETED,UTC_EXPIRED };
enum utc_reason { UTC_OK,UTC_ROLE,UTC_NO_SOURCE,UTC_BUSY,UTC_TOKEN,UTC_LIMIT,UTC_WAIT_SLOT,
 UTC_WAIT_SOURCE,UTC_DEADLINE,UTC_CONTEXT,UTC_OFF,UTC_FAILED,UTC_WAIT_BUDGET,UTC_WAIT_COOLDOWN };
struct utc_request {
 struct rc8_scope scope;
 uint64_t token,floor,due_us,deadline_us,capture_epoch;
 uint32_t revision,admitted,completed,refused,expired,deferrals,reserved_us,manual_serial,manual_stream;
 int last_result;uint8_t state,reason,wait_reason;bool override;
};
static struct utc_request utc_job;
static uint64_t utc_retry_us,utc_running_token;
static bool utc_active(unsigned state){return state==UTC_PENDING||state==UTC_RESERVED;}
static void utc_finish(unsigned state,unsigned reason){
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(utc_active(utc_job.state)){
  utc_job.state=(uint8_t)state;utc_job.reason=(uint8_t)reason;
  if(state==UTC_COMPLETED)inc(&utc_job.completed);else inc(&utc_job.expired);
 }
 utc_retry_us=0;utc_running_token=0;k_spin_unlock(&lock,key);
}
static uint64_t utc_due(void){
 k_spinlock_key_t key=k_spin_lock(&lock);uint64_t due=UINT64_MAX;
 if(utc_job.state==UTC_PENDING){due=utc_job.due_us;if(utc_retry_us>due)due=utc_retry_us;}
 k_spin_unlock(&lock,key);return due;
}

K_THREAD_STACK_DEFINE(worker_stack,2048);
static struct k_thread worker_thread;
static uint64_t mono(void){return k_ticks_to_us_floor64(k_uptime_ticks());}
static bool fenced(void);
static void inc(uint32_t *v){if(*v<UINT32_MAX)++*v;}
static void stop_now(void){
 k_thread_priority_set(&worker_thread,DRAIN_PRIORITY);
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(!stop_deadline){stop_deadline=mono()+PASSIVE_DRAIN_US;
  r2_diag_scratch.lora.drain_deadline_us=stop_deadline;r2_diag_scratch.lora.drain_promoted_us=mono();
  r2_diag_scratch.lora.drain_poll_us=r2_diag_scratch.lora.drain_ack_us=0;}
 uint64_t deadline=stop_deadline;k_spin_unlock(&lock,key);
 r2_lora_hw_stop_begin(deadline);k_wakeup(&worker_thread);
}
void r2_lora_name_publish(const char *name){(void)name;}
void r2_lora_tz_publish(struct r2_tz tz,uint64_t until,uint64_t root,uint64_t boot,uint32_t epoch){
 if(!transmitter)return;
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(root!=role.tx.maps[0].scope.root||boot!=role.tx.maps[0].scope.root_boot||epoch!=role.tx.maps[0].scope.source_epoch||mono()>=until||!r2_tz_valid(tz)){tz=r2_tz_unknown();until=0;}
 source_tz=(struct r2_tz_lease){.value=tz,.until_us=until};k_spin_unlock(&lock,key);
}
void r2_lora_rtt_lab(bool on){(void)on;} /* No RTT frames exist in this wire. */
void r2_lora_control(bool rc,bool allowed,enum rc8_selected preferred){
 atomic_val_t old=atomic_get(&flags),next;
 do{next=(old&(REQUESTED|SOURCE_PRESENT|MESH_CONTACT|DISPLAY_CLEAR))|(rc?PROFILE_RC:0)|(allowed?ALLOWED:0)|(preferred!=RC8_SELECT_NONE?NORMAL:0);
  if((old^next)&(PROFILE_RC|NORMAL|ALLOWED))next|=DISPLAY_CLEAR;
 }while(!atomic_cas(&flags,old,next)&&(old=atomic_get(&flags),true));
 if((!allowed||(transmitter&&rc))&&!atomic_get(&closed))stop_now();
}
void r2_lora_mesh_contact(bool present){if(present)atomic_or(&flags,MESH_CONTACT);else atomic_and(&flags,~MESH_CONTACT);}
void r2_lora_request(bool on){
 k_spinlock_key_t key=k_spin_lock(&lock);
#ifdef R2_LORA_TX_DIAGNOSTIC
 /* One explicit finite arm per boot; repeated ON cannot renew its budget. */
 if(on&&r2_diag_scratch.lora.diagnostic.used){k_spin_unlock(&lock,key);return;}
 if(on){r2_diag_scratch.lora.diagnostic.used=true;r2_diag_scratch.lora.diagnostic.armed_us=mono();}
#endif
#ifdef R2_LORA_PASSIVE_SLOW
 lab_until=on?UINT64_MAX:0;
 if(!on){published.utc_us=published.display_until_us=0;published.display_held=published.display_acquiring=false;}
#else
 lab_until=on?mono()+LAB_RUN_US:0;
#endif
 k_spin_unlock(&lock,key);
 if(on)atomic_or(&flags,REQUESTED);else{atomic_or(&flags,DISPLAY_CLEAR);atomic_and(&flags,~REQUESTED);if(!atomic_get(&closed))stop_now();}
 k_wakeup(&worker_thread);
}
void r2_lora_toggle(void){r2_lora_request(!(atomic_get(&flags)&REQUESTED));}
void r2_lora_stop(void){atomic_or(&flags,DISPLAY_CLEAR);atomic_and(&flags,~ALLOWED);if(!atomic_get(&closed))stop_now();}
int r2_lora_close_result(void){
 if((atomic_get(&faulted)&2)||r2_diag_scratch.lora.terminal)return -EIO;

 return r2_diag_scratch.lora.drain_late?-ETIMEDOUT:atomic_get(&closed)?0:-EAGAIN;
}
bool r2_lora_source_needed(void){
#ifdef R2_LORA_TX_DIAGNOSTIC
 return false;
#else
 atomic_val_t f=atomic_get(&flags);return
#ifdef R2_LORA_PASSIVE_SLOW
 transmitter
#else
 transmitter
#endif
 &&!atomic_get(&faulted)&&(f&ALLOWED)&&(f&REQUESTED)&&!(f&PROFILE_RC);
#endif
}
static bool same_formula(const struct rc8_export *a,const struct rc8_export *b){
 return rc8_scope_equal(&a->scope,&b->scope)&&a->capture_epoch==b->capture_epoch&&
  a->map_generation==b->map_generation&&a->anchor_tick==b->anchor_tick&&a->anchor_ns==b->anchor_ns&&
  a->rate_q16==b->rate_q16&&a->phase_ns==b->phase_ns&&a->rate_ppb==b->rate_ppb&&a->mode==b->mode;
}
/* Called under the source copy lock. Freeze a candidate formula using raw
 * capture bounds only; DONE must still project/check its original UTC expiry.
 * Before pinning, edge is the new packet's capture floor, not its timestamp. */
static void preserve(void){
 if(!owner.tx_kind||role.tx.pinned)return;
 struct r2_lora_hw_edge edge;if(!r2_lora_hw_tx_edge(&edge)||edge.generation!=generation||
  edge.capture_epoch!=role.tx.edge.capture_epoch||edge.tick<role.tx.edge.tick)return;
 for(unsigned i=0;i<2;i++){const struct rc8_export *m=&role.tx.maps[i];
  if(m->valid&&m->authenticated&&m->map_generation&&m->capture_epoch==edge.capture_epoch&&
     edge.tick>=m->from_tick&&edge.tick<m->until_tick){
   if(!i)role.tx.maps[1]=*m;
   role.tx.edge=edge;role.tx.pinned=true;break;
  }
 }
}
/* Projection is deliberately preemptible. Every publisher, shortening and
 * packet replacement changes map_version; no stale snapshot can commit. */
static bool __attribute__((noinline)) completed_source(const struct r2_lora_hw_event *h,uint64_t *ns,uint32_t *mapped_version,bool *raced){
 for(unsigned attempt=0;attempt<3;attempt++){
  k_spinlock_key_t key=k_spin_lock(&lock);
  bool present=(atomic_get(&flags)&SOURCE_PRESENT)&&!stop_deadline;
  bool pinned=role.tx.pinned;uint32_t version=map_version;
  bool matching=role.tx.edge.capture_epoch==h->capture_epoch&&role.tx.edge.generation==h->generation&&
   (pinned?role.tx.edge.tick==h->tick:h->tick>=role.tx.edge.tick);
  k_spin_unlock(&lock,key);
  if(!present||!matching){*raced=true;return false;}
  bool retry=false;
  for(unsigned i=0;i<(pinned?1u:2u);i++){
   struct rc8_export map;struct rc8_correction correction;
   key=k_spin_lock(&lock);map=role.tx.maps[pinned?1:i];
   bool stable=map_version==version&&owner.tx_kind==LP_SYNC&&generation==h->generation;
   k_spin_unlock(&lock,key);
   if(!stable){retry=true;break;}
   bool projected=rc8_export_event(&correction,&map,1,
    (struct rc8_event){.tick=h->tick,.capture_epoch=h->capture_epoch,.kind=RC8_EVENT_TX_START});
   key=k_spin_lock(&lock);
   stable=map_version==version&&owner.tx_kind==LP_SYNC&&generation==h->generation&&
    (atomic_get(&flags)&SOURCE_PRESENT)&&!stop_deadline;
   k_spin_unlock(&lock,key);
   if(!stable){retry=true;break;}
   if(projected){*ns=correction.event_ns;*mapped_version=version;return true;}
  }
  if(!retry)return false;
 }
 *raced=true;return false;
}
void r2_lora_source_publish(const struct rc8_export *input){
 if(!r2_lora_source_needed()||!input)return;
 struct rc8_export m=*input;m.scope.emitter=factory;m.scope.emitter_boot=physical_boot;m.scope.stream=1;
 if(!m.valid||!m.authenticated||m.scope.scale!=RC8_UTC||
 (m.scope.kind!=RC8_OWN_GM&&m.scope.kind!=RC8_SELECTED_BLE)||rc8_scope_validate(&m.scope)){r2_lora_source_withdraw();return;}
 k_spinlock_key_t key=k_spin_lock(&lock);
#ifdef R2_LORA_PASSIVE_SLOW
 if(!transmitter){k_spin_unlock(&lock,key);return;}
#endif
 ++map_version;
 preserve();
 struct rc8_export *last=&role.tx.maps[0];
 bool changed=last->valid&&(!rc8_scope_equal(&last->scope,&m.scope)||last->capture_epoch!=m.capture_epoch);
 if(changed)role.tx.pinned=false;
 if(last->valid&&last->scope.root==m.scope.root&&last->scope.root_boot==m.scope.root_boot&&m.scope.source_epoch<last->scope.source_epoch){
  k_spin_unlock(&lock,key);r2_lora_source_withdraw();return;
 }
 bool duplicate=last->valid&&!changed&&last->anchor_tick==m.anchor_tick&&last->anchor_ns==m.anchor_ns&&
  last->rate_q16==m.rate_q16&&last->phase_ns==m.phase_ns&&last->rate_ppb==m.rate_ppb&&last->mode==m.mode;
 if(duplicate){
  /* Repeated snapshots may shorten the original interval, never renew it. */
  bool shortened=m.until_tick<last->until_tick||m.expires_ns<last->expires_ns;
  if(m.until_tick<last->until_tick)last->until_tick=m.until_tick;
  if(m.expires_ns<last->expires_ns)last->expires_ns=m.expires_ns;
  if(role.tx.pinned&&same_formula(&role.tx.maps[1],last)){
   struct rc8_export *saved=&role.tx.maps[1];
   if(saved->until_tick>last->until_tick)saved->until_tick=last->until_tick;
   if(saved->expires_ns>last->expires_ns)saved->expires_ns=last->expires_ns;
   if(role.tx.edge.tick>=saved->until_tick)role.tx.pinned=false;
  }
  k_spin_unlock(&lock,key);
  /* A preempted SetTx may hold the earlier, wider bound. Fence it before
   * returning; the next operation must acquire the shortened grant. */
  if(shortened&&!atomic_get(&closed)){
#ifdef R2_LORA_PASSIVE_SLOW
   inc(&r2_diag_scratch.lora.startup_counts[1]);
#endif
   stop_now();
  }
  return;
 }
 if(changed||!(atomic_get(&flags)&SOURCE_PRESENT))atomic_inc(&source_revision);
 /* The existing formula owns every event before this publication edge. */
 struct r2_capture commit;
 if(!r2_capture_now(&commit)||commit.epoch!=m.capture_epoch||commit.tick<m.from_tick||commit.tick>=m.until_tick||
    (commit.tick-m.from_tick)/16>UINT32_MAX-m.map_age_us){
  k_spin_unlock(&lock,key);r2_lora_source_withdraw();return;
 }
 m.map_age_us+=(uint32_t)((commit.tick-m.from_tick)/16);m.from_tick=commit.tick;
 if(last->valid&&!changed){
  if(m.from_tick<=last->from_tick){k_spin_unlock(&lock,key);return;}
  if(last->until_tick>m.from_tick)last->until_tick=m.from_tick;
  if(role.tx.pinned&&same_formula(&role.tx.maps[1],last)){
   if(role.tx.maps[1].until_tick>last->until_tick)role.tx.maps[1].until_tick=last->until_tick;
   if(role.tx.edge.tick>=last->until_tick)role.tx.pinned=false;
  }
 }
 if(!role.tx.pinned)role.tx.maps[1]=*last;
 *last=m;
 atomic_or(&flags,SOURCE_PRESENT);k_spin_unlock(&lock,key);
 if(changed&&!atomic_get(&closed))stop_now();
}
void r2_lora_source_withdraw(void){
#ifdef R2_LORA_TX_DIAGNOSTIC
 return; /* This build has no timing source or authority to withdraw. */
#endif
 if(!transmitter)return;
 k_spinlock_key_t key=k_spin_lock(&lock);
#ifdef R2_LORA_PASSIVE_SLOW
 if(!transmitter){k_spin_unlock(&lock,key);return;}
#endif
 ++map_version;
 preserve();
 bool was_present=(atomic_and(&flags,~SOURCE_PRESENT)&SOURCE_PRESENT)!=0;
 if(was_present)atomic_inc(&source_revision);
 struct r2_capture now;bool valid=r2_capture_now(&now);
 for(unsigned i=0;i<2;i++)if(valid&&role.tx.maps[i].capture_epoch==now.epoch)(void)rc8_export_close(&role.tx.maps[i],now.tick,now.epoch);else role.tx.maps[i].valid=false;
 role.tx.pinned=false;
 k_spin_unlock(&lock,key);
 /* The high-priority source owner can fence an in-flight launch without
  * SPI or flash. The worker performs the original bounded drain. */
 if(was_present&&!atomic_get(&closed))stop_now();
}
void r2_lora_quality_publish(const struct rc8_scope *s,const struct rc8_quality *q){(void)s;(void)q;}
void r2_lora_parent_quality(uint64_t f,uint64_t b,uint8_t c,uint8_t a,uint64_t s){(void)f;(void)b;(void)c;(void)a;(void)s;}
void r2_lora_coordination(struct r2_gm_coordination *out){if(out)memset(out,0,sizeof *out);}
void r2_lora_origin(bool active,uint64_t boot,uint32_t epoch){(void)active;(void)boot;(void)epoch;}
#ifdef R2_LORA_PASSIVE_SLOW
bool r2_lora_transmitter_quiescent(void){return !transmitter||r2_lora_close_result()==0;}
void r2_lora_gm_admission(bool active,uint64_t until,bool takeover,uint32_t source_epoch,uint32_t capture_epoch){
 /* GM election does not select the independent designated LoRa bridge. */
 (void)active;(void)until;(void)takeover;(void)source_epoch;(void)capture_epoch;
}
#endif
enum source_result { SOURCE_ABSENT, SOURCE_READY, SOURCE_TRANSIENT };
static enum source_result __attribute__((noinline)) source_now(uint64_t *phase_ns,uint32_t *epoch,uint64_t *root_boot,struct r2_capture *limit){
#ifdef R2_LORA_TX_DIAGNOSTIC
 struct r2_capture at;if(!r2_capture_now(&at))return SOURCE_ABSENT;
 /* Local monotonic phase only. Never construct/export a UTC source map. */
 *phase_ns=mono()*1000;*epoch=0;*root_boot=0;
 *limit=(struct r2_capture){.tick=at.tick+DIAGNOSTIC_CAPTURE_US*16,.epoch=at.epoch,.kind=R2_CAPTURE_NOW};
 k_spinlock_key_t key=k_spin_lock(&lock);inc(&source_reads[0]);k_spin_unlock(&lock,key);return SOURCE_READY;
#else
 for(unsigned attempt=0;attempt<3;attempt++){
  struct rc8_export map;struct rc8_correction correction;struct r2_capture at;
  k_spinlock_key_t key=k_spin_lock(&lock);
  bool present=(atomic_get(&flags)&SOURCE_PRESENT)&&!stop_deadline;uint32_t version=map_version;map=role.tx.maps[0];
  if(!present){inc(&source_reads[2]);k_spin_unlock(&lock,key);return SOURCE_ABSENT;}
  k_spin_unlock(&lock,key);
  /* Capture AFTER copying the map: an older NOW cannot precede the freshly
   * published map's from_tick. Projection remains preemptible. */
  bool good=r2_capture_now(&at)&&rc8_export_event(&correction,&map,1,
   (struct rc8_event){.tick=at.tick,.capture_epoch=at.epoch,.kind=RC8_EVENT_TX_START});
  key=k_spin_lock(&lock);
  present=(atomic_get(&flags)&SOURCE_PRESENT)&&!stop_deadline;
  if(!present){inc(&source_reads[2]);k_spin_unlock(&lock,key);return SOURCE_ABSENT;}
  if(version!=map_version){inc(&source_reads[3]);k_spin_unlock(&lock,key);continue;}
  inc(&source_reads[good?0:2]);k_spin_unlock(&lock,key);
  if(!good)return SOURCE_ABSENT;
  *phase_ns=correction.event_ns;*epoch=map.scope.source_epoch;*root_boot=map.scope.root_boot;
  *limit=(struct r2_capture){.tick=map.until_tick,.epoch=map.capture_epoch,.kind=R2_CAPTURE_NOW};return SOURCE_READY;
 }
 k_spinlock_key_t key=k_spin_lock(&lock);inc(&source_reads[1]);k_spin_unlock(&lock,key);
 return SOURCE_TRANSIENT; /* Keep prepared work/debt for the next worker poll. */
#endif
}
int r2_lora_utc_request(uint64_t token,uint32_t delay_ms,bool override){
#if !defined(R2_LORA_PASSIVE_SLOW) || defined(R2_LORA_TX_DIAGNOSTIC)
 (void)token;(void)delay_ms;(void)override;return -ENOTSUP;
#else
 if(!token||delay_ms>10000)return -EINVAL;
 uint64_t phase=0,root=0;uint32_t epoch=0;struct r2_capture limit={0};
 enum source_result source=source_now(&phase,&epoch,&root,&limit);uint64_t now=mono();
 k_spinlock_key_t key=k_spin_lock(&lock);int result=0;unsigned reason=UTC_OK;
 if(token==utc_job.token){k_spin_unlock(&lock,key);return 0;}
 if(token==utc_job.floor){result=utc_job.last_result;k_spin_unlock(&lock,key);return result;}
 if(token<utc_job.floor){k_spin_unlock(&lock,key);return -ESTALE;}
 atomic_val_t f=atomic_get(&flags);
 if(utc_active(utc_job.state)){result=-EBUSY;reason=UTC_BUSY;}
 else if(utc_job.admitted==UINT32_MAX){result=-EOVERFLOW;reason=UTC_LIMIT;}
 else if(!transmitter||!group||!(f&ALLOWED)||!(f&REQUESTED)||(f&PROFILE_RC)||atomic_get(&faulted)||stop_deadline){result=-EACCES;reason=UTC_ROLE;}
 else if(source==SOURCE_TRANSIENT){result=-EAGAIN;reason=UTC_WAIT_SOURCE;}
 else if(source!=SOURCE_READY||!(f&SOURCE_PRESENT)||role.tx.maps[0].scope.root_boot!=root||role.tx.maps[0].scope.source_epoch!=epoch){result=-EAGAIN;reason=UTC_NO_SOURCE;}
 else if(now>UINT64_MAX-(uint64_t)delay_ms*1000-UTC_PLAN_US){result=-ERANGE;reason=UTC_LIMIT;}
 utc_job.floor=token;utc_job.last_result=result;
 if(result){inc(&utc_job.refused);if(!utc_active(utc_job.state))utc_job.reason=(uint8_t)reason;}
 else{
  utc_job.token=token;utc_job.scope=role.tx.maps[0].scope;utc_job.revision=(uint32_t)atomic_get(&source_revision);
  utc_job.capture_epoch=limit.epoch;utc_job.due_us=now+(uint64_t)delay_ms*1000;utc_job.deadline_us=utc_job.due_us+UTC_PLAN_US;
  ++utc_job.admitted;utc_job.state=UTC_PENDING;utc_job.reason=UTC_OK;utc_job.override=override;
  utc_job.reserved_us=utc_job.deferrals=0;utc_job.wait_reason=UTC_OK;utc_retry_us=0;
 }
 k_spin_unlock(&lock,key);if(!result)k_wakeup(&worker_thread);return result;
#endif
}
/* Only an unstarted job expires on its scheduling TTL. Once physical work is
 * admitted, original RF/source guards complete or quiesce it; never cut FOLLOW
 * merely because a host request deadline passed. */
static bool utc_check(uint64_t now){
 k_spinlock_key_t initial=k_spin_lock(&lock);bool active=utc_active(utc_job.state);k_spin_unlock(&lock,initial);if(!active)return false;
 struct r2_capture at;bool captured=r2_capture_now(&at);unsigned reason=UTC_OK;
 k_spinlock_key_t key=k_spin_lock(&lock);atomic_val_t f=atomic_get(&flags);
 if(utc_active(utc_job.state)){
  if(!(f&ALLOWED)||!(f&REQUESTED)||(f&PROFILE_RC)||atomic_get(&faulted)||stop_deadline)reason=UTC_OFF;
  else if(utc_job.revision!=(uint32_t)atomic_get(&source_revision)||!rc8_scope_equal(&utc_job.scope,&role.tx.maps[0].scope)||!captured||at.epoch!=utc_job.capture_epoch)reason=UTC_CONTEXT;
  else if(!(f&SOURCE_PRESENT))reason=UTC_NO_SOURCE;
  else if(utc_job.state==UTC_PENDING&&(now>utc_job.deadline_us||utc_job.deadline_us-now<PRECHARGE_LEAD_US+LP_FRAME_END_US))reason=UTC_DEADLINE;
 }
 k_spin_unlock(&lock,key);
 if(reason){utc_finish(UTC_EXPIRED,reason);return true;}return false;
}
static bool utc_plan(uint64_t now,struct lp_tx *t,uint64_t *target,bool *override){
 uint64_t due=utc_due();if(due==UINT64_MAX||now<due)return false;
 k_spinlock_key_t key=k_spin_lock(&lock);
 bool good=utc_job.state==UTC_PENDING&&!t->durable.pending&&
  now<=UINT64_MAX-PRECHARGE_LEAD_US-LP_FRAME_END_US-1000000;
 uint64_t at=good?now+PRECHARGE_LEAD_US:0;
 bool extra=good&&!t->durable.owed&&at+LP_FRAME_END_US+1000000<=t->durable.next_slot_us;
 /* Near a genuine periodic deadline, let this job own that due event rather
  * than create two close pairs or stall behind its exhausted ordinary cap. */
 good=good&&(extra||t->durable.owed||at>=t->durable.next_slot_us)&&
  at+LP_FRAME_END_US<=utc_job.deadline_us;
 if(good){*target=at;*override=utc_job.override;}
 else utc_job.reason=utc_job.wait_reason=UTC_WAIT_SLOT;
 k_spin_unlock(&lock,key);return good;
}
static void utc_defer(uint64_t now,unsigned reason){
 k_spinlock_key_t key=k_spin_lock(&lock);inc(&utc_job.deferrals);utc_job.reason=utc_job.wait_reason=(uint8_t)reason;
 /* Dedicated job retry: never move the ordinary timer or leave it armed. */
 utc_retry_us=now+ADMISSION_RETRY_US+(uint64_t)(utc_job.deferrals%7)*17000;
 utc_job.state=UTC_PENDING;utc_running_token=0;k_spin_unlock(&lock,key);
}
static void utc_reserved(void){
 k_spinlock_key_t key=k_spin_lock(&lock);utc_job.state=UTC_RESERVED;utc_job.reason=UTC_OK;
 utc_job.reserved_us=lp_duration(LP_META)+lp_duration(LP_SYNC)+lp_duration(LP_FOLLOW);
 utc_running_token=utc_job.token;k_spin_unlock(&lock,key);
}
static void utc_possible_meta(void){
 k_spinlock_key_t key=k_spin_lock(&lock);utc_job.manual_serial=role.tx.reservation.serial;utc_job.manual_stream=role.tx.stream;k_spin_unlock(&lock,key);
}
int r2_lora_utc_json(char *out,size_t capacity,int result){
#ifndef R2_LORA_PASSIVE_SLOW
 if(!result)result=-ENOTSUP;
#endif
 struct utc_request j;struct r2_lora_view v;struct lp_diagnostics d;
 k_spinlock_key_t key=k_spin_lock(&lock);j=utc_job;v=published;d=diagnostics;k_spin_unlock(&lock,key);
 uint32_t span=d.fit_span_us,remain=span>=LP_TRAIN_US?0:(uint32_t)LP_TRAIN_US-span;
 unsigned clock_reason=v.choice==RC8_SELECT_LORA?0:v.display_acquiring?5:v.display_held?4:!v.time_valid?1:!v.quality_known?2:3;
 return snprintf(out,capacity,"{\"utc_job\":1,\"supported\":%s,\"result\":%d,\"boot\":\"%016llx\",\"token\":\"%016llx\",\"floor\":\"%016llx\",\"state\":%u,\"reason\":%u,\"wait_reason\":%u,\"admitted\":%u,\"completed\":%u,\"refused\":%u,\"expired\":%u,\"deferrals\":%u,\"due_us\":%llu,\"deadline_us\":%llu,\"over_cap\":%s,\"reserved_us\":%u,\"source_kind\":%u,\"source_root_boot\":\"%016llx\",\"source_epoch\":%u,\"capture_epoch\":%llu,\"clock_reason\":%u,\"time_valid\":%s,\"trained\":%s,\"fit_n\":%u,\"fit_span_us\":%u,\"train_remaining_us\":%u}",
#ifdef R2_LORA_PASSIVE_SLOW
 "true",
#else
 "false",
#endif
 result,(unsigned long long)physical_boot,(unsigned long long)j.token,(unsigned long long)j.floor,j.state,j.reason,j.wait_reason,j.admitted,j.completed,j.refused,j.expired,j.deferrals,(unsigned long long)j.due_us,(unsigned long long)j.deadline_us,j.override?"true":"false",j.reserved_us,j.token?j.scope.kind:v.kind,(unsigned long long)j.scope.root_boot,j.scope.source_epoch,(unsigned long long)j.capture_epoch,clock_reason,v.time_valid?"true":"false",v.quality_known?"true":"false",v.link_samples,span,remain);
}

static void begin_stop(int error){
 if(error){owner.error=error;atomic_or(&faulted,1);}
 stop_now();owner.stopping=true;
 if(!transmitter){
 role.rx.display_valid=role.rx.display_acquiring=false;
#ifndef R2_LORA_PASSIVE_SLOW
 lp_rx_clear(&role.rx);
#endif
 r2_shadow_lora_withdraw();}
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
 /* Only this worker publishes sampled_us; preserve a rollback fence even
  * when local time remains later than the original accepted pair. */
 bool backwards=now<published.sampled_us;
 if(backwards&&!transmitter)role.rx.display_valid=role.rx.display_acquiring=false;
 struct r2_lora_view v={.requested=(atomic_get(&flags)&REQUESTED)!=0,.active=owner.active&&!owner.stopping,
 .transmitter=transmitter,.fault=atomic_get(&faulted)!=0,.sampled_us=now,.state=RC8_ACQUIRING};
 if(!v.requested||!v.active)v.state=RC8_OFF;
 /* Worker-owned policy is computed with interrupts enabled. Publication
  * only copies bounded scalars;61 bucket divisions must never mask BLE. */
 uint64_t charged=transmitter?lp_charged(&role.tx.core.durable,now):0;
 if(transmitter){v.kind=role.tx.maps[0].scope.kind;
#ifdef R2_LORA_TX_DIAGNOSTIC
  v.time_valid=false;
#else
  v.time_valid=(atomic_get(&flags)&SOURCE_PRESENT)!=0;
#endif
  v.parent_tag=(uint16_t)role.tx.maps[0].scope.parent;v.root_tag=(uint16_t)role.tx.maps[0].scope.root;v.emitter_tag=(uint16_t)factory;if(v.active&&v.requested&&v.time_valid)v.state=RC8_AVAILABLE;
 }
 if(!transmitter){
  struct lp_rx *r=&role.rx;struct r2_capture at;uint64_t ns;
  v.kind=r->scope.kind;v.quality_known=r->trained;v.fit_age_ms=r->last_us&&now>=r->last_us?(uint32_t)((now-r->last_us)/1000):UINT32_MAX;v.link_samples=r->fit_count;v.link_rms_ns=r->rms_ns;v.time_valid=r->have_pair;v.parent_tag=(uint16_t)r->scope.parent;v.root_tag=(uint16_t)r->scope.root;v.emitter_tag=(uint16_t)r->scope.emitter;
  uint64_t age=r->last_us&&now>=r->last_us?(now-r->last_us)/1000:UINT64_MAX;
  v.age_ms=v.rx_age_ms=age>UINT32_MAX?UINT32_MAX:(uint32_t)age;
  v.rssi_half_dbm=owner.rssi;v.snr_quarter_db=owner.snr;v.rssi_valid=r->have_pair;
  v.assurance=RC8_FRESHNESS_UNKNOWN; /* No independent source-age bound installed. */
  /* Authenticated/replay-checked META is a coarse UI hint. The original
   * bounded tz_deadline_us and every timing/source grant remain unchanged. */
  if(r->cached&&now<r->observed_deadline_us&&r2_tz_valid(r->tz)){
   v.tz=r->tz;v.tz_until_us=r->observed_deadline_us;v.tz_observed=true;
  }
  bool display_eligible=!backwards&&v.active&&v.requested&&!v.fault&&!(atomic_get(&flags)&(NORMAL|DISPLAY_CLEAR))&&r2_capture_now(&at);
  if(display_eligible&&lp_rx_estimate(r,at.tick,at.epoch,now,&ns)){
   v.utc_us=ns/1000;v.until_us=v.display_until_us=r->physical_deadline_us;v.choice=RC8_SELECT_LORA;v.state=RC8_AVAILABLE;
   /* Unknown freshness cannot issue a TZ authority lease. */
  }
#ifdef R2_LORA_PASSIVE_SLOW
  else if(display_eligible&&lp_rx_display_estimate(r,at.tick,at.epoch,now,&ns)){
   v.utc_us=ns/1000;v.until_us=r->display_us+LP_HOLD_US;v.display_until_us=UINT64_MAX;
   uint64_t reference_age=(now-r->display_us)/1000;
   v.age_ms=reference_age>UINT32_MAX?UINT32_MAX:(uint32_t)reference_age;
   v.display_acquiring=r->display_acquiring;v.display_held=!v.display_acquiring;v.state=RC8_EXPIRED;v.time_valid=v.quality_known=v.rssi_valid=false;
   /* Presentation only: original physical/META leases and all authority stay expired. */
  }
#endif
 }
 k_spinlock_key_t key=k_spin_lock(&lock);
 struct lp_diagnostics *d=&diagnostics;
 if(transmitter){d->emitter=factory;d->boot=physical_boot;d->stream=role.tx.stream;d->source_epoch=role.tx.context_epoch;d->seed=role.tx.core.durable.seed;d->cursor=role.tx.core.durable.cursor;d->serial=role.tx.core.durable.serial;
  d->pending=role.tx.core.durable.pending;d->fh_disabled=role.tx.core.durable.fh_disabled;d->next_slot=role.tx.core.durable.next_slot_us;
  d->charged=charged;d->tx_root_boot=role.tx.context_root_boot;d->store_sequence=0;
 }else{struct lp_rx *r=&role.rx;d->emitter=r->scope.emitter;d->boot=r->scope.boot;d->stream=r->scope.stream;d->source_epoch=r->scope.epoch;d->seed=r->scope.seed;d->last_tick=r->last_tick;d->last_ns=r->last_tx_ns;d->epoch=r->last_epoch;
  d->last_us=r->last_us;d->physical_until=r->physical_deadline_us;d->meta_until=r->observed_deadline_us;d->root_boot=r->scope.root_boot;
  d->fit_span_us=r->fit_count>1?(uint32_t)((r->fit[r->fit_count-1].tick-r->fit[0].tick)/16):0;
  d->cursor=r->highest;d->serial=r->serial;d->accepted=r->accepted;d->rejected=r->rejected;d->missing=r->missing;
  d->pending=r->pending;d->trained=r->trained;d->rate=r->rate_ppb;
  d->baseline_rate=r->baseline_rate_ppb;d->hold_short_rate=r->hold_short_ppb;d->hold_long_rate=r->hold_long_ppb;
  d->hold_short_guard=r->hold_short_guard;d->hold_long_guard=r->hold_long_guard;d->hold_selected=r->trained&&r->hold_selected;}
 if(!transmitter){d->model_tick=role.rx.display_tick;d->model_ns=role.rx.display_ns;d->model_phase=role.rx.display_phase_ns;}
 d->refused=owner.refused;d->late=owner.late;d->missing_map=owner.missing_map;d->bad=owner.bad;
 d->storage_fail=owner.storage_fail;if(transmitter)d->storage_max_us=owner.storage_max_us;d->error=owner.error;d->stage=owner.stage;d->phy=owner.phy;d->channel=owner.channel;
 d->reserve_lead_us=owner.reserve_lead_us;d->launch_late_us=owner.launch_late_us;
 d->source_room_us=owner.source_room_us;d->source_phase_us=owner.source_phase_us;
 d->max_source_room_us=owner.max_source_room_us;d->max_source_phase_us=owner.max_source_phase_us;
 d->reject_phase_us=owner.reject_phase_us;d->no_rf_refunds=owner.no_rf_refunds;
 memcpy(d->sent,owner.sent,sizeof d->sent);memcpy(d->received,owner.received,sizeof d->received);
 published=v;k_spin_unlock(&lock,key);
}
static void view_advance(struct r2_lora_view *v,uint64_t now){
 if(now<v->sampled_us||now>=v->tz_until_us){v->tz=r2_tz_unknown();v->tz_observed=false;}
 uint64_t until=(v->display_held||v->display_acquiring)?v->display_until_us:v->until_us;
 if(now<v->sampled_us||now>=until||(atomic_get(&flags)&DISPLAY_CLEAR)||
    (v->utc_us&&now-v->sampled_us>UINT64_MAX-v->utc_us)){
  v->utc_us=0;v->choice=RC8_SELECT_NONE;v->time_valid=v->display_held=v->display_acquiring=false;
 }else if(v->utc_us)v->utc_us+=now-v->sampled_us;
 v->sampled_us=now;
}
void r2_lora_view_named(struct r2_lora_view *v,uint64_t *f,char name[25]){
 if(!v)return;
 k_spinlock_key_t key=k_spin_lock(&lock);*v=published;if(f)*f=transmitter?factory:role.rx.scope.emitter;if(name)name[0]=0;k_spin_unlock(&lock,key);
 view_advance(v,mono());
}
void r2_lora_view(struct r2_lora_view *v){r2_lora_view_named(v,NULL,NULL);}
void __attribute__((noinline)) r2_lora_shadow_publish(void){
 struct rc_shadow_map m={0};struct r2_capture at;uint64_t now,until=0;
 if(transmitter||!r2_shadow_enabled()||!r2_capture_now_at(&at,&now))goto withdraw;
 k_spinlock_key_t key=k_spin_lock(&lock);
 const struct lp_diagnostics *d=&diagnostics;
 bool valid=published.active&&published.requested&&published.choice==RC8_SELECT_LORA&&!published.fault&&d->trained&&d->epoch==at.epoch&&now<d->physical_until&&d->last_ns<=INT64_MAX;
 if(valid){m=(struct rc_shadow_map){.normal=true,.lora=true,.normal_map={.delivery=d->model_tick,.anchor=(int64_t)d->model_ns,.rate=d->rate,.valid=true},
  .map={.effective=d->model_tick,.generation=d->accepted,.phase_ns=d->model_phase},.lo=at.tick,.epoch=at.epoch,.parent=d->emitter,.parent_boot=d->boot,
  .root=d->emitter,.root_boot=d->root_boot,.root_epoch=d->source_epoch,.accepted=d->accepted,.issued=d->last_ns};until=d->physical_until;}
 k_spin_unlock(&lock,key);
 if(!valid||until<=now)goto withdraw;
 m.normal_until=at.tick+(until-now)*16;if(m.normal_until>m.map.effective+LP_HOLD_US*16)m.normal_until=m.map.effective+LP_HOLD_US*16;
 if(shadow_tick==m.map.effective)return;
 r2_shadow_normal_map(&m);shadow_tick=m.map.effective;return;
withdraw:if(shadow_tick){r2_shadow_lora_withdraw();shadow_tick=0;}
}
/* Accounting is updated synchronously in RAM. No flash owner is entered. */
static bool storage_prepare(void){return true;}
static bool restore(uint64_t first_slot){
 if(role.tx.loaded)return role.tx.core.ready;
 if(mono()<owner.quiet_until_us)return false;
 uint32_t seed;do{seed=sys_rand32_get();}while(!seed);
 role.tx.loaded=lp_tx_start(&role.tx.core,seed,mono(),first_slot,owner.boot_ms);
 return role.tx.loaded;
}
#ifdef R2_LORA_PASSIVE_SLOW
static int launch_prepared(unsigned kind,uint64_t finish,struct r2_capture limit){
 if(owner.prepared_kind!=kind)return -EAGAIN;
 /* Waiting for a fresh original grant must not consume staged FIFO work or
  * retire its reservation. META has a narrow packet-sized BLE launch window;
  * restarting its two-second preparation after every refusal can repeatedly
  * miss that window. This check emits no RF and keeps the original slot and
  * finish deadline. The driver still consumes its token and repeats admission
  * after this check, then fences the final SetTx callback against preemption. */
 int ready=r2_lora_hw_source_preflight(r2_lora_hw_budget_us(lp_size(kind)),generation,finish,
  limit.tick,limit.epoch,mono());
 if(ready)return ready;
 struct r2_capture floor;if(!r2_capture_now(&floor))return -EAGAIN;
 k_spinlock_key_t key=k_spin_lock(&lock);++map_version;role.tx.pinned=false;
 role.tx.edge=(struct r2_lora_hw_edge){.tick=floor.tick,.capture_epoch=floor.epoch,.generation=generation};
 owner.tx_kind=(uint8_t)kind;owner.prepared_kind=0;k_spin_unlock(&lock,key);
 int rc=r2_lora_hw_launch_before_source(generation,finish,limit.tick,limit.epoch);
 /* A healthy SetTx scheduling abort still needs conservative quiescent
  * retirement. Keep its possible packet kind until the original drain ACK. */
 if(rc&&rc!=-EINTR&&!(rc==-ECANCELED&&fenced()))owner.tx_kind=0;
 return rc;
}
#endif
static int send_packet(struct lp_packet *p,uint64_t finish,struct r2_capture limit){
 uint8_t wire[LP_META_BYTES];int rc;
#ifdef R2_LORA_TX_DIAGNOSTIC
 /* Same PHY/lengths, deliberately outside every accepted timing namespace.
  * No UTC, trust tag or production context appears in diagnostic frames. */
 memset(wire,0,lp_size(p->kind));wire[0]='D';wire[1]='X';wire[2]=p->kind;wire[3]=1;
 r2_group_put(wire+4,p->cursor,4);
#else
 rc=lp_encode(wire,sizeof wire,p,role.tx.context,group);if(rc)return rc;
#endif
#ifdef R2_LORA_PASSIVE_SLOW
 (void)finish;(void)limit;
 rc=r2_lora_hw_prepare(wire,lp_size(p->kind),generation);
 if(!rc)owner.prepared_kind=p->kind;
 return rc;
#else
 struct r2_capture floor;if(!r2_capture_now(&floor))return -EAGAIN;
 k_spinlock_key_t key=k_spin_lock(&lock);++map_version;role.tx.pinned=false;
 role.tx.edge=(struct r2_lora_hw_edge){.tick=floor.tick,.capture_epoch=floor.epoch,.generation=generation};
 owner.tx_kind=p->kind;k_spin_unlock(&lock,key);
 uint64_t submitted=mono();
 rc=r2_lora_hw_tx_before_source(wire,lp_size(p->kind),generation,finish,limit.tick,limit.epoch);
 if(rc){owner.tx_kind=0;if(rc==-EAGAIN)owner.reject_phase_us=(uint32_t)((owner.source_phase_us+mono()-submitted)%UINT64_C(1000000));}
 return rc;
#endif
}
/* Keep packet/context construction outside reservation locals. Each launch
 * still rechecks the original upstream source interval after RAM precharge. */
static int __attribute__((noinline)) send_pair(enum lp_kind kind,uint64_t finish,struct r2_capture limit){
 struct lp_packet p={.kind=kind,.schedule=LP_SCHEDULE,.cursor=role.tx.reservation.cursor,
  .utc_ns=kind==LP_FOLLOW?role.tx.sync_ns:0};
 return send_packet(&p,finish,limit);
}
static int __attribute__((noinline)) launch_control(uint64_t now,uint64_t phase_ns,
 uint32_t source_epoch,uint64_t root_boot,struct r2_capture limit,bool *attempted){
 struct lp_reservation *r=&role.tx.reservation;
#ifndef R2_LORA_TX_DIAGNOSTIC
 struct lp_tx *t=&role.tx.core;
 if(r->control==LP_META&&!role.tx.context_ready){
  if(role.tx.stream==UINT32_MAX)return -EOVERFLOW;
  role.tx.context_epoch=source_epoch;role.tx.context_root_boot=root_boot;++role.tx.stream;
  k_spinlock_key_t key=k_spin_lock(&lock);struct rc8_scope upstream=role.tx.maps[0].scope;k_spin_unlock(&lock,key);
  if(upstream.root_boot!=root_boot||upstream.source_epoch!=source_epoch)return -ESTALE;
  role.tx.scope=(struct lp_scope){.emitter=factory,.boot=physical_boot,.root=upstream.root,.root_boot=root_boot,
   .parent=upstream.parent,.parent_boot=upstream.parent_boot,.kind=upstream.kind,.epoch=source_epoch,.stream=role.tx.stream,.seed=t->durable.seed};
  if(lp_context(role.tx.context,&role.tx.scope,LP_SCHEDULE,group))return -EINVAL;
 }
#endif
 struct lp_packet p={.kind=r->control,.schedule=LP_SCHEDULE,.scope=role.tx.scope,.cursor=r->cursor,.serial=r->serial,.utc_ns=
#ifdef R2_LORA_TX_DIAGNOSTIC
  0,
#else
  phase_ns,
#endif
  .has_pair=r->has_pair,.time_valid=true,.tz=r2_tz_unknown()};
 k_spinlock_key_t key=k_spin_lock(&lock);if(now<source_tz.until_us)p.tz=source_tz.value;k_spin_unlock(&lock,key);
 if(r->has_pair)p.delay_10ms=(uint16_t)((LP_SYNC_OFFSET_US-(lp_duration(p.kind)-10000)+9999)/10000);
 int rc=tune(R2_LORA_DTS,0,false);if(rc)return rc;
#ifndef R2_LORA_PASSIVE_SLOW
 struct r2_capture launch;owner.launch_late_us=(int32_t)((int64_t)mono()-(int64_t)r->slot_us);
 owner.source_room_us=r2_capture_now(&launch)&&launch.epoch==limit.epoch&&launch.tick<limit.tick?(uint32_t)((limit.tick-launch.tick)/16):0;
 r2_diag_scratch.lora.attempt.source_room_us=owner.source_room_us;r2_diag_scratch.lora.attempt.valid|=16;
 *attempted=true;
#else
 (void)attempted;
#endif
 return send_packet(&p,r->slot_us+LP_LAUNCH_PAD_US+lp_duration(p.kind),limit);
}
static int __attribute__((noinline)) transmit(uint64_t now){
#ifdef R2_LORA_PASSIVE_SLOW
 if(owner.stage==4&&owner.tx_kind==LP_SYNC)return 0; /* Captured completion awaits projection. */
#endif
 struct lp_tx *t=&role.tx.core;uint64_t phase_ns=0;uint32_t source_epoch=0;uint64_t root_boot=0;struct r2_capture limit;
 uint32_t revision=(uint32_t)atomic_get(&source_revision);
 if(revision!=owner.revision){
  /* utc_check fences the job against the current published scope. A job
   * admitted after publication must survive this worker-local turnover. */
  if(t->durable.pending){begin_stop(0);return 0;}
  role.tx.context_ready=false;owner.revision=revision;
 }
 enum source_result source=source_now(&phase_ns,&source_epoch,&root_boot,&limit);
 if(source==SOURCE_TRANSIENT)return 0;
 if(source==SOURCE_ABSENT){
  utc_finish(UTC_EXPIRED,UTC_NO_SOURCE);
  if(t->durable.pending)begin_stop(0);
  return 0;
 }
 struct r2_capture observed;
 if(r2_capture_now(&observed)&&observed.epoch==limit.epoch&&observed.tick<limit.tick){
  uint64_t room=(limit.tick-observed.tick)/16;uint32_t bounded=room>UINT32_MAX?UINT32_MAX:(uint32_t)room;
  if(bounded>owner.max_source_room_us){owner.max_source_room_us=bounded;owner.max_source_phase_us=(uint32_t)((phase_ns%UINT64_C(1000000000))/1000);}}
 if(owner.tx_kind)return 0;
 /* Choose the initial fixed monotonic phase200ms after the source second.
  * Own-GNSS passive LoRa has its separate three-second PPS grant; delivered
  * BLE time retains the original authenticated upstream bound.
  * Never slew/rephase an admitted stream; service restarts retain next_slot.
  * Per-boot accounting survives service changes; no prior boot is restored. */
 uint64_t period_ns=UINT64_C(1000000000);
 uint64_t delay_ns=(period_ns+UINT64_C(200000000)-phase_ns%period_ns)%period_ns;
#ifdef R2_LORA_PASSIVE_SLOW
 if(delay_ns<PRECHARGE_LEAD_US*1000)delay_ns+=((PRECHARGE_LEAD_US*1000-delay_ns+period_ns-1)/period_ns)*period_ns;
#else
 if(delay_ns<UINT64_C(300000000))delay_ns+=period_ns;
#endif
 if(!restore(now+(delay_ns+999)/1000)){
#ifdef R2_LORA_PASSIVE_SLOW
  if(!owner.listening)return tune(R2_LORA_DTS,0,true);
#endif
  return 0;}
 if(owner.stage==1){
  uint64_t at=role.tx.reservation.slot_us+LP_SYNC_OFFSET_US;
#ifdef R2_LORA_PASSIVE_SLOW
  if(owner.prepared_kind!=LP_SYNC&&now+PAIR_PREPARE_LEAD_US>=at){
   int rc=tune(R2_LORA_FHSS,lp_channel(t->order,role.tx.reservation.cursor),false);if(rc)return rc;
   rc=send_pair(LP_SYNC,0,limit);if(rc)return rc;now=mono();
  }
#endif
  if(now<at)return 0;
  due_lateness(LP_SYNC,at,now);
  if(now>at+LP_LAUNCH_LATE_US){inc(&owner.late);
#ifdef R2_LORA_PASSIVE_SLOW
 /* DTS already advertised a future SYNC. Preserve pending/stage until
  * actual closure applies the peer's original hint/retune recovery floor.
  * Extra UTC jobs remain manual and cannot create ordinary owed work. */
 utc_finish(UTC_EXPIRED,UTC_FAILED);if(!t->manual)inc(&retry_count);begin_stop(0);return 0;
#else
 lp_tx_fault(t);
#endif
 owner.stage=owner.prepared_kind=0;
#ifdef R2_LORA_TX_DIAGNOSTIC
 r2_lora_request(false);
#endif
 return 0;}
#ifdef R2_LORA_PASSIVE_SLOW
  if(fenced()||revision!=(uint32_t)atomic_get(&source_revision))return -ECANCELED;
  source=source_now(&phase_ns,&source_epoch,&root_boot,&limit);if(source==SOURCE_TRANSIENT)return 0;
  if(source==SOURCE_ABSENT){begin_stop(0);return 0;}
  int rc=0;
#else
  int rc=tune(R2_LORA_FHSS,lp_channel(t->order,role.tx.reservation.cursor),false);if(rc)return rc;
#endif
  owner.source_phase_us=(uint32_t)((phase_ns%UINT64_C(1000000000))/1000);
#ifdef R2_LORA_PASSIVE_SLOW
  rc=launch_prepared(LP_SYNC,at+LP_LAUNCH_PAD_US+lp_duration(LP_SYNC),limit);
#else
  rc=send_pair(LP_SYNC,at+LP_LAUNCH_PAD_US+lp_duration(LP_SYNC),limit);
#endif
#ifdef R2_LORA_PASSIVE_SLOW
  if(rc==-EAGAIN)return 0;
  if(rc==-EINTR||(rc==-ECANCELED&&fenced()))return rc; /* Preserve pending for proven closure/retry. */
#endif
  if(rc)lp_tx_fault(t);
  return rc;
 }
 if(owner.stage==2){
  uint64_t at=owner.last_sync_us+LP_FOLLOW_OFFSET_US;
#ifdef R2_LORA_PASSIVE_SLOW
  if(owner.prepared_kind!=LP_FOLLOW){
   int rc=tune(R2_LORA_FHSS,lp_channel(t->order,role.tx.reservation.cursor+1),false);if(rc)return rc;
   rc=send_pair(LP_FOLLOW,0,limit);if(rc)return rc;now=mono();
  }
#endif
  if(now<at)return 0;
  due_lateness(LP_FOLLOW,at,now);
  if(now>owner.last_sync_us+LP_FOLLOW_LATEST_US-5000){inc(&owner.late);utc_finish(UTC_EXPIRED,UTC_FAILED);begin_stop(0);
#ifdef R2_LORA_TX_DIAGNOSTIC
 r2_lora_request(false);
#endif
 return 0;}
#ifdef R2_LORA_PASSIVE_SLOW
  if(fenced()||revision!=(uint32_t)atomic_get(&source_revision))return -ECANCELED;
  source=source_now(&phase_ns,&source_epoch,&root_boot,&limit);if(source==SOURCE_TRANSIENT)return 0;
  if(source==SOURCE_ABSENT){begin_stop(0);return 0;}
  int rc=0;
#else
  int rc=tune(R2_LORA_FHSS,lp_channel(t->order,role.tx.reservation.cursor+1),false);if(rc)return rc;
#endif
  owner.source_phase_us=(uint32_t)((phase_ns%UINT64_C(1000000000))/1000);
#ifdef R2_LORA_PASSIVE_SLOW
  rc=launch_prepared(LP_FOLLOW,owner.last_sync_us+LP_FOLLOW_LATEST_US+lp_duration(LP_FOLLOW),limit);
#else
  rc=send_pair(LP_FOLLOW,owner.last_sync_us+LP_FOLLOW_LATEST_US+lp_duration(LP_FOLLOW),limit);
#endif
#ifdef R2_LORA_PASSIVE_SLOW
  if(rc==-EAGAIN)return 0;
  if(rc==-EINTR||(rc==-ECANCELED&&fenced()))return rc; /* Preserve pending for proven closure/retry. */
#endif
  if(rc)lp_tx_fault(t);
  return rc;
 }
 /* A fresh context may skip DISC slots while awaiting META. Select that
  * future slot BEFORE applying the precharge lead; a stale next_slot must
  * not defer its RAM reservation until the RF deadline itself. */
 uint64_t target=t->durable.next_slot_us,skipped=0;
 if(now>target&&now-target>LP_LAUNCH_LATE_US)skipped=(now-target+LP_BEAT_US-1)/LP_BEAT_US;
 uint64_t index=(uint64_t)t->durable.slot+skipped;
 if(!role.tx.context_ready&&index%LP_META_BEATS)skipped+=LP_META_BEATS-index%LP_META_BEATS;
 target+=skipped*LP_BEAT_US;
#ifdef R2_LORA_PASSIVE_SLOW
 target=t->durable.next_slot_us;
 if(owner.stage!=3&&(retry_at_us||lp_tx_has_deferred(t)||now>target+LP_LAUNCH_LATE_US)){
  if(!retry_at_us||now>retry_at_us+LP_LAUNCH_LATE_US)retry_at_us=now+PRECHARGE_LEAD_US;
  target=retry_at_us;
 }
#endif
 bool job_owned=owner.stage==3&&utc_running_token,over_cap=false;
#ifdef R2_LORA_PASSIVE_SLOW
 if(!owner.stage)job_owned=utc_plan(now,t,&target,&over_cap);
#endif
 #ifdef R2_LORA_TX_DIAGNOSTIC
 /* Reserve only complete original frames. Host loss cannot create a partial
  * final FH pair or renew the finite lifetime/new-charge budget. */
 if(!owner.stage&&!owner.tx_kind&&!t->durable.pending&&
    (t->durable.fh_disabled||target+LP_FRAME_END_US>lab_until||
     r2_diag_scratch.lora.diagnostic.new_charge_us>DIAGNOSTIC_CHARGE_US-
       (lp_duration(LP_META)+lp_duration(LP_SYNC)+lp_duration(LP_FOLLOW)))){
  r2_lora_request(false);return 0;}
#endif
 if(owner.stage!=3&&now+PRECHARGE_LEAD_US<target){
  /* No storage work occurs during idle or the reservation interval. */
  if(!storage_prepare()){inc(&owner.storage_fail);return -EIO;}
#ifdef R2_LORA_PASSIVE_SLOW
  if(!owner.listening)return tune(R2_LORA_DTS,0,true);
#endif
  return 0;}
 if(owner.stage!=3){
  r2_diag_scratch.lora.attempt.valid=0;r2_diag_scratch.lora.attempt.target_us=target;
  r2_diag_scratch.lora.attempt.cursor=t->durable.cursor;r2_diag_scratch.lora.attempt.serial=t->durable.serial;
  r2_diag_scratch.lora.attempt.commit_us=0;r2_diag_scratch.lora.attempt.committed_us=0;
  r2_diag_scratch.lora.attempt.postcommit_lead_us=0;r2_diag_scratch.lora.attempt.first_due_late_us=0;
  r2_diag_scratch.lora.attempt.source_room_us=r2_diag_scratch.lora.attempt.source_phase_us=0;
  r2_diag_scratch.lora.attempt.source_revision=revision;
  owner.source_room_us=owner.source_phase_us=0;
  if(!storage_prepare()){inc(&owner.storage_fail);return -EIO;}
  r2_diag_scratch.lora.attempt.valid=1;
  if(mono()>target+LP_LAUNCH_LATE_US)return 0;
#ifndef R2_LORA_PASSIVE_SLOW
  unsigned planned=(!role.tx.context_ready||index%LP_META_BEATS==0)?LP_META:LP_DISC;
  int preflight=r2_lora_hw_source_preflight(lp_duration(planned),generation,
      target+LP_LAUNCH_PAD_US+lp_duration(planned),limit.tick,limit.epoch,target);
  if(preflight){if(preflight!=-EAGAIN)return preflight;
   inc(&owner.refused);owner.reject_phase_us=(uint32_t)((phase_ns%UINT64_C(1000000000))/1000);
   return 0;}
#else
  /* The source snapshot above is valid NOW. Encoding/FIFO staging emits no
   * RF and needs no forecast permission. Admission against a packet-sized
   * current grant can phase-lock retries away from fresh short BLE grants.
   * Only actual launch/callback requires the complete original RF interval. */
#endif
  uint64_t at=mono();owner.reserve_lead_us=target>at?(uint32_t)(target-at):0;
  bool reserved=job_owned?lp_tx_reserve_utc(t,target,&role.tx.reservation,over_cap):lp_tx_reserve_control(t,target,&role.tx.reservation,!role.tx.context_ready);
  uint64_t committed=mono();r2_diag_scratch.lora.attempt.commit_us=(uint32_t)(committed-at);
  r2_diag_scratch.lora.attempt.committed_us=committed;
  r2_diag_scratch.lora.attempt.postcommit_lead_us=(int32_t)((int64_t)target-(int64_t)committed);
  if(!reserved){r2_diag_scratch.lora.attempt.valid|=32;inc(&owner.refused);
#ifdef R2_LORA_PASSIVE_SLOW
   if(job_owned){uint64_t charged=lp_charged(&t->durable,mono());uint32_t cost=lp_duration(LP_META)+lp_duration(LP_SYNC)+lp_duration(LP_FOLLOW);
    if(t->durable.fh_disabled||t->durable.serial==UINT32_MAX||t->durable.slot==UINT32_MAX)utc_finish(UTC_EXPIRED,UTC_LIMIT);
    else utc_defer(mono(),!over_cap&&(charged>LP_CAP_US||cost>LP_CAP_US-charged)?UTC_WAIT_BUDGET:UTC_WAIT_COOLDOWN);
   }else retry_at_us=admission_retry(mono());
#endif
   return 0;}
#ifdef R2_LORA_PASSIVE_SLOW
  if(job_owned)utc_reserved();
  if(!t->manual)retry_at_us=0;
#endif
  r2_diag_scratch.lora.attempt.cursor=role.tx.reservation.cursor;r2_diag_scratch.lora.attempt.serial=role.tx.reservation.serial;
  r2_diag_scratch.lora.attempt.valid|=2;
#ifdef R2_LORA_TX_DIAGNOSTIC
  r2_diag_scratch.lora.diagnostic.new_charge_us+=lp_duration(role.tx.reservation.control)+
   (role.tx.reservation.has_pair?lp_duration(LP_SYNC)+lp_duration(LP_FOLLOW):0);
#endif
  owner.stage=3;
  /* Reservation and all later RF transitions remain at priority11.
   * Contention uses the original late/deferred obligation path. */
 }
 struct lp_reservation *r=&role.tx.reservation;
#ifdef R2_LORA_PASSIVE_SLOW
 /* An authenticated context and the preceding FOLLOW predict this slot.
  * No DTS packet is fabricated or charged on precision-only slots. SYNC
  * still stages and rechecks its original physical/source grant normally. */
 if(!r->control){owner.stage=1;return 0;}
 if(owner.prepared_kind!=r->control){
  bool prepared=false;int rc=launch_control(mono(),phase_ns,source_epoch,root_boot,limit,&prepared);
  if(rc)return rc;
 }
#endif
 owner.launch_late_us=(int32_t)((int64_t)mono()-(int64_t)r->slot_us);
 if(mono()<r->slot_us)return 0;
 if(!(r2_diag_scratch.lora.attempt.valid&4)){r2_diag_scratch.lora.attempt.first_due_late_us=owner.launch_late_us;r2_diag_scratch.lora.attempt.valid|=4;}
 due_lateness(r->control,r->slot_us,mono());
 if(mono()>r->slot_us+LP_LAUNCH_LATE_US){inc(&owner.late);lp_tx_cancel(t,true);owner.stage=owner.prepared_kind=0;
#ifdef R2_LORA_PASSIVE_SLOW
 if(job_owned)utc_finish(UTC_EXPIRED,UTC_FAILED);else{retry_at_us=mono()+LP_RETRY_US;++retry_count;}
#endif
#ifdef R2_LORA_TX_DIAGNOSTIC
 r2_lora_request(false);
#endif
 return 0;}
 /* Precharge may yield through a physical PPS/map update. Refresh the source
  * snapshot after storage, before the guarded launch; never renew a staged TX. */
 if(fenced()||revision!=(uint32_t)atomic_get(&source_revision))return -ECANCELED;
 source=source_now(&phase_ns,&source_epoch,&root_boot,&limit);if(source==SOURCE_TRANSIENT)return 0;
 if(source==SOURCE_ABSENT){lp_tx_cancel(t,true);if(job_owned)utc_finish(UTC_EXPIRED,UTC_NO_SOURCE);owner.stage=owner.prepared_kind=0;return 0;}
 owner.source_phase_us=(uint32_t)((phase_ns%UINT64_C(1000000000))/1000);
 r2_diag_scratch.lora.attempt.source_phase_us=owner.source_phase_us;r2_diag_scratch.lora.attempt.valid|=8;
 bool attempted=false;int rc;
#ifdef R2_LORA_PASSIVE_SLOW
 owner.source_room_us=r2_capture_now(&observed)&&observed.epoch==limit.epoch&&observed.tick<limit.tick?(uint32_t)((limit.tick-observed.tick)/16):0;
 r2_diag_scratch.lora.attempt.source_room_us=owner.source_room_us;r2_diag_scratch.lora.attempt.valid|=16;
 attempted=true;rc=launch_prepared(r->control,r->slot_us+LP_LAUNCH_PAD_US+lp_duration(r->control),limit);
 /* An intact prepared token means the capture-only precheck deferred work;
  * no driver attempt, cancellation, debit refund or new context occurred. */
 if(rc==-EAGAIN&&owner.prepared_kind==r->control)return 0;
#else
 rc=launch_control(now,phase_ns,source_epoch,root_boot,limit,&attempted);
#endif
 if(job_owned&&attempted&&(rc!=-EAGAIN||!r2_lora_hw_no_rf(generation)))utc_possible_meta();
#ifdef R2_LORA_PASSIVE_SLOW
 /* A known scheduling abort closes through the same bounded physical ACK
  * path as partial pairs; do not pre-clear the pending recovery evidence. */
 if(rc==-EINTR||(rc==-ECANCELED&&fenced()))return rc;
#endif
 /* Cancel only a failed send, after launch locals have left the save chain. */
 if(rc&&attempted){
  /* Only the synchronous healthy pre-SetTx proof can return this complete
   * reservation's debit. A DTS emission/cancel/abort is never refundable. */
  if(rc==-EAGAIN&&r2_lora_hw_no_rf(generation)&&lp_tx_refund_no_rf(t,r)){
   inc(&owner.no_rf_refunds);
   if(job_owned){k_spinlock_key_t k=k_spin_lock(&lock);utc_job.reserved_us=0;k_spin_unlock(&lock,k);utc_defer(mono(),UTC_WAIT_SOURCE);owner.stage=owner.prepared_kind=0;return 0;}
#ifdef R2_LORA_TX_DIAGNOSTIC
   r2_diag_scratch.lora.diagnostic.new_charge_us-=lp_duration(r->control)+(r->has_pair?lp_duration(LP_SYNC)+lp_duration(LP_FOLLOW):0);
#endif
  }else lp_tx_cancel(t,rc==-EAGAIN);
#ifdef R2_LORA_PASSIVE_SLOW
  if(job_owned)utc_finish(UTC_EXPIRED,UTC_FAILED);else if(rc==-EAGAIN){retry_at_us=admission_retry(mono());}
#endif
 }
 return rc;
}
static int __attribute__((noinline)) receive_event(const struct r2_lora_hw_event *h,uint64_t event,uint64_t now){
 struct lp_rx *r=&role.rx;struct lp_packet p;
 if(lp_decode(&p,h->frame,h->length,r->context,group)){inc(&owner.bad);return 0;}
 k_spinlock_key_t audit_key=k_spin_lock(&lock);
 audits[AUDIT_INDEX(p.kind)]=(struct lp_audit){.tick=h->tick,.epoch=h->capture_epoch,.start_us=event,.end_us=now,.cursor=p.cursor,.phy=owner.phy,.channel=owner.channel,.valid=true};
 k_spin_unlock(&lock,audit_key);
#ifdef R2_LORA_PASSIVE_SLOW
 audit_kind=p.kind;
#endif
 inc(&owner.received[p.kind-1]);owner.rssi=h->rssi_half_dbm;owner.snr=h->snr_quarter_db;
 if(owner.phy==R2_LORA_DTS){
  if(p.kind!=LP_META&&p.kind!=LP_DISC)return 0;
  uint32_t duration=lp_duration(p.kind)-10000;if(event<duration)return 0;
  uint64_t start=event-duration;
  if(p.kind==LP_META){
   if(p.scope.emitter!=transmitter_factory){inc(&owner.bad);return 0;}
   unsigned peer=0;
   struct lp_replay *floor=&replay[peer];
   if(!lp_replay_admit(floor,&p)){inc(&owner.bad);return 0;}
   if(!lp_rx_meta(r,&p,group,NULL,event,now)){inc(&owner.bad);return 0;}
   lp_replay_commit(floor,&p);
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
  bool accepted=lp_rx_follow(r,&p,NULL,h->capture_epoch,now);
#ifdef R2_LORA_PASSIVE_SLOW
  if(accepted&&r->trained){struct r2_capture at;if(r2_capture_now(&at))lp_rx_discipline(r,at.tick,at.epoch,mono());}
#endif
  if(accepted)replay[0].cursor=r->highest<=UINT32_MAX-2?r->highest+2:UINT32_MAX;
  owner.listening=false;return tune(R2_LORA_DTS,0,true);
 }
 return 0;
}
static int __attribute__((noinline)) tx_complete(const struct r2_lora_hw_event *h,uint64_t event){
 unsigned kind=owner.tx_kind;if(!kind)return -ESTALE;
 bool mapped=false,raced=false;uint64_t sync_ns=0;uint32_t mapped_version=0;
 if(kind==LP_SYNC){
#ifdef R2_LORA_TX_DIAGNOSTIC
  /* process_event already validated the physical incarnation/timestamp. */
  mapped=true;role.tx.sync_ns=0;
#else
#ifdef R2_LORA_PASSIVE_SLOW
  /* Pin the exact captured event before projecting outside the copy lock.
   * Keep the existing immutable map through later publication/poll retries. */
  k_spinlock_key_t pin_key=k_spin_lock(&lock);
  if(!role.tx.pinned){for(unsigned i=0;i<2;i++){const struct rc8_export *m=&role.tx.maps[i];
   if(m->valid&&m->authenticated&&m->capture_epoch==h->capture_epoch&&h->tick>=m->from_tick&&h->tick<m->until_tick){
    if(!i)role.tx.maps[1]=*m;
    role.tx.edge=(struct r2_lora_hw_edge){h->tick,h->capture_epoch,h->generation};role.tx.pinned=true;++map_version;break;
   }
  }}
  k_spin_unlock(&lock,pin_key);
  if(owner.stage==4&&mono()>=h->end_local_us+LP_FOLLOW_LATEST_US-5000)raced=true;
  else
#endif
  mapped=completed_source(h,&sync_ns,&mapped_version,&raced);
#endif
 }
 k_spinlock_key_t key=k_spin_lock(&lock);
 if(mapped&&kind==LP_SYNC){
#ifndef R2_LORA_TX_DIAGNOSTIC
  if(mapped_version!=map_version||owner.tx_kind!=LP_SYNC||generation!=h->generation||
     stop_deadline||!(atomic_get(&flags)&SOURCE_PRESENT)){mapped=false;raced=true;}
  else role.tx.sync_ns=sync_ns;
#else
  (void)sync_ns;(void)mapped_version;
#endif
 }
#ifdef R2_LORA_PASSIVE_SLOW
 if(kind==LP_SYNC){
  if(!mapped&&raced&&mono()<h->end_local_us+LP_FOLLOW_LATEST_US-5000&&!stop_deadline&&(atomic_get(&flags)&SOURCE_PRESENT)){
   owner.stage=4;owner.last_end_us=h->end_local_us;
   role.tx.edge=(struct r2_lora_hw_edge){h->tick,h->capture_epoch,h->generation};
   inc(&sync_map[1]);k_spin_unlock(&lock,key);return 0;
  }
  inc(&sync_map[mapped?0:mono()>=h->end_local_us+LP_FOLLOW_LATEST_US-5000?3:2]);
 }
#endif
 ++map_version;role.tx.pinned=false;
 owner.tx_kind=0;
#ifdef R2_LORA_PASSIVE_SLOW
 if(kind!=LP_SYNC) /* Physical SYNC was recorded when hardware DONE was consumed. */
#endif
 audits[AUDIT_INDEX(kind)]=(struct lp_audit){.tick=h->tick,.epoch=h->capture_epoch,.start_us=event,.end_us=h->end_local_us,
 .cursor=role.tx.reservation.cursor+(kind==LP_FOLLOW?1:0),.phy=owner.phy,.channel=owner.channel,.valid=true};
 k_spin_unlock(&lock,key);
#ifdef R2_LORA_PASSIVE_SLOW
 audit_kind=kind;
#endif
#ifdef R2_LORA_PASSIVE_SLOW
 if(kind!=LP_SYNC)
#endif
 {inc(&owner.sent[kind-1]);owner.last_end_us=h->end_local_us;}
 if(kind==LP_META||kind==LP_DISC){lp_tx_control_complete(&role.tx.core);
#ifdef R2_LORA_PASSIVE_SLOW
  /* The received delay is measured from this actual control event, not its
   * earlier reservation. Keep the recurring30s grid separate. */
  role.tx.reservation.slot_us=role.tx.core.slot_us=event;
#endif
#ifdef R2_LORA_PASSIVE_SLOW
  role.tx.dirty=false;
#endif
  if(kind==LP_META)role.tx.context_ready=true;
  owner.stage=role.tx.reservation.has_pair?1:0;return 0;}
 bool confirmed=kind==LP_SYNC?lp_tx_capture_sync(&role.tx.core,h->end_local_us):lp_tx_capture_follow(&role.tx.core,h->end_local_us);
 if(!confirmed){owner.stage=owner.prepared_kind=0;return EVENT_FH_FAULT;}
 if(kind==LP_SYNC){
  if(!mapped){inc(&owner.missing_map);owner.stage=owner.prepared_kind=0;
   /* The physical SYNC is accounted above. A publisher that preempted
    * projection causes proven-quiescent retirement and retry, not a fault. */
   if(raced){begin_stop(0);return 0;}return EVENT_FH_FAULT;}
  owner.last_sync_us=h->end_local_us;owner.stage=2;
 }else{role.tx.dirty=false;owner.stage=owner.prepared_kind=0;if(utc_running_token)utc_finish(UTC_COMPLETED,UTC_OK);}
 return 0;
}

static int __attribute__((noinline)) process_event(uint64_t now){
 struct r2_lora_hw_event h;
#ifdef R2_LORA_PASSIVE_SLOW
 if(transmitter&&owner.stage==4&&owner.tx_kind==LP_SYNC){
  h.tick=role.tx.edge.tick;h.capture_epoch=role.tx.edge.capture_epoch;h.generation=role.tx.edge.generation;h.end_local_us=owner.last_end_us;h.kind=R2_LORA_HW_TX_DONE;
  /* First hardware DONE passed physical() before its audit was retained.
   * A deferred projection reuses that immutable event; it does not re-admit
   * an aged capture or acquire new source authority. Keep live epoch and
   * monotonic guards while the original FOLLOW deadline stays absolute. */
  k_spinlock_key_t saved_key=k_spin_lock(&lock);
  uint64_t event=audits[0].start_us;
  bool saved=audit_kind==LP_SYNC&&audits[0].valid&&audits[0].tick==h.tick&&
   audits[0].epoch==h.capture_epoch&&audits[0].end_us==h.end_local_us&&h.generation==generation&&mono()>=published.sampled_us;
  k_spin_unlock(&lock,saved_key);
  struct r2_capture current;
  if(!saved||!r2_capture_now(&current)||current.epoch!=h.capture_epoch||current.tick<h.tick||mono()<h.end_local_us)return EVENT_FH_FAULT;
  return tx_complete(&h,event); /* Hardware DONE is consumed exactly once. */
 }
 if(transmitter&&owner.tx_kind==LP_SYNC){k_spinlock_key_t key=k_spin_lock(&lock);preserve();k_spin_unlock(&lock,key);}
#endif
 int rc=r2_lora_hw_take(&h);if(rc<=0)return rc;
 uint64_t event;if(h.generation!=generation||!physical(&h,&event)){inc(&owner.bad);return transmitter&&role.tx.core.durable.pending?EVENT_FH_FAULT:0;}
 if(h.kind==R2_LORA_HW_RX_DONE){owner.listening=false;
  /* TX-mode idle RX must not write the RX half of the shared role storage.
   * A GNSS serving GM already has time; the HW retains RX/error counters. */
  rc=transmitter?0:receive_event(&h,event,mono());
  if(!rc&&!owner.listening){rc=r2_lora_hw_rx_rearm();if(!rc)owner.listening=true;}return rc;}
#ifdef R2_LORA_PASSIVE_SLOW
 if(owner.tx_kind==LP_SYNC){
  /* A new packet's capture floor cannot be replaced by an older DONE when
   * pinning its original map for deferred projection. */
  k_spinlock_key_t floor_key=k_spin_lock(&lock);
  bool matching=role.tx.edge.generation==h.generation&&role.tx.edge.capture_epoch==h.capture_epoch&&
   (role.tx.pinned?role.tx.edge.tick==h.tick:h.tick>=role.tx.edge.tick);
  k_spin_unlock(&lock,floor_key);if(!matching)return EVENT_FH_FAULT;
  /* Preserve the physical denominator even when later projection defers,
   * expires or withdraws. Stage4 replay never consumes hardware DONE again. */
  k_spinlock_key_t key=k_spin_lock(&lock);
  audits[0]=(struct lp_audit){.tick=h.tick,.epoch=h.capture_epoch,.start_us=event,.end_us=h.end_local_us,.cursor=role.tx.reservation.cursor,.phy=owner.phy,.channel=owner.channel,.valid=true};
  audit_kind=LP_SYNC;inc(&owner.sent[LP_SYNC-1]);owner.last_end_us=h.end_local_us;k_spin_unlock(&lock,key);
 }
#endif
 return tx_complete(&h,event);
}

static int receive_step(uint64_t now){
 struct lp_rx *r=&role.rx;lp_rx_expire(r,now);
#ifdef R2_LORA_PASSIVE_SLOW
 /* A predicted FH hop must not steal DTS while the prior predicted control
  * can still finish and be processed. Fresh authenticated control clears
  * prediction. Capture this absolute horizon before advancing the cursor. */
 bool wait_control=r->prediction&&owner.next_control_us&&owner.next_control_us<r->hint_us&&now<=owner.next_control_us+LP_CONTROL_RETIRE_US;
#endif
 /* Every30s grid point is a possible control opportunity. Listen on DTS
  * before its SYNC even on precision-only slots; actual DISC is120s and
  * META300s. Successful FOLLOW chains the next prediction, never authority. */
 if(owner.next_control_us&&now+LP_RX_TUNE_LEAD_US>=owner.next_control_us&&owner.phy!=R2_LORA_DTS){
  int rc=tune(R2_LORA_DTS,0,true);if(rc)return rc;
 }
 if(owner.next_control_us&&now>owner.next_control_us+LP_CONTROL_RETIRE_US)owner.next_control_us+=LP_BEAT_US;
 if(r->hint&&!r->pending&&now+LP_RX_TUNE_LEAD_US>=r->hint_us&&now<=r->hint_us+LP_RX_TUNE_LATE_US&&owner.phy==R2_LORA_DTS){
  /* Consume the one saved prediction when control was missed. */
#ifdef R2_LORA_PASSIVE_SLOW
  if(wait_control)return 0;
#endif
#ifndef R2_LORA_PASSIVE_SLOW
  if(r->prediction)r->prediction=false;
#endif
  return tune(R2_LORA_FHSS,lp_channel(r->order,r->hint_cursor),true);
 }
 if(owner.phy==R2_LORA_FHSS&&!r->pending&&!r->hint)return tune(R2_LORA_DTS,0,true);
 return 0;
}
static bool fenced(void){k_spinlock_key_t key=k_spin_lock(&lock);bool value=stop_deadline!=0;k_spin_unlock(&lock,key);return value;}
static void __attribute__((noinline)) service_step(void){
 uint64_t now=mono();atomic_val_t f=atomic_get(&flags);
 bool job_inflight=utc_running_token!=0;
 if(utc_check(now)&&job_inflight&&role.tx.core.durable.pending&&!owner.stopping)begin_stop(0);
 if(f&DISPLAY_CLEAR){if(!transmitter)role.rx.display_valid=role.rx.display_acquiring=false;atomic_and(&flags,~DISPLAY_CLEAR);f=atomic_get(&flags);}
 k_spinlock_key_t key=k_spin_lock(&lock);uint64_t until=lab_until;k_spin_unlock(&lock,key);
 if((f&REQUESTED)&&now>=until){r2_lora_request(false);f=atomic_get(&flags);}
 bool desired=!atomic_get(&faulted)&&(f&ALLOWED)&&(f&REQUESTED)&&group&&factory&&transmitter_factory&&(
#ifdef R2_LORA_PASSIVE_SLOW
 !transmitter||(!(f&PROFILE_RC)&&(f&SOURCE_PRESENT))
#elif defined(R2_LORA_TX_DIAGNOSTIC)
 !transmitter||!(f&PROFILE_RC)
#else
 !transmitter||(!(f&PROFILE_RC)&&(f&SOURCE_PRESENT))
#endif
 );
 r2_diag_scratch.lora.beat=k_uptime_get_32();r2_diag_scratch.lora.monitor=desired||owner.active||owner.stopping;
 if(((!desired&&owner.active)||fenced())&&!owner.stopping)begin_stop(0);
 if(owner.stopping){
  utc_finish(UTC_EXPIRED,UTC_OFF);
  if(!r2_diag_scratch.lora.drain_poll_us)r2_diag_scratch.lora.drain_poll_us=mono();
  struct r2_lora_hw_ack ack={0};r2_lora_hw_stop_poll(&ack);uint64_t observed=mono();
  bool complete=ack.dma_retired&&ack.radio_quiescent&&ack.fem_off&&ack.capture_released;
  if(ack.fault)atomic_or(&faulted,1);
  if(observed>=stop_deadline&&!complete){atomic_or(&faulted,2);r2_lora_hw_watchdog_trip(-ETIMEDOUT);}
  if(complete){r2_diag_scratch.lora.drain_ack_us=observed;
   if(observed>=stop_deadline){r2_diag_scratch.lora.drain_late=1;inc(&r2_diag_scratch.lora.drain_late_count);}
   /* The live owner can prove that no FHSS SetTx was submitted before SYNC.
    * A DTS control may have completed or been cut off; its full reservation
    * stays charged. Possible RF retires both hops only after proven closure. */
   bool no_fh=role.tx.core.durable.pending==1&&(owner.stage==3||owner.stage==1)&&owner.tx_kind!=LP_SYNC&&owner.tx_kind!=LP_FOLLOW;
   owner.active=owner.stopping=owner.listening=false;owner.tx_kind=owner.stage=owner.prepared_kind=0;atomic_set(&closed,1);
   key=k_spin_lock(&lock);stop_deadline=0;++map_version;role.tx.pinned=false;k_spin_unlock(&lock,key);k_thread_priority_set(&worker_thread,WORKER_PRIORITY);
   /* Ordinary scheduling/source cancellation may retire hops after proven
    * closure. Real/unknown faults stay inhibited until physical reboot. */
   if(transmitter&&role.tx.core.durable.pending){
    if(no_fh)lp_tx_cancel(&role.tx.core,true);
    else if(!atomic_get(&faulted)&&!owner.error&&!ack.fault&&!r2_diag_scratch.lora.drain_late&&
            lp_tx_retire_quiescent(&role.tx.core,observed)){
#ifdef R2_LORA_PASSIVE_SLOW
     /* Source revision turnover creates a fresh stream in transmit(). */
     if(owner.revision!=(uint32_t)atomic_get(&source_revision))role.tx.context_ready=false;
#else
     role.tx.context_ready=false;
#endif
    }
    else lp_tx_fault(&role.tx.core);
#ifdef R2_LORA_PASSIVE_SLOW
    if(!role.tx.core.manual&&!atomic_get(&faulted)&&!owner.error&&!ack.fault&&!r2_diag_scratch.lora.drain_late&&!role.tx.core.durable.fh_disabled){
     /* The peer may await an old FOLLOW, or still hold the authenticated
      * control's future SYNC hint when closure occurred before SYNC. Wait
      * both original horizons plus bounded retune before new control RF.
      * A canceled extra UTC job must not arm ordinary unsolicited work. */
     uint64_t rendezvous=observed+LP_PENDING_US+LP_RX_TUNE_LEAD_US;
     uint64_t hint=role.tx.reservation.slot_us+LP_SYNC_OFFSET_US+LP_LAUNCH_PAD_US+LP_HINT_RETIRE_US+LP_RX_TUNE_LEAD_US+10000;
     if(rendezvous<hint)rendezvous=hint;
     if(retry_at_us<rendezvous)retry_at_us=rendezvous;
     if(owner.revision!=(uint32_t)atomic_get(&source_revision))role.tx.context_ready=false;
    }
#endif
   }
}
 }else if(desired&&!owner.active){
  if(generation==UINT32_MAX)atomic_or(&faulted,1);
  else{++generation;owner.prepared_kind=0;atomic_set(&closed,0);
#ifdef R2_LORA_PASSIVE_SLOW
   inc(&r2_diag_scratch.lora.startup_counts[0]);
#endif
   int rc=r2_lora_hw_start(generation,false);
   if(rc==-EAGAIN)atomic_set(&closed,1);
   else{owner.active=true;if(!rc){r2_diag_scratch.lora.drain_late=0;rc=tune(R2_LORA_DTS,0,!transmitter);}
    /* Source withdrawal may preempt startup's reset/TCXO/SPI waits just as
     * it can preempt an active transaction. Drain the retained fence without
     * promoting that deliberate inhibition to a permanent hardware fault. */
    if(rc)begin_stop(rc==-ECANCELED&&fenced()?0:rc);}}
 }else if(owner.active){
  int rc=process_event(now);
  if(rc==EVENT_FH_FAULT){utc_finish(UTC_EXPIRED,UTC_FAILED);lp_tx_fault(&role.tx.core);rc=0;}
  if(!rc){if(fenced()
#ifndef R2_LORA_TX_DIAGNOSTIC
     ||(transmitter&&!(atomic_get(&flags)&SOURCE_PRESENT))
#endif
    )begin_stop(0);
   else rc=transmitter?transmit(mono()):receive_step(mono());}
  if(rc==-EAGAIN){inc(&owner.refused);if(transmitter){if(utc_running_token)utc_finish(UTC_EXPIRED,UTC_FAILED);owner.stage=owner.prepared_kind=0;lp_tx_cancel(&role.tx.core,true);}}
  else if(rc)begin_stop(rc==-EINTR||(rc==-ECANCELED&&fenced())?0:rc);
 }
#ifdef R2_LORA_PASSIVE_SLOW
 if(!desired&&!owner.active&&!transmitter)lp_rx_clear(&role.rx);
#endif
 now=mono();if(!transmitter||!owner.active||owner.stopping||now-published.sampled_us>=20000)report(now);
 r2_diag_scratch.lora.beat=k_uptime_get_32();
}
static int worker_priority(uint64_t now){
 (void)now;return owner.stopping||fenced()?DRAIN_PRIORITY:WORKER_PRIORITY;
}
static void worker(void *a,void *b,void *c){(void)a;(void)b;(void)c;for(;;){service_step();k_thread_priority_set(&worker_thread,worker_priority(mono()));k_sleep(K_MSEC(2));}}
void r2_lora_init(uint64_t f,uint64_t boot,uint64_t tx,const struct r2_group_config *g){
 atomic_set(&observer_ready,0);map_version=0;memset(source_reads,0,sizeof source_reads);memset(sync_map,0,sizeof sync_map);
#ifdef R2_LORA_PASSIVE_SLOW
 memset(tx_due,0,sizeof tx_due);
#endif
 memset(&utc_job,0,sizeof utc_job);utc_retry_us=utc_running_token=0;memset(&r2_diag_scratch.lora,0,sizeof r2_diag_scratch.lora);
 r2_diag_scratch.lora.journal_result=-ENODATA;factory=f;physical_boot=boot;transmitter_factory=tx;group=g;transmitter=f&&f==tx;
 source_tz.value=r2_tz_unknown();owner.boot_ms=(uint32_t)((mono()+999)/1000);if(!owner.boot_ms)owner.boot_ms=1;
 owner.quiet_until_us=mono()+REBOOT_CONTROL_GUARD_US;
#ifdef R2_LORA_PASSIVE_SLOW
 atomic_set(&flags,REQUESTED);lab_until=UINT64_MAX;
#else
 atomic_set(&flags,0);
#endif
 atomic_set(&observer_ready,1);
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
  atomic_set(&faulted,1);r2_lora_hw_stop_begin(mono()+PASSIVE_DRAIN_US);
 }
 if(w->journal_generation&&w->journal_result==-EINPROGRESS){
  int rc=r2_diag_record_result(w->journal_generation);
  if(rc!=-EINPROGRESS){w->journal_result=rc;if(!rc)w->saved=w->submitted;}
 }
 uint32_t elapsed=(uint32_t)(k_uptime_get_32()-w->fault_at);
 bool closure_ready=!w->terminal||(w->closure&15u)==15u||elapsed>=PASSIVE_DRAIN_US/1000u;
 if(closure_ready&&w->serial!=w->saved&&w->serial!=w->submitted&&!r2_diag_busy()){
  uint32_t record[16];unsigned key=irq_lock();
  memcpy(record,w->record,sizeof record);record[5]|=(w->closure&31u)<<20;
  uint32_t serial=w->serial;bool terminal=w->terminal!=0;
  irq_unlock(key);
  uint64_t token=0;int rc=r2_diag_lora(record,terminal,&token);
  if(!rc){w->submitted=serial;w->journal_generation=token;w->journal_result=-EINPROGRESS;}
  else if(rc!=-EAGAIN)w->journal_result=rc;
 }
 /* Never wait indefinitely for RAM snapshot completion. The capture result
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

/* Bounded read-only poll for unattended runs; larger causal audits stay in
 * `lora`. UTC comes from the accepted SYNC event, never host wall time. */
int r2_lora_diagnostics_json(char *out,size_t capacity,int result){
 struct lp_diagnostics d;struct r2_lora_view v;uint8_t owed=0;uint32_t reads[4],sync_stats[4],due[2][4]={{0}};
 k_spinlock_key_t key=k_spin_lock(&lock);d=diagnostics;v=published;memcpy(reads,source_reads,sizeof reads);memcpy(sync_stats,sync_map,sizeof sync_stats);
#ifdef R2_LORA_PASSIVE_SLOW
 memcpy(due,tx_due,sizeof due);
#endif

 bool tx=transmitter;uint32_t manual_jobs=utc_job.admitted,manual_serial=utc_job.manual_serial,manual_stream=utc_job.manual_stream;bool manual_active=utc_active(utc_job.state);atomic_val_t f=atomic_get(&flags);
 uint32_t tx_gate=(!(f&ALLOWED)?4u:0u)|(!(f&REQUESTED)?8u:0u)|((f&PROFILE_RC)?2u:0u)|(!(f&SOURCE_PRESENT)?32u:0u);
#ifdef R2_LORA_PASSIVE_SLOW
 if(!tx)tx_gate|=1u;
 if(tx)owed=role.tx.core.durable.owed;
 uint32_t retries=retry_count;bool inhibited=false;
 if(owed&&mono()<retry_at_us)tx_gate|=1024u;
#else
 uint32_t retries=0;bool inhibited=false;
 if(!tx)tx_gate|=1u;
#endif
 #ifdef R2_LORA_TX_DIAGNOSTIC
 tx_gate&=~32u;
#endif
 if(tx){if(!role.tx.loaded||!role.tx.core.ready)tx_gate|=64u;
  if(d.fh_disabled)tx_gate|=128u;
  if(d.charged>LP_CAP_US||lp_duration(LP_META)+lp_duration(LP_SYNC)+lp_duration(LP_FOLLOW)>LP_CAP_US-d.charged)tx_gate|=256u;}
 if(r2_diag_scratch.lora.attempt.valid&32)tx_gate|=512u;
 k_spin_unlock(&lock,key);
 struct r2_lora_hw_diag h;r2_lora_hw_diag(&h);
 uint64_t sampled=mono();view_advance(&v,sampled);
 bool display_available=v.utc_us&&v.requested&&v.active&&!v.fault;
 uint64_t reference=v.until_us>=LP_HOLD_US?v.until_us-LP_HOLD_US:UINT64_MAX;
 uint64_t age=display_available&&sampled>=reference?(sampled-reference)/1000:UINT64_MAX;
 uint32_t reference_age=age>UINT32_MAX?UINT32_MAX:(uint32_t)age;
 return snprintf(out,capacity,
 "{\"diag\":1,\"tx_diagnostic\":" DIAGNOSTIC_JSON ",\"profile\":\"%s\",\"result\":%d,\"boot\":\"%016llx\",\"at_us\":%llu,"
 "\"transmitter\":%s,\"requested\":%s,\"active\":%s,\"fault\":%s,\"inhibited\":%s,\"error\":%d,\"tx_gate\":%u,"
 "\"emitter\":\"%016llx\",\"emitter_boot\":\"%016llx\",\"stream\":%u,\"source_epoch\":%u,\"capture_epoch\":%llu,"
 "\"serial\":%u,\"cursor\":%u,\"event_mono_us\":%llu,\"event_tick\":%llu,\"event_ns\":%llu,"
 "\"physical_until_us\":%llu,\"meta_until_us\":%llu,\"trained\":%s,\"time_valid\":%s,\"link_fit\":[%u,%u],"
 "\"hold_selected\":%s,\"hold_rates\":[%d,%d,%d,%u,%d,%u],\"hold_model\":[%llu,%llu,%d,%d],\"discovery_us\":%llu,"
 "\"accepted\":%u,\"rejected\":%u,\"missing\":%u,\"sent\":[%u,%u,%u,%u],\"received\":[%u,%u,%u,%u],"
 "\"late\":%u,\"refused\":%u,\"bad\":%u,\"missing_map\":%u,\"storage_fail\":%u,\"retries\":%u,\"owed\":%u,"
 "\"charged_us\":%llu,\"pending\":%u,\"fh_disabled\":%u,\"hw_errors\":[%u,%u,%u,%u,%u],"
 "\"display_available\":%s,\"display_held\":%s,\"display_acquiring\":%s,\"display_utc_us\":%llu,\"display_sampled_us\":%llu,\"display_ref_age_ms\":%u,"
 "\"prediction_window_us\":[%llu,%llu],\"schedule_window_us\":[%llu,%llu,%llu,%llu],\"tx_due_us\":[[%u,%u,%u,%u],[%u,%u,%u,%u]],\"sync_map\":[%u,%u,%u,%u],\"source_read\":[%u,%u,%u,%u],\"manual_jobs\":%u,\"manual_active\":%s,\"manual_serial\":%u,\"manual_stream\":%u,\"scheduler_priority\":[%d,%d,%d],\"beat_us\":%llu,\"meta_us\":%llu,\"hold_us\":%llu,\"lease_us\":%llu,\"cadence_bound_us\":%llu,\"utc_accuracy\":\"UNQUALIFIED\"}",
 LP_PROFILE_NAME,result,(unsigned long long)physical_boot,(unsigned long long)sampled,
 tx?"true":"false",v.requested?"true":"false",v.active?"true":"false",v.fault?"true":"false",inhibited?"true":"false",d.error,tx_gate,
 (unsigned long long)d.emitter,(unsigned long long)d.boot,d.stream,d.source_epoch,(unsigned long long)(tx?0:d.epoch),
 d.serial,d.cursor,(unsigned long long)(tx?0:d.last_us),(unsigned long long)(tx?0:d.last_tick),(unsigned long long)(tx?0:d.last_ns),
 (unsigned long long)(tx?0:d.physical_until),(unsigned long long)(tx?0:d.meta_until),!tx&&d.trained?"true":"false",v.time_valid?"true":"false",v.link_samples,v.link_rms_ns,
 !tx&&d.hold_selected?"true":"false",d.baseline_rate,d.rate,d.hold_short_rate,d.hold_short_guard,d.hold_long_rate,d.hold_long_guard,
 (unsigned long long)d.model_tick,(unsigned long long)d.model_ns,d.rate,d.model_phase,(unsigned long long)(LP_BEAT_US*LP_DISC_BEATS),
 tx?0:d.accepted,tx?0:d.rejected,tx?0:d.missing,d.sent[0],d.sent[1],d.sent[2],d.sent[3],
 tx?0:d.received[0],tx?0:d.received[1],tx?0:d.received[2],tx?0:d.received[3],
 d.late,d.refused,d.bad,d.missing_map,d.storage_fail,retries,owed,(unsigned long long)(tx?d.charged:0),d.pending,tx?d.fh_disabled:0,
 h.crc_bad,h.overflow,h.spi_error,h.cutoffs,h.invalid_capture,
 display_available?"true":"false",v.display_held?"true":"false",v.display_acquiring?"true":"false",(unsigned long long)v.utc_us,(unsigned long long)v.sampled_us,reference_age,
 (unsigned long long)LP_PREDICT_HEADER_WINDOW_US,(unsigned long long)LP_PREDICT_HINT_RETIRE_US,(unsigned long long)LP_SYNC_OFFSET_US,(unsigned long long)LP_FRAME_END_US,(unsigned long long)LP_RX_TUNE_LEAD_US,(unsigned long long)LP_CONTROL_RETIRE_US,due[0][0],due[0][1],due[0][2],due[0][3],due[1][0],due[1][1],due[1][2],due[1][3],sync_stats[0],sync_stats[1],sync_stats[2],sync_stats[3],reads[0],reads[1],reads[2],reads[3],manual_jobs,manual_active?"true":"false",manual_serial,manual_stream,k_thread_priority_get(&worker_thread),WORKER_PRIORITY,DRAIN_PRIORITY,(unsigned long long)LP_BEAT_US,(unsigned long long)(LP_BEAT_US*LP_META_BEATS),(unsigned long long)LP_HOLD_US,(unsigned long long)LP_LEASE_US,(unsigned long long)LP_CADENCE_BOUND_US);
}

int r2_lora_json(char *out,size_t capacity,int result){
 struct lp_diagnostics d;struct r2_lora_view v;struct lp_audit audit[4]={0};uint64_t until;uint32_t reads[4],sync_stats[4],due[2][4]={{0}};
 k_spinlock_key_t key=k_spin_lock(&lock);d=diagnostics;v=published;memcpy(reads,source_reads,sizeof reads);memcpy(sync_stats,sync_map,sizeof sync_stats);
#ifdef R2_LORA_PASSIVE_SLOW
 memcpy(due,tx_due,sizeof due);
#endif
until=lab_until;
#ifdef R2_LORA_PASSIVE_SLOW
 if(audit_kind>=1&&audit_kind<=4)audit[audit_kind-1]=audits[0];
#else
 memcpy(audit,audits,sizeof audit);
#endif
 k_spin_unlock(&lock,key);
 struct r2_lora_hw_diag h;r2_lora_hw_diag(&h);
 struct r2_lora_hw_rejection rejected;r2_lora_hw_rejection(&rejected);
 uint32_t fault_words[16]={0};uint64_t fault_boot=0;unsigned fault_key=irq_lock();
 struct r2_lora_attempt attempt=r2_diag_scratch.lora.attempt;
#ifdef R2_LORA_PASSIVE_SLOW
 /* No slow lab arm or NV timing backend: legacy fields stay explicit zero. */
 const struct r2_lora_store_timing store={0};
 uint32_t startup0=r2_diag_scratch.lora.startup_counts[0],startup1=r2_diag_scratch.lora.startup_counts[1];
#define PASSIVE_DIAGNOSTIC_ARM_US UINT64_C(0)
#define PASSIVE_DIAGNOSTIC_CHARGE_US 0u
#else
 struct r2_lora_store_timing store=r2_diag_scratch.lora.store;
 uint32_t startup0=0,startup1=0;
#define PASSIVE_DIAGNOSTIC_ARM_US r2_diag_scratch.lora.diagnostic.armed_us
#define PASSIVE_DIAGNOSTIC_CHARGE_US r2_diag_scratch.lora.diagnostic.new_charge_us
#endif
 bool checkpoint_dirty=transmitter&&role.tx.dirty;
 if(r2_diag_scratch.lora.record[0]==R2_LORA_FAULT_MAGIC){memcpy(fault_words,r2_diag_scratch.lora.record,sizeof fault_words);fault_boot=physical_boot;}
 else if(r2_diag_scratch.lora.retained.record[0]==R2_LORA_FAULT_MAGIC){memcpy(fault_words,r2_diag_scratch.lora.retained.record,sizeof fault_words);fault_boot=r2_diag_scratch.lora.retained.boot;}
 irq_unlock(fault_key);
 return snprintf(out,capacity,
 "{\"passive\":3,\"runtime_state\":\"RAM_ONLY\",\"tx_diagnostic\":" DIAGNOSTIC_JSON ",\"diagnostic_arm_us\":%llu,\"diagnostic_charge_us\":%u,\"profile\":\"%s\",\"cap_us\":%llu,\"result\":%d,"
 "\"boot\":\"%016llx\",\"configured_tx\":\"%016llx\",\"transmitter\":%s,\"requested\":%s,\"active\":%s,\"closed\":%s,\"fault\":%s,"
 "\"lab_until_us\":%llu,\"quiet_until_us\":%llu,\"stack_unused\":%u,\"error\":%d,"
 "\"emitter\":\"%016llx\",\"emitter_boot\":\"%016llx\",\"stream\":%u,\"source_epoch\":%u,\"root_boot\":\"%016llx\",\"seed\":%u,\"cursor\":%u,\"serial\":%u,"
 "\"event_tick\":%llu,\"event_ns\":%llu,\"capture_epoch\":%llu,\"event_mono_us\":%llu,\"physical_until_us\":%llu,\"meta_until_us\":%llu,"
 "\"trained\":%s,\"link_fit\":[%u,%u],\"rate_ppb\":%d,\"accepted\":%u,\"rejected\":%u,\"missing\":%u,"
 "\"choice\":%u,\"state\":%u,\"time_valid\":%s,\"assurance\":\"FRESHNESS_UNKNOWN\",\"rc7_authority\":false,\"utc_accuracy\":\"UNQUALIFIED\","
 "\"charged_us\":%llu,\"next_slot_us\":%llu,\"pending\":%u,\"fh_disabled\":%u,\"store_sequence\":%u,\"storage_fail\":%u,\"storage_max_us\":%u,"
 "\"refused\":%u,\"late\":%u,\"missing_map\":%u,\"bad\":%u,\"sent\":[%u,%u,%u,%u],\"received\":[%u,%u,%u,%u],"
 "\"at_us\":%llu,\"tx_admission_us\":[%u,%d,%u,%u],\"tx_attempt\":[%u,%u,%u,%u,%u,%d,%d,%u,%u,%u,%u,%u],"
 "\"tx_source_max_us\":[%u,%u],\"tx_reject\":[%u,%u,%llu,%llu,%llu,%u,%u,%u,%u,%u,%u,%u,%u],\"no_rf_refunds\":%u,\"store_timing\":[%u,%u,%u,%u,%u],\"audit\":[[%llu,%llu,%llu,%llu,%u,%u,%u,%u],[%llu,%llu,%llu,%llu,%u,%u,%u,%u],[%llu,%llu,%llu,%llu,%u,%u,%u,%u],[%llu,%llu,%llu,%llu,%u,%u,%u,%u]],"
 "\"rf_window_us\":[%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu],\"prediction_window_us\":[%llu,%llu],\"schedule_window_us\":[%llu,%llu,%llu,%llu],\"tx_due_us\":[[%u,%u,%u,%u],[%u,%u,%u,%u]],\"sync_map\":[%u,%u,%u,%u],\"source_read\":[%u,%u,%u,%u],\"owner\":[%u,%u,%u],\"startup\":[%u,%u],\"hw\":[%u,%u,%u,%u,%u,%u,%u,%d,%u,%u,%u],\"drain_result\":%d,"
 "\"fault_boot\":\"%016llx\",\"checkpoint_dirty\":%s,\"fault_words\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]}",
 (unsigned long long)PASSIVE_DIAGNOSTIC_ARM_US,PASSIVE_DIAGNOSTIC_CHARGE_US,LP_PROFILE_NAME,(unsigned long long)LP_CAP_US,result,(unsigned long long)physical_boot,(unsigned long long)transmitter_factory,transmitter?"true":"false",v.requested?"true":"false",v.active?"true":"false",atomic_get(&closed)?"true":"false",atomic_get(&faulted)?"true":"false",
 (unsigned long long)until,(unsigned long long)owner.quiet_until_us,stack_unused(),d.error,
 (unsigned long long)d.emitter,(unsigned long long)d.boot,d.stream,d.source_epoch,(unsigned long long)(transmitter?d.tx_root_boot:d.root_boot),d.seed,d.cursor,d.serial,
 (unsigned long long)(transmitter?0:d.last_tick),(unsigned long long)(transmitter?0:d.last_ns),(unsigned long long)(transmitter?0:d.epoch),(unsigned long long)(transmitter?0:d.last_us),(unsigned long long)(transmitter?0:d.physical_until),(unsigned long long)(transmitter?0:d.meta_until),
 d.trained?"true":"false",v.link_samples,v.link_rms_ns,transmitter?0:d.rate,d.accepted,d.rejected,d.missing,v.choice,v.state,v.time_valid?"true":"false",
 (unsigned long long)(transmitter?d.charged:0),(unsigned long long)(transmitter?d.next_slot:0),d.pending,d.fh_disabled,d.store_sequence,d.storage_fail,transmitter?d.storage_max_us:0,d.refused,d.late,d.missing_map,d.bad,
 d.sent[0],d.sent[1],d.sent[2],d.sent[3],transmitter?0:d.received[0],transmitter?0:d.received[1],transmitter?0:d.received[2],transmitter?0:d.received[3],
 (unsigned long long)mono(),transmitter?d.reserve_lead_us:0,transmitter?d.launch_late_us:0,transmitter?d.source_room_us:0,transmitter?d.source_phase_us:0,
 attempt.valid,attempt.cursor,attempt.serial,attempt.prepare_us,attempt.commit_us,attempt.postcommit_lead_us,attempt.first_due_late_us,
 attempt.source_room_us,attempt.source_phase_us,attempt.prepare_max_us,attempt.commit_max_us,attempt.source_revision,
 d.max_source_room_us,d.max_source_phase_us,rejected.reason,rejected.point,(unsigned long long)rejected.at_us,(unsigned long long)rejected.until_tick,(unsigned long long)rejected.finish_us,
 rejected.available_us,rejected.required_us,rejected.finish_room_us,rejected.source_count,rejected.finish_count,rejected.generation,rejected.no_rf?1:0,d.reject_phase_us,d.no_rf_refunds,store.sequence,store.phase,store.body_us,store.verify_us,store.commit_us,
#define AUDIT_ARGS(i) (unsigned long long)audit[i].tick,(unsigned long long)audit[i].epoch,(unsigned long long)audit[i].start_us,(unsigned long long)audit[i].end_us,audit[i].cursor,audit[i].phy,audit[i].channel,audit[i].valid
 AUDIT_ARGS(0),AUDIT_ARGS(1),AUDIT_ARGS(2),AUDIT_ARGS(3),
#undef AUDIT_ARGS
 (unsigned long long)LP_LAUNCH_LATE_US,(unsigned long long)LP_LAUNCH_PAD_US,(unsigned long long)LP_FOLLOW_OFFSET_US,(unsigned long long)LP_FOLLOW_LATEST_US,(unsigned long long)LP_HEADER_WINDOW_US,(unsigned long long)LP_HINT_RETIRE_US,(unsigned long long)LP_PENDING_US,(unsigned long long)LP_CONTROL_PROCESS_US,(unsigned long long)LP_PREDICT_HEADER_WINDOW_US,(unsigned long long)LP_PREDICT_HINT_RETIRE_US,(unsigned long long)LP_SYNC_OFFSET_US,(unsigned long long)LP_FRAME_END_US,(unsigned long long)LP_RX_TUNE_LEAD_US,(unsigned long long)LP_CONTROL_RETIRE_US,due[0][0],due[0][1],due[0][2],due[0][3],due[1][0],due[1][1],due[1][2],due[1][3],sync_stats[0],sync_stats[1],sync_stats[2],sync_stats[3],reads[0],reads[1],reads[2],reads[3],d.stage,d.phy,d.channel,startup0,startup1,
 h.rx_done,h.tx_done,h.crc_bad,h.overflow,h.spi_error,h.cutoffs,h.invalid_capture,h.error,h.phase,h.launch_aborts,h.busy_aborts,r2_lora_close_result(),
 (unsigned long long)fault_boot,checkpoint_dirty?"true":"false",fault_words[0],fault_words[1],fault_words[2],fault_words[3],fault_words[4],fault_words[5],fault_words[6],fault_words[7],fault_words[8],fault_words[9],fault_words[10],fault_words[11],fault_words[12],fault_words[13],fault_words[14],fault_words[15]);
}

#undef PASSIVE_DIAGNOSTIC_ARM_US
#undef PASSIVE_DIAGNOSTIC_CHARGE_US

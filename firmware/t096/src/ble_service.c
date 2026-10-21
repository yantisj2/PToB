#include "cleanup_budget.h"
/* SPDX-License-Identifier: Apache-2.0 */
#include "ble_service.h"
#include "group_admission.h"
#include "gm_diagnostics.h"
#include "build_identity.h"
#include "runtime_profile.h"
#include "rc_shadow.h"
#include "ble_transport.h"
#include "owner_budget.h"
#include <stdio.h>
#include "group_precision.h"
#include "gm_role_status.h"
#ifdef R2_LORA
#include "lora_service.h"
#endif
#if defined(CONFIG_BT_PER_ADV)
#include "radio_service.h"
#include "time_service.h"
#include "capture.h"
static struct r2_source_clock source_input,source_work;
#endif
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/atomic.h>
#define R2_LOCK_UNIT 2u
#include "health_lock.h"
static struct r2_network net;
static struct r2_clock input;
static struct r2_service_status status;
static struct r2_enrollment roster;
/* Thread-only mailbox. Never acquired by ISR or Bluetooth callbacks.
 * Service -> PT is the only nested mutex order; PT never acquires service. */
K_MUTEX_DEFINE(lock);
/* Public controller operations remain serialized, but cannot monopolize the
 * cooperative system queue. Main7, UI6 and precision8 can preempt this control owner9. */
K_THREAD_STACK_DEFINE(owner_stack,8192);
static struct k_work_q owner_queue;
static atomic_t stop_requested,busy;
static bool used,started,transport_stopped;
static uint64_t close_deadline;
static uint64_t physical_boot,profile_identity,profile_reset_until;
static bool profile_rc,profile_target_rc,profile_pending,profile_supported;
static uint8_t profile_source_caps;
static uint32_t profile_changes;
static unsigned profile_phase;
static int profile_restart(uint64_t now);
static void service_stop_owned(bool migration);
static struct r2_local_demand local_demand;
static bool relay_selected,relay_transition,relay_profile_set,relay_retiring;
static uint64_t relay_activation_until,relay_transition_until,direct_check_due;
static void normal_relay_tick(struct r2_radio_scope scope,uint64_t now,int64_t utc);
static char pending_name[25];static bool name_pending;
static uint32_t rx_latency_max_us,rx_queue_peak;
static uint64_t now_us(void){return k_ticks_to_us_floor64(k_uptime_ticks());}
static uint8_t owner_tx_peers(unsigned self,unsigned count,unsigned *cursor,uint8_t ready)
{
 if(!count)return 0;
 uint8_t available=0;unsigned selected=0;
 for(unsigned offset=0;offset<count&&selected<R2_OWNER_CONTROL_TX;offset++){
  unsigned p=(*cursor+offset)%count;
  if(p!=self&&(ready&(1u<<p))){available|=1u<<p;selected++;}
 }
 *cursor=(*cursor+1)%count;return available;
}
#if defined(R2_GM5_NATIVE)
_Static_assert(sizeof(struct r2_gm5)+sizeof(struct r2_gm5_diagnostic)<=1712u,"G5 state must fit two retired728B peers plus256B of native ACK-ring savings");
static struct r2_gm5_diagnostic gm_diagnostic;
void r2_gm_diagnostic_snapshot(struct r2_gm5_diagnostic *out){
 (void)k_mutex_lock(&lock,K_FOREVER);*out=gm_diagnostic;(void)k_mutex_unlock(&lock);
}
#endif
static void publish(uint64_t now)
{
#ifdef R2_LORA
 r2_lora_mesh_contact(r2_network_mesh_contact(&net,now)||r2_group_admission_mesh_contact(now));
#endif
#if defined(CONFIG_BT_PER_ADV)
 struct r2_radio_precision_view precision_view;struct r2_relay_status relay_view;
 relay_view=(struct r2_relay_status){0};
 if(relay_selected)r2_relay_service_snapshot(&relay_view,&precision_view,now);else r2_radio_service_view(&precision_view,now);
 struct r2_link_quality_view reception;
 if(relay_selected)r2_relay_service_link_quality(&reception);else r2_radio_service_link_quality(&reception);
#endif
 struct v12_dc_snapshot view={.elected_root=255,.hot_standby=255};
 r2_network_snapshot(&net,&view);
#if defined(CONFIG_BT_PER_ADV)
 /* CONTROL owns net. Finish nested snapshots before masking radio IRQs. */
 struct r2_group_client public;r2_radio_service_client_state(&public,now);
#endif
#ifdef R2_LORA
 /* Only an authenticated peer's advertised dataset is descriptive parent
  * quality. The service matches exact factory+boot to the frozen source. */
 for(unsigned i=0;i<net.count;i++)if(net.peer[i].connected&&net.peer[i].authenticated&&net.peer[i].map_valid&&
  net.peer[i].quality.identity==net.peer[i].identity&&now>=net.peer[i].seen&&now-net.peer[i].seen<10000000)
  r2_lora_parent_quality(net.peer[i].identity,net.peer[i].boot,net.peer[i].quality.clock_class,net.peer[i].quality.accuracy,net.peer[i].seen);
#endif
 bool storage_ready=r2_network_storage_ready(&net);
 uint32_t hot_refusal=r2_network_hot_refusal(&net);
 struct r2_transport_fault transport_fault;r2_ble_fault_snapshot(&transport_fault);
 uint32_t backlog=r2_ble_backlog();
 uint8_t lamps[8];r2_ble_lamps(lamps,now);
 uint64_t direct_path[4];r2_group_admission_path_view(direct_path);
 (void)k_mutex_lock(&lock,K_FOREVER);
#if defined(R2_GM5_NATIVE)
 gm_diagnostic.state=net.gm5;gm_diagnostic.at=now;gm_diagnostic.boot=net.boot;
 gm_diagnostic.source_until=net.candidate.valid_until_us;gm_diagnostic.source_epoch=net.candidate.generation;
 gm_diagnostic.root_health_at=net.root_health_at;gm_diagnostic.output_not_before=net.fence>net.takeover_not_before?net.fence:net.takeover_not_before;gm_diagnostic.refusal=hot_refusal;gm_diagnostic.pair_generation=net.management_generation;
 gm_diagnostic.physical_eligible=net.admission.eligible;gm_diagnostic.global_hot_known=net.gm5.role==R2_GM5_ACTIVE;
 gm_diagnostic.global_hot=gm_diagnostic.global_hot_known&&net.hot_ready;
 gm_diagnostic.local_hot=net.hot_ready&&net.coordinator==net.self;
 gm_diagnostic.serving=!profile_rc&&!profile_pending&&started&&!atomic_get(&stop_requested)&&view.local_gm_ready;
 gm_diagnostic.map_valid=precision_view.map_valid;gm_diagnostic.locked=precision_view.locked;
#endif
 status.gm_cold=net.gm5.enabled&&net.gm5.role==R2_GM5_COLD;
 memcpy(status.direct_path,direct_path,sizeof direct_path);
 #if defined(CONFIG_BT_PER_ADV)
 status.profile_rc=profile_rc;status.profile_target_rc=profile_target_rc;status.profile_pending=profile_pending;status.profile_phase=profile_phase;status.profile_changes=profile_changes;status.profile_incarnation=net.boot;
 status.precision=precision_view;
 status.reception=reception;
 status.relay=relay_view;status.relay_pending=relay_transition;status.activation_until=relay_activation_until;
#endif
 status.field_profile=false;
 status.membership_mask=0;
 for(unsigned i=0;i<net.count;i++)if(r2_network_member(&net,i))status.membership_mask|=1u<<i;
 status.standby_authorized=net.standby_authorized;status.takeover_pending=net.takeover_pending;
 status.root_health_at=net.root_health_at;status.takeover_started=net.takeover_started;status.takeover_not_before=net.takeover_not_before;
 status.group_id=roster.group.id;status.key_epoch=roster.group.epoch;status.authenticated_mask=0;
 for(unsigned i=0;i<net.count;i++)if(net.peer[i].authenticated)status.authenticated_mask|=1u<<i;
 memcpy(status.identities,roster.identity,sizeof status.identities);memcpy(status.name,net.name,sizeof status.name);
 memset(status.peer_names,0,sizeof status.peer_names);
 for(unsigned i=0;i<net.count;i++)memcpy(status.peer_names[i],net.peer[i].name,25);
 memcpy(status.lamps,lamps,8);status.at=now;status.self=net.self;status.coordinator=net.ready?net.coordinator:255;
 status.root=view.elected_root;status.standby=view.hot_standby;status.members=view.members;status.phase=view.phase;
 status.quality=net.quality;status.source_eligible=net.admission.eligible;status.gm_capable=!!(net.gm_mask&(1u<<net.self));status.hot_ready=net.hot_ready;
 uint64_t admission_age=net.admission.observing?now-net.admission.stable_at:0;
 status.admission_ms=admission_age>=R2_ELECTION_STABLE_US?0:(uint32_t)((R2_ELECTION_STABLE_US-admission_age+999)/1000);
 status.role_until=view.role_until_us;status.lease_expired=view.lease_expired;status.core_refusal=view.last_refusal;
#if defined(R2_RC_DYNAMIC_FIXTURE)
 status.fixture_pair_root_identity=net.pair.root<net.count?roster.identity[net.pair.root]:0;
 status.fixture_pair_standby_identity=net.pair.standby<net.count?roster.identity[net.pair.standby]:0;
 status.fixture_pair_root_boot=net.pair.root_boot;status.fixture_pair_standby_boot=net.pair.standby_boot;
#endif
 status.standby_boot=net.coordinator_boot;status.pair_until=net.pair.until;status.pair_epoch=net.pair.epoch;status.management_until=net.management_until;status.source_evidence=net.view.evidence_us;
 status.hot_withdrawals=net.hot_withdrawals;status.hot_withdrawal_at=net.hot_withdrawal_at;status.hot_withdrawal_reason=net.hot_withdrawal_reason;
 status.hot_refusal=hot_refusal;status.record_age_us=net.record_age_us;status.record_age_max_us=net.record_age_max_us;status.rx_latency_max_us=rx_latency_max_us;status.rx_queue_peak=rx_queue_peak;status.backlog=backlog;
 status.hello_map_refusals=net.hello_map_refusals;status.hello_width_us=net.hello_width_us;
 status.authority_until=net.service_until;
 status.root_boot=view.root_boot;status.source_epoch=view.coarse.generation;status.service_until=view.service_until_us;status.service_receipts=view.service_receipts;status.rx=r2_ble_receipts();status.authenticated=net.authenticated_rx;status.rejected=net.rejected;
 status.drops=r2_ble_drops();status.errors=r2_ble_errors();status.transport_fault=transport_fault;
 status.serving=!profile_rc&&!profile_pending&&started&&!atomic_get(&stop_requested)&&view.local_gm_ready;
 status.storage_window=storage_ready;
 status.peer_delivery_confirmed=status.serving&&view.serving;
 status.follower=started&&!atomic_get(&stop_requested)&&net.follower_valid;
 status.follower_clock_valid=net.follower_clock_valid;status.follower_utc=net.follower_utc;
#if defined(CONFIG_BT_PER_ADV)
 status.group_member=false;
 for(unsigned p=0;p<net.count;p++){if(r2_network_member(&net,p))status.group_member=true;}
 status.public_client=public.passive;
 if(public.passive){
  status.serving=false;status.hot_ready=false;status.standby=status.coordinator=255;
 }
#endif
 (void)k_mutex_unlock(&lock);
}
/* Caller owns lock; preserve the first supervisor failure before teardown. */
static void supervisor_fault(unsigned site,uint64_t now)
{
 struct r2_supervision *d=&status.supervision;
 if(!d->fault_site){d->fault_site=site;d->fault_at=now;d->fault_submitted=d->submitted;d->fault_started=d->started;d->fault_state=d->state;d->fault_stage=d->stage;d->fault_stage_at=d->stage_at;}
}
static void worker_begin(void)
{
 (void)k_mutex_lock(&lock,K_FOREVER);uint64_t now=now_us();
 struct r2_supervision *d=&status.supervision;
 uint64_t age=now>=d->submitted?now-d->submitted:0;
 uint32_t bounded=age>UINT32_MAX?UINT32_MAX:(uint32_t)age;
 if(bounded>d->queue_max_us)d->queue_max_us=bounded;
 if(age>=100000&&d->late_queues!=UINT32_MAX)d->late_queues++;
 d->started=d->stage_at=now;d->state=2;d->stage=1;(void)k_mutex_unlock(&lock);
}
/* Seven bounded wall-time stages, including interrupt preemption. No ISR
 * formatting or snapshots. Stage7 includes polling and the final owner tail. */
static void worker_stage_locked(uint64_t now,unsigned next)
{
 struct r2_supervision *d=&status.supervision;
 uint64_t elapsed=now>=d->stage_at?now-d->stage_at:0;
 uint32_t bounded=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
 if(d->stage&&d->stage<=7&&bounded>d->stage_max_us[d->stage-1])d->stage_max_us[d->stage-1]=bounded;
 d->stage=next;d->stage_at=now;
}
static void worker_stage(unsigned next)
{
 (void)k_mutex_lock(&lock,K_FOREVER);worker_stage_locked(now_us(),next);(void)k_mutex_unlock(&lock);
}
static void worker_end(void)
{
 (void)k_mutex_lock(&lock,K_FOREVER);uint64_t now=now_us();
 struct r2_supervision *d=&status.supervision;worker_stage_locked(now,0);
 uint64_t elapsed=now>=d->started?now-d->started:0,total=now>=d->submitted?now-d->submitted:0;
 uint32_t bounded=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
 if(bounded>status.worker_max_us)status.worker_max_us=bounded;
 bounded=total>UINT32_MAX?UINT32_MAX:(uint32_t)total;
 if(bounded>d->owner_max_us)d->owner_max_us=bounded;
 d->state=0;atomic_clear(&busy);(void)k_mutex_unlock(&lock);
}
static void cycle(struct k_work *work)
{
 worker_begin();
#if defined(CONFIG_BT_PER_ADV)
 struct r2_radio_precision_view timing;
#endif
 r2_ble_owner_deadline(0); /* Each control request owns its own bounded lifetime. */
 uint64_t now=now_us();
 (void)k_mutex_lock(&lock,K_FOREVER);
 if(name_pending){memcpy(net.name,pending_name,sizeof net.name);name_pending=false;for(unsigned p=0;p<net.count;p++)net.peer[p].next_hello=0;}
 (void)k_mutex_unlock(&lock);
 if(atomic_get(&stop_requested)){
#ifdef R2_LORA
  r2_lora_origin(false,0,0);
#endif
  if(!transport_stopped){r2_network_stop(&net,now);r2_ble_stop();transport_stopped=true;}
#if defined(CONFIG_BT_PER_ADV)
  r2_radio_service_stop(now);r2_relay_service_stop(now);
#endif
  r2_ble_poll(now);
  if(r2_ble_stop_error()){(void)k_mutex_lock(&lock,K_FOREVER);status.error=r2_ble_stop_error();(void)k_mutex_unlock(&lock);}
  if(r2_ble_closed()
#if defined(CONFIG_BT_PER_ADV)
     &&r2_radio_service_closed()&&r2_relay_service_closed()
#endif
  ){
   (void)k_mutex_lock(&lock,K_FOREVER);
   /* A proved close is a latched acknowledgement of this stop, including its
    * original deadline. A later profile restart may run this owner again;
    * it must not turn the earlier successful close into a stale timeout. */
   if(!status.closed){
    status.closed=true;status.closed_at=now;status.active=false;
    if(now>=close_deadline&&!status.error)status.error=-ETIMEDOUT;
   }
   (void)k_mutex_unlock(&lock);
  }
 }else{
  struct r2_group_client public;r2_group_admission_client(&public);
  if(net.gm5.enabled){
#ifdef R2_LORA
   struct r2_lora_view bridge_view;r2_lora_view(&bridge_view);net.gm5.bridge=r2_gm5_bridge_needed(&net.gm5,bridge_view.transmitter,bridge_view.requested);
#endif
   r2_group_admission_gm5_policy(&net,now);
  }
  r2_ble_serving(net.local_gm&&net.ready&&now<net.service_until);
  bool cold=net.gm5.enabled&&net.gm5.role==R2_GM5_COLD&&!net.gm5.bridge;
  if(!cold)r2_ble_client_window(profile_supported||!(roster.group.id&&(public.passive||!(net.capabilities&1)))||r2_ble_client_window_open(now,400000));
  r2_ble_poll(now);
  r2_ble_client_seek(now,!net.gm5.enabled&&public.passive&&!public.source);
  if(r2_ble_stop_error())r2_service_stop();
  for(unsigned p=0;p<net.count;p++)if(p!=net.self){
   if(roster.group.id){uint8_t address[6];uint64_t id=r2_ble_peer_identity(p,address);
    if(id!=net.peer[p].identity){
     r2_network_link(&net,p,false,now_us());memset(&net.peer[p],0,sizeof net.peer[p]);
     net.peer[p].identity=id;roster.identity[p]=id;memcpy(roster.address[p],address,6);
     r2_group_pair_key(net.peer[p].tx_key,&roster.group,net.identity,id);r2_group_pair_key(net.peer[p].rx_key,&roster.group,id,net.identity);
    }
    roster.identity[p]=id;memcpy(roster.address[p],address,6);
   }
   r2_ble_peer_authenticated(p,net.peer[p].authenticated);
   r2_network_link(&net,p,r2_ble_ready(p),now_us());
  }
#if defined(CONFIG_BT_PER_ADV)
  if(roster.group.id)r2_radio_service_roster(&roster);
#endif
  worker_stage(2);
  unsigned queued=r2_ble_backlog()&255;if(queued>rx_queue_peak)rx_queue_peak=queued;
  if(roster.group.id){
   uint64_t check_at=now_us();struct r2_group_client context={0};
   if(relay_selected){struct r2_relay_status r;struct r2_radio_precision_view v;r2_relay_service_snapshot(&r,&v,check_at);
    context=(struct r2_group_client){.passive=true,.source=r.map_valid,.root=r.root_identity,.peer=r.parent_identity,.boot=r.root_boot,.source_epoch=r.source_epoch,.publication=r.upstream_train,.utc_us=r.utc_us,.at=r.sampled_us,.until=r.source_until_us,.map_until=r.valid_until_us};
    if(local_demand.received){struct r2_link_quality_view q;r2_relay_service_link_quality(&q);r2_group_admission_direct_quality(&q,check_at);}
   }else r2_radio_service_context(&context,check_at);
   uint32_t received;if(!profile_rc&&r2_radio_service_direct_opportunity(&received))r2_group_admission_direct_opportunity(received,check_at);
   if(!relay_selected){struct r2_link_quality_view q;r2_radio_service_link_quality(&q);r2_group_admission_direct_quality(&q,check_at);}
   r2_group_admission_timing(&context);
   r2_group_admission_relay_ready(relay_selected&&local_demand.received&&r2_relay_service_serving(check_at));
  }
  r2_group_admission_tick(&net,now_us());
  struct r2_ble_event event;
  for(unsigned k=0;k<R2_OWNER_CONTROL_RX&&r2_ble_receive(&event);k++){
   if(event.peer==r2_ble_admission_peer()){r2_group_admission_receive(&net,&event,now_us());continue;}
   if(event.peer<net.count){
   uint64_t processed=now_us();uint64_t latency=processed>=event.at?processed-event.at:0;
   if(latency>rx_latency_max_us)rx_latency_max_us=latency>UINT32_MAX?UINT32_MAX:(uint32_t)latency;
   r2_network_receive(&net,event.peer,event.data,event.length,event.at,processed);
  }
  }
  worker_stage(3);
  struct r2_clock clock;
  (void)k_mutex_lock(&lock,K_FOREVER);clock=input;
#if defined(CONFIG_BT_PER_ADV)
  source_work=source_input;
#endif
  (void)k_mutex_unlock(&lock);
  now=now_us();r2_clock_advance(&clock,(int64_t)now);
#if defined(CONFIG_BT_PER_ADV)
  struct r2_capture captured;uint64_t relative,generation;
  net.relative_source=r2_capture_now(&captured)&&r2_source_clock_read(&source_work,captured,&relative,&generation);
  if(!profile_rc){
    int64_t utc;uint64_t sampled=now_us();
    net.relative_source=net.relative_source&&r2_source_utc_read(&source_work,captured,clock.view.source_epoch,&utc);
    if(net.relative_source&&sampled>=now&&sampled-now<=100){
      clock.translated_utc_us=utc/1000;clock.translated_at_us=(int64_t)(now+(sampled-now)/2);
      clock.translated_epoch=clock.view.source_epoch;}
    if(clock.translated_epoch!=clock.view.source_epoch)net.relative_source=false;
    /* Network time must be at or after the coherent observation. */
    now=sampled;r2_clock_advance(&clock,(int64_t)now);
  }else clock.translated_epoch=0;
#endif
#ifdef R2_LORA
  struct r2_gm_coordination coordination;r2_lora_coordination(&coordination);
  coordination.direct=r2_group_admission_coordination_direct(&coordination,now);
  r2_network_tick_coordinated(&net,&clock,now,&coordination);
  r2_lora_origin(!profile_rc&&!profile_pending&&net.local_gm&&net.ready&&now<net.service_until,net.boot,net.candidate.generation);
#else
  r2_network_tick(&net,&clock,now);
#endif
  r2_ble_topology(net.coordinator,net.members_mask);
#if defined(CONFIG_BT_PER_ADV)
  worker_stage(4);
  r2_group_admission_client(&public);
  if(net.gm5.enabled&&net.gm5.role==R2_GM5_COLD&&!net.gm5.bridge)public=(struct r2_group_client){.passive=true};
  r2_radio_service_client(&public,&roster,now);
  struct v12_dc_snapshot authority={0};r2_network_snapshot(&net,&authority);
  struct r2_radio_scope scope={.boot=authority.root_boot,.source_epoch=authority.coarse.generation,.publication_epoch=net.publication,.root=authority.elected_root};
  if(authority.relative_precision_permitted){
   scope.gm=authority.local_gm_ready;scope.follower=net.follower_valid&&net.follower_clock_valid&&!scope.gm;
   scope.until_us=scope.gm?authority.coarse.valid_until_us:authority.service_until_us;
  }
  int64_t utc=scope.follower?(int64_t)net.follower_utc:clock.view.utc_us;
  const struct r2_role_record role=r2_gm_role_status(&net,roster.identity,scope.gm,
    public.passive,scope.until_us,scope.source_epoch);
  r2_radio_service_role(&role,&roster.group);
  normal_relay_tick(scope,now,utc);
  if(relay_selected){struct r2_relay_status route;r2_relay_service_snapshot(&route,&timing,now);}else r2_radio_service_view(&timing,now);
  r2_network_timing(&net,scope.boot,scope.source_epoch,timing.train,scope.gm,timing.authorized&&(scope.gm?timing.rms_valid:timing.map_valid),now);
  if(r2_radio_service_error()){(void)k_mutex_lock(&lock,K_FOREVER);status.error=r2_radio_service_error();(void)k_mutex_unlock(&lock);r2_service_stop();}
#endif
  worker_stage(5);
  /* Fair bounded peer attempts: a busy low slot cannot starve other peers.
   * Only queue processing changes; message timestamps/authority remain original. */
  static unsigned tx_cursor;uint8_t ready=0;
  for(unsigned p=0;p<net.count;p++)if(p!=net.self&&r2_ble_ready(p))ready|=1u<<p;
  uint8_t available=owner_tx_peers(net.self,net.count,&tx_cursor,ready);
  for(unsigned k=0;k<R2_OWNER_CONTROL_TX&&!atomic_get(&stop_requested);k++){
   uint8_t peer,wire[R2_GROUP_CONTROL_MAX];bool hello;
   if(!r2_network_next_ready(&net,available,&peer,wire,&hello))break;
   available&=~(1u<<peer); /* one attempt per selected peer, including failure */
   int rc=r2_ble_send(peer,wire);
   if(rc)continue;
   r2_network_sent(&net,hello,peer,wire);
  }
 }
 if(profile_pending&&atomic_get(&stop_requested)){
  int profile_result=profile_restart(now_us());
  if(profile_result&&profile_result!=-EAGAIN){(void)k_mutex_lock(&lock,K_FOREVER);if(!status.error)status.error=profile_result;profile_phase=4;profile_pending=false;(void)k_mutex_unlock(&lock);}
 }
 worker_stage(6);now=now_us();publish(now);
 (void)k_mutex_lock(&lock,K_FOREVER);
 if(status.cycles!=UINT32_MAX)status.cycles++;
 (void)k_mutex_unlock(&lock);worker_stage(7);worker_end();
 /* Relinquish after each control batch; the timer coalesces further work. */
 k_sleep(K_MSEC(1));
}
K_WORK_DEFINE(service_work,cycle);
/* Timer ISR publishes a bounded wakeup only. The independent priority5
 * supervisor owns mutex access and checks the original absolute deadlines. */
K_SEM_DEFINE(supervise_requested,0,1);
K_THREAD_STACK_DEFINE(supervisor_stack,2048);
static struct k_thread supervisor_thread;
static void supervise_timer(struct k_timer *timer)
{
 k_sem_give(&supervise_requested);
}
K_TIMER_DEFINE(service_timer,supervise_timer,NULL);
static void supervisor_owner(void *a,void *b,void *c)
{
 for(;;){
  k_sem_take(&supervise_requested,K_FOREVER);
  r2_service_supervise();
  (void)k_mutex_lock(&lock,K_FOREVER);bool closed=status.closed,pending=profile_pending;(void)k_mutex_unlock(&lock);
  if(closed&&!pending)k_timer_stop(&service_timer);
 }
}
int r2_service_configure(const struct r2_identity *id,const struct r2_enrollment *e)
{
 if(e&&e->field_profile)return -ENOTSUP;
 if(used||status.configured||!id||id->result||!id->boot||!e||!e->map_bound_us||e->map_bound_us>50000)return -EINVAL;
 uint64_t silicon=strtoull(id->silicon,NULL,16);unsigned self=4;
 roster=*e;roster.local_capabilities&=7;
 physical_boot=id->boot;profile_identity=silicon;profile_source_caps=e->local_capabilities&3;profile_supported=e->group.id&&(profile_source_caps&1);profile_rc=profile_target_rc=profile_supported&&r2_profile_default_rc(silicon);
#if defined(R2_SWITCHABLE_FIELD)
 profile_rc=profile_target_rc=false; /* Wrapper RC uses its separate relay-only receiver. */
#endif
 if(e->group.id){
  if(!r2_group_valid(&e->group)||!silicon)return -EACCES;
  self=0;roster.count=4;memset(roster.identity,0,sizeof roster.identity);memset(roster.address,0,sizeof roster.address);memset(roster.key,0,sizeof roster.key);
  roster.identity[0]=silicon;uint8_t derived[16];r2_group_pair_key(derived,&e->group,silicon,silicon);memcpy(roster.address[0],derived,6);roster.address[0][5]|=0xc0;
  if(e->local_capabilities>7)return -EINVAL;
  roster.local_capabilities=profile_supported?(profile_rc?(profile_source_caps&2)|4:profile_source_caps):profile_source_caps;roster.gm_mask=profile_rc?0:(profile_source_caps&1);roster.gnss_mask=(profile_source_caps&2)?1:0;
 }else{
  if(e->count<2||e->count>4)return -EINVAL;
  for(unsigned p=0;p<e->count;p++){
   if(!e->identity[p]||(e->address[p][5]&0xc0)!=0xc0)return -EINVAL;
   if(e->identity[p]==silicon)self=p;
   for(unsigned q=0;q<p;q++)if(e->identity[p]==e->identity[q]||!memcmp(e->address[p],e->address[q],6))return -EINVAL;
   for(unsigned q=0;q<e->count;q++)if(p!=q){unsigned bits=0;for(unsigned k=0;k<16;k++)bits|=e->key[p][q][k];if(!bits)return -EACCES;}
  }
 }
 if(self>=roster.count)return -EACCES;
 bt_addr_le_t address={.type=BT_ADDR_LE_RANDOM};memcpy(address.a.val,roster.address[self],6);
 int rc=bt_id_create(&address,NULL);if(rc<0)return rc;
 rc=r2_network_init(&net,self,roster.group.id?R2_NET_PEERS:roster.count,silicon,id->boot,id->boot^UINT64_C(0x52424c453031),roster.map_bound_us,now_us());if(rc)return rc;
 net.group=roster.group;net.capabilities=e->group.id?roster.local_capabilities:3;
#if defined(R2_GM5_NATIVE) && !defined(R2_PC_PROFILE)
 net.gm5.enabled=roster.group.id&&(net.capabilities&1)&&!profile_rc;
#ifdef R2_LORA
 struct r2_lora_view bridge_view;r2_lora_view(&bridge_view);net.gm5.bridge=r2_gm5_bridge_needed(&net.gm5,bridge_view.transmitter,bridge_view.requested);
#endif
#endif
 r2_ble_gm5_policy(net.gm5.enabled,0,true);
 r2_ble_group_client(roster.group.id&&!(net.capabilities&1));
 r2_ble_group_relay(profile_rc);
 r2_radio_service_continuous(profile_supported);
 snprintf(net.name,sizeof net.name,"Clock-%016llx",(unsigned long long)silicon);
 r2_network_capabilities(&net,roster.gm_mask,roster.gnss_mask);

 for(unsigned p=0;p<net.count;p++){
  net.peer[p].identity=roster.identity[p];memcpy(net.peer[p].tx_key,roster.key[self][p],16);memcpy(net.peer[p].rx_key,roster.key[p][self],16);
 }
#if defined(CONFIG_BT_PER_ADV)
 rc=r2_radio_service_init(self,&roster);if(rc)return rc;
 rc=r2_relay_service_init(self,&roster,id->boot,NULL);if(rc)return rc;
#endif
 k_work_queue_start(&owner_queue,owner_stack,K_THREAD_STACK_SIZEOF(owner_stack),9,NULL);
 k_thread_name_set(&owner_queue.thread,"r2_control");
 rc=r2_ble_owner_bind(k_work_queue_thread_get(&owner_queue));if(rc)return rc;
 k_thread_create(&supervisor_thread,supervisor_stack,K_THREAD_STACK_SIZEOF(supervisor_stack),supervisor_owner,NULL,NULL,NULL,5,0,K_NO_WAIT);
 k_thread_name_set(&supervisor_thread,"r2_supervisor");
 status.profile_rc=profile_rc;status.profile_target_rc=profile_rc;status.profile_incarnation=id->boot;
 status.configured=true;status.self=self;status.root=status.coordinator=status.standby=255;status.closed=true;
 return 0;
}
void r2_service_clock(const struct r2_clock *clock)
{
 (void)k_mutex_lock(&lock,K_FOREVER);input=*clock;
#if defined(CONFIG_BT_PER_ADV)
 r2_time_source_snapshot(&source_input);
 r2_radio_service_source(&source_input,!clock->stopped&&!clock->fatal&&(clock->view.source_available||(clock->view.valid&&clock->view.fix_active&&clock->view.pps_present)),clock->view.source_epoch);
#endif
 /* Withdraw the published serving claim as soon as the sole clock owner
  * reports loss; the serialized core processes that input on its next tick. */
 if(clock->stopped||clock->fatal||(!clock->view.source_available&&
    !(clock->view.valid&&clock->view.fix_active&&clock->view.pps_present)))status.serving=false;
 (void)k_mutex_unlock(&lock);
}
int r2_service_preflight(uint32_t seconds,uint64_t arrival_ms)
{
 uint64_t now=now_us();
 return !status.configured||used||!seconds||seconds>120||arrival_ms>now/1000||now/1000-arrival_ms>=R2_REPLY_MS?-EACCES:0;
}
int r2_service_start(uint32_t seconds,uint64_t arrival_ms)
{
 if(!seconds){r2_service_stop();return 0;}
 int check=r2_service_preflight(seconds,arrival_ms);if(check)return check;
 int rc=(roster.group.id?r2_ble_group_start(net.identity,roster.group.id,roster.group.epoch,roster.address[net.self]):r2_ble_start(net.self,roster.count,roster.address));if(rc)return rc;
 used=started=true;
#if defined(CONFIG_BT_PER_ADV)
 r2_radio_service_activate();
#endif
 (void)k_mutex_lock(&lock,K_FOREVER);
 status.closed=false;status.active=true;status.deadline=arrival_ms*1000+(uint64_t)seconds*1000000;
 (void)k_mutex_unlock(&lock);
 k_timer_start(&service_timer,K_MSEC(2),K_MSEC(2));
 return 0;
}
int r2_service_run(uint64_t arrival_ms)
{
 int rc=r2_service_preflight(1,arrival_ms);if(rc)return rc;
 rc=(roster.group.id?r2_ble_group_start(net.identity,roster.group.id,roster.group.epoch,roster.address[net.self]):r2_ble_start(net.self,roster.count,roster.address));if(rc)return rc;
 used=started=true;
#if defined(CONFIG_BT_PER_ADV)
 r2_radio_service_activate();
#endif
 (void)k_mutex_lock(&lock,K_FOREVER);
 status.closed=false;status.active=true;status.normal=true;status.deadline=0;
 (void)k_mutex_unlock(&lock);
 k_timer_start(&service_timer,K_MSEC(2),K_MSEC(2));return 0;
}
/* Relay work is deferred in the direct V14 release. */
int r2_service_relay(uint64_t session,uint32_t seconds,uint64_t arrival_ms){(void)session;(void)seconds;(void)arrival_ms;return -ENOTSUP;}
int r2_service_relay_cancel(uint64_t session){(void)session;return -ENOTSUP;}
static void service_stop_owned(bool migration)
{
 if(!started)return;
 if(!migration){(void)k_mutex_lock(&lock,K_FOREVER);profile_pending=false;profile_phase=0;profile_target_rc=profile_rc;status.profile_pending=false;(void)k_mutex_unlock(&lock);}
#if defined(CONFIG_BT_PER_ADV)
 r2_radio_service_withdraw();
#endif
 (void)k_mutex_lock(&lock,K_FOREVER);
 if(!atomic_get(&stop_requested)){close_deadline=now_us()+R2_CLEANUP_US;status.close_deadline=close_deadline;status.closing=true;status.serving=status.follower=false;atomic_set(&stop_requested,1);}
 (void)k_mutex_unlock(&lock);
}
void r2_service_stop(void){service_stop_owned(false);}
void r2_service_supervise(void)
{
 if(!started)return;
#if defined(R2_SWITCHABLE_FIELD)
 k_mutex_lock(&lock,K_FOREVER);bool inactive=status.closed&&!status.active&&!profile_pending;k_mutex_unlock(&lock);
 if(inactive)return; /* Other backend exclusively owns the controller. */
#endif
 /* Sample clock and publish busy/timestamps in one critical section. Reading
  * now before this lock lets a preempting timer publish a newer submission;
  * unsigned subtraction would then falsely report an enormous work age. */
 (void)k_mutex_lock(&lock,K_FOREVER);uint64_t now=now_us();
 struct r2_supervision *d=&status.supervision;
 bool closed=status.closed,stop=status.deadline&&now>=status.deadline;
#if defined(CONFIG_BT_PER_ADV)
 int radio_error=r2_radio_service_supervise(now);
 if(radio_error){if(!status.error)status.error=radio_error;stop=true;}
#endif
 if(atomic_get(&stop_requested)&&!closed&&now>=close_deadline){
  supervisor_fault(2,now);if(!status.error)status.error=-ETIMEDOUT;
 }
 bool queue=(!closed||profile_pending)&&atomic_cas(&busy,0,1);
 if(queue){d->submitted=now;d->started=0;d->state=1;}
 (void)k_mutex_unlock(&lock);
 if(stop)r2_service_stop();
 if(queue){
  int rc=k_work_submit_to_queue(&owner_queue,&service_work);
  if(rc<0){
   (void)k_mutex_lock(&lock,K_FOREVER);supervisor_fault(3,now_us());status.error=rc;
   d->state=0;atomic_clear(&busy);(void)k_mutex_unlock(&lock);r2_service_stop();
  }
 }
}
int r2_service_close_result(void)
{struct r2_service_status s;r2_service_snapshot(&s);return s.error?s.error:s.closed||!started?0:-EAGAIN;}
void r2_service_snapshot(struct r2_service_status *out)
{
 (void)k_mutex_lock(&lock,K_FOREVER);*out=status;(void)k_mutex_unlock(&lock);
 uint64_t now=now_us();
#if defined(CONFIG_BT_PER_ADV)
 struct r2_group_client coarse,time;uint64_t frame;
 if(out->relay.selected){
  struct r2_relay_status *r=&out->relay;bool valid=!r->error&&r->map_valid&&now>=r->sampled_us&&now<r->valid_until_us&&now<r->source_until_us;
  time=(struct r2_group_client){.passive=true,.source=valid,.root=r->root_identity,.peer=r->parent_identity,.boot=r->root_boot,.source_epoch=r->source_epoch,.publication=r->upstream_train,.utc_us=r->utc_us,.at=r->sampled_us,.until=r->source_until_us,.map_until=r->valid_until_us};
  coarse=time;frame=r->source_frame_ns;out->precision.map_valid&=valid;out->precision.authorized&=now<r->source_until_us;
 }else r2_radio_service_snapshot(&out->precision,&time,&coarse,&frame,now);
 if(out->serving&&(!out->precision.authorized||!r2_radio_service_serving(now)))out->serving=false;
 out->display_coarse_valid=coarse.source;out->display_coarse_utc=coarse.utc_us;out->display_coarse_at=coarse.at;
 out->source_frame_ns=frame;
 /* The control clock is coarse and has no delivered-map sample timestamp.
  * Never leave it marked as delivered time when a direct map disappears. */
 out->follower_clock_valid=time.source;out->follower_utc=time.utc_us;
 out->time_sampled_us=time.at;out->service_until=time.map_until;
 if(time.passive||time.source){
  out->root_boot=time.boot;out->source_epoch=time.source_epoch;out->authority_until=time.until;
  if(time.root){int root=r2_group_slot(out->identities,time.root);if(root<0){root=1;out->identities[1]=time.root;}out->root=root;}
  out->follower=time.source;
  if(time.passive)out->serving=false;
 }
#endif
 if(out->error||out->closing||atomic_get(&stop_requested)){
  out->precision.rms_valid=out->precision.map_valid=out->precision.locked=out->precision.authorized=out->precision.drift_valid=false;
  out->follower_clock_valid=out->follower=out->serving=out->hot_ready=out->standby_authorized=false;
  out->authority_until=out->role_until=out->service_until=0;
 }

}
void r2_service_ui(struct r2_ui_model *m)
{
 struct r2_service_status s;r2_service_snapshot(&s);
 uint64_t now=now_us();m->ble_active=s.active&&!s.closing;m->root=s.root;m->rx=s.rx;m->authenticated=s.authenticated;
 m->precision=s.precision;m->quality=s.quality;m->coordinator=s.coordinator;m->standby=s.standby;
 m->profile_supported=profile_supported;m->profile_rc=s.profile_rc;m->profile_switching=s.profile_pending;
 m->relay_selected=s.relay.selected;m->relay_upstream=s.relay.parent;
 m->time_relay_hops=s.relay.selected?s.relay.time_relay_hops:0;
 m->boundary_clock=s.relay.selected;m->rc_source_held=s.relay.held_time;m->rc_update_age_ms=s.relay.correction_age_ms;m->rc_source_age_ms=s.relay.source_age_ms;
 /* Presentation-only settling state. Progress refreshes it; stale permission
  * never grants time or analog eligibility. Keep identity for at most5s after
  * the last accepted correction, and clear immediately on stop/root change. */
 static uint64_t ui_root_boot,ui_root_id,ui_progress_at;
 static uint32_t ui_source_epoch,ui_accepted;
 uint64_t candidate=s.root<4?s.identities[s.root]:0;
 bool candidate_live=s.active&&!s.error&&!s.closing&&!atomic_get(&stop_requested)&&
  !s.serving&&s.root_boot&&s.source_epoch&&candidate&&s.root!=s.self&&
  (s.self>=4||candidate!=s.identities[s.self]);
 if(!candidate_live||ui_root_boot!=s.root_boot||ui_root_id!=candidate||ui_source_epoch!=s.source_epoch){
  ui_root_boot=s.root_boot;ui_root_id=candidate;ui_source_epoch=s.source_epoch;
  ui_accepted=s.precision.accepted;ui_progress_at=0;
 }
 if(candidate_live&&s.precision.accepted!=ui_accepted){ui_accepted=s.precision.accepted;ui_progress_at=now;}
 m->sync_attempt=candidate_live&&(now<s.authority_until||
  (ui_progress_at&&now>=ui_progress_at&&now-ui_progress_at<5000000));
 unsigned labels[3]={s.root,s.root,s.coordinator};char *names[3]={m->root_name,m->upstream_name,m->coordinator_name};
 for(unsigned j=0;j<3;j++){
  unsigned p=labels[j];uint64_t id=p<4?s.identities[p]:0;
  bool known=p==s.self||((s.follower_clock_valid||m->sync_attempt)&&p==s.root)||(p<4&&(s.authenticated_mask&(1u<<p)));
  r2_ui_identity_name(names[j],known?id:0,known?(p==s.self?s.name:p<4?s.peer_names[p]:NULL):NULL);
 }
 if(s.profile_rc){m->clock.valid=false;}
 if(s.relay.selected)r2_ui_identity_name(m->upstream_name,s.relay.parent_identity,NULL);
 m->source_eligible=s.source_eligible;m->gm_capable=s.gm_capable;m->admission_ms=s.admission_ms;m->hot_ready=s.hot_ready;m->hot_refusal=s.hot_refusal;
#if defined(CONFIG_BT_PER_ADV)
 r2_time_precision_snapshot(&m->pps_precision);
#endif
 m->precision_beat_hz=s.active&&!s.closing&&!s.error&&(s.serving||s.follower_clock_valid)?R2_PRECISION_HZ:0;
 r2_ui_reception(m,&s.reception,now);
 memcpy(m->lamps,s.lamps,8);
 /* UI may project the frozen delivered map only through its independently
  * computed deadline. Worker cadence is not source evidence, and does not
  * shorten an otherwise valid map to an arbitrary50ms presentation lease. */
 bool fresh=!s.error&&!s.closing&&!atomic_get(&stop_requested);
 /* Authenticated source role and valid delivered time are independent.
  * Loss of a precision map need not erase an unchanged live root identity. */
 uint64_t self_id=s.self<4?s.identities[s.self]:0;
 uint64_t root_id=s.root<4?s.identities[s.root]:0;
 m->scope_active=fresh&&(s.serving||(root_id&&self_id&&root_id!=self_id&&now<s.authority_until));
 m->role=m->scope_active?(s.serving?R2_UI_GM:R2_UI_FOLLOWER):R2_UI_NO_ROLE;
 m->authenticated_gm=m->scope_active&&!s.serving;
 m->local_hot_standby=fresh&&m->scope_active&&s.follower&&s.hot_ready&&s.standby==s.self&&s.precision.authorized&&s.precision.map_valid;
 m->gm_source_fresh=m->scope_active&&s.serving&&s.source_epoch==m->clock.source_epoch&&m->clock.fix_active&&m->clock.pps_present;
 /* A nonbootstrap authenticated GR3 origin comes from the root physical
  * PPS-derived source map. Captured
  * corrections plus the locked receiver map establish synchronization to
  * that GM counter, independently of local GNSS. This is no absolute UTC or
  * output accuracy qualification, and public coarse JOIN alone cannot pass. */
 m->follower_pps_valid=m->authenticated_gm&&s.follower_clock_valid&&s.source_frame_ns&&s.root_boot&&s.source_epoch&&
  s.precision.authorized&&s.precision.locked&&s.precision.fit_valid&&s.precision.map_valid&&now<s.service_until;
 m->sync_age_ms=UINT32_MAX;
 r2_ui_select_ble_time(m,s.follower_utc,s.time_sampled_us,now,s.service_until,fresh&&s.follower_clock_valid);
 /* One UI producer owns this boot-local display anchor. Permission expiry,
  * root changes and reacquisition withdraw authority above, not elapsed time.
  * Only validated local, delivered BLE or authenticated JOIN time seeds it. */
 static uint64_t display_utc,display_at;static bool display_valid;static uint32_t display_profile;
 if(s.profile_pending||display_profile!=s.profile_changes){display_valid=false;display_profile=s.profile_changes;}
 if(display_valid&&now<display_at)display_valid=false;
 if(m->clock.valid&&m->clock.utc_us>=0){
  display_utc=(uint64_t)m->clock.utc_us;display_at=now;display_valid=true;
 }else if(!display_valid&&fresh&&s.display_coarse_valid&&now>=s.display_coarse_at&&
          now-s.display_coarse_at<=INT64_MAX&&s.display_coarse_utc<=(uint64_t)INT64_MAX-(now-s.display_coarse_at)){
  display_utc=s.display_coarse_utc;display_at=s.display_coarse_at;display_valid=true;
 }
 m->coarse_valid=display_valid;m->coarse_utc_us=display_utc;m->coarse_at_us=display_at;
 /* Operator UI policy: <=200ns delivered RMS, with <=500ms visual loss
  * tolerance. This never modifies servo lock, map expiry or output authority. */
 static uint64_t face_root,face_boot,face_at,face_until;
 static uint32_t face_epoch,face_rms;
 bool changed=(candidate&&face_root&&candidate!=face_root)||
  (s.root_boot&&face_boot&&s.root_boot!=face_boot)||
  (s.source_epoch&&face_epoch&&s.source_epoch!=face_epoch);
 bool hard_loss=!fresh||!s.active||s.serving||now<face_at||changed;
 if(hard_loss||(s.precision.rms_valid&&s.precision.rms_ns>200))face_until=0;
 bool measured=fresh&&candidate_live&&m->authenticated_gm&&s.follower_clock_valid&&
  s.source_frame_ns&&s.precision.authorized&&s.precision.fit_valid&&
  s.precision.map_valid&&s.precision.rms_valid&&s.precision.rms_ns<=200&&
  now<s.service_until;
 if(measured&&now<=UINT64_MAX-500000){
  face_root=candidate;face_boot=s.root_boot;face_epoch=s.source_epoch;
  face_at=now;face_until=now+500000;face_rms=s.precision.rms_ns;
 }
 m->face_lock_until_us=face_until;m->face_rms_ns=face_rms;
}

int r2_service_name(const char *name){
 size_t length=name?strlen(name):0;if(!r2_group_name(name,length))return -EINVAL;
 (void)k_mutex_lock(&lock,K_FOREVER);memcpy(pending_name,name,length+1);name_pending=true;(void)k_mutex_unlock(&lock);return 0;
}

#if defined(R2_FIELD_FIXTURE)
/* Simulator-only counterfactual: preserve field roles/control; omit relay owner. */
void r2_service_fixture_direct(void){}
#endif

uint32_t r2_service_stack_unused(void)
{
#ifdef CONFIG_INIT_STACKS
 size_t unused;if(status.configured&&!k_thread_stack_space_get(&owner_queue.thread,&unused)&&unused<=UINT32_MAX)return (uint32_t)unused;
#endif
 return UINT32_MAX;
}

#if defined(R2_DIRECT_SPLIT_FIXTURE)
/* Modeled execution pressure only. Does not alter production service policy. */
static void control_pause(struct k_work *work){k_sleep(K_MSEC(200));}
K_WORK_DEFINE(control_pause_work,control_pause);
void r2_service_fixture_control_pause(void){k_work_submit_to_queue(&owner_queue,&control_pause_work);}
#endif

void r2_service_profile_status(uint32_t out[5]){
 (void)k_mutex_lock(&lock,K_FOREVER);out[0]=profile_rc;out[1]=profile_pending;out[2]=profile_target_rc;out[3]=profile_phase;out[4]=profile_changes;(void)k_mutex_unlock(&lock);
}
int r2_service_profile(unsigned mode,uint64_t arrival_ms){
 uint64_t now=now_us();if(mode>R2_PROFILE_TOGGLE||arrival_ms>now/1000||now/1000-arrival_ms>=R2_REPLY_MS)return -EINVAL;
 (void)k_mutex_lock(&lock,K_FOREVER);
 if(!profile_supported||!roster.group.id||!status.normal||!status.active||status.error||status.closing||profile_pending){(void)k_mutex_unlock(&lock);return -EBUSY;}
 bool target=mode==R2_PROFILE_TOGGLE?!profile_rc:mode==R2_PROFILE_RC;
 if(target==profile_rc){(void)k_mutex_unlock(&lock);return 0;}
 if(profile_changes==65535||!r2_profile_incarnation(physical_boot,profile_changes+1)){(void)k_mutex_unlock(&lock);return -EOVERFLOW;}
 profile_pending=true;profile_target_rc=target;profile_phase=1;status.profile_pending=true;
 status.serving=status.follower=status.follower_clock_valid=status.hot_ready=status.standby_authorized=false;
 status.authority_until=status.role_until=status.service_until=0;status.precision.map_valid=status.precision.authorized=false;
 (void)k_mutex_unlock(&lock);r2_shadow_normal_withdraw();service_stop_owned(true);return 0;
}
static int profile_restart(uint64_t now){
 /* An actual controller/transport/precision drain under its original2s
  * deadline is mandatory. No reboot, driver recreation or flash operation. */
 (void)k_mutex_lock(&lock,K_FOREVER);bool closed=status.closed;int failure=status.error;(void)k_mutex_unlock(&lock);
 if(!closed||failure||!r2_ble_closed()||!r2_radio_service_closed()||!r2_relay_service_closed())return failure?failure:-EAGAIN;
 if(profile_phase==1){profile_phase=2;profile_reset_until=now+R2_CLEANUP_US;}
 int rc=r2_radio_service_reset(profile_changes+1);
 if(rc==-EAGAIN)return now<profile_reset_until?rc:-ETIMEDOUT;
 if(rc)return rc;
 char saved_name[25];memcpy(saved_name,net.name,sizeof saved_name);
 profile_phase=3;profile_rc=profile_target_rc;++profile_changes;
 relay_selected=relay_transition=relay_profile_set=relay_retiring=false;relay_activation_until=relay_transition_until=direct_check_due=0;local_demand=(struct r2_local_demand){0};
 r2_group_admission_reset();roster.local_capabilities=profile_supported?(profile_rc?(profile_source_caps&2)|4:profile_source_caps):profile_source_caps;roster.gm_mask=profile_rc?0:(profile_source_caps&1);roster.gnss_mask=(profile_source_caps&2)?1:0;
 memset(roster.identity+1,0,sizeof roster.identity-sizeof roster.identity[0]);memset(roster.address+1,0,sizeof roster.address-sizeof roster.address[0]);
 uint64_t incarnation=r2_profile_incarnation(physical_boot,profile_changes);
 rc=r2_relay_service_profile_reset(incarnation);if(rc)return rc;
 rc=r2_network_init(&net,0,R2_NET_PEERS,profile_identity,incarnation,incarnation^UINT64_C(0x52424c453031),roster.map_bound_us,now);if(rc)return rc;
 net.group=roster.group;net.capabilities=roster.local_capabilities;
#if defined(R2_GM5_NATIVE)
 net.gm5.enabled=!profile_rc&&(net.capabilities&1);
#ifdef R2_LORA
 struct r2_lora_view bridge_view;r2_lora_view(&bridge_view);net.gm5.bridge=r2_gm5_bridge_needed(&net.gm5,bridge_view.transmitter,bridge_view.requested);
#endif
#endif
 r2_ble_gm5_policy(net.gm5.enabled,0,true);
 net.peer[0].identity=profile_identity;memcpy(net.name,saved_name,sizeof net.name);
 r2_network_capabilities(&net,roster.gm_mask,roster.gnss_mask);r2_ble_group_client(profile_rc);r2_ble_group_relay(profile_rc);r2_radio_service_continuous(profile_supported);r2_radio_service_roster(&roster);
 rc=r2_ble_group_start(profile_identity,roster.group.id,roster.group.epoch,roster.address[0]);if(rc)return rc;
 (void)k_mutex_lock(&lock,K_FOREVER);atomic_clear(&stop_requested);transport_stopped=false;close_deadline=0;
 status.active=true;status.closed=status.closing=false;status.close_deadline=status.closed_at=0;status.profile_rc=profile_rc;status.profile_incarnation=incarnation;
 r2_radio_service_activate();profile_pending=false;profile_phase=0;(void)k_mutex_unlock(&lock);return 0;
}

/* RC always observes timing; downstream publication requires authenticated
 * demand, and a leaf never becomes a second relay or a GM-control participant. */
static void __attribute__((noinline)) normal_relay_tick(struct r2_radio_scope scope,uint64_t now,int64_t utc){
 if(atomic_get(&stop_requested)){r2_relay_service_stop(now);return;}
 struct r2_local_demand incoming={0};r2_group_admission_demand(&incoming);
 struct r2_group_client anchor={0};r2_group_admission_client(&anchor);
 bool fresh=anchor.source&&now>=anchor.at&&now<anchor.until&&anchor.root&&anchor.boot&&anchor.source_epoch;
 if(relay_selected&&!local_demand.received&&!relay_retiring){
  /* Route loss is sustained delivery loss, independent of unavailable
   * fitted time. A healthy chosen relay remains selected through warmup. */
  struct r2_link_quality_view quality;r2_relay_service_link_quality(&quality);
  bool stalled=quality.poor;
  if(stalled){
   r2_group_admission_parent_unavailable(local_demand.peer,now);r2_group_admission_demand(&incoming);
  }
 }
 /* Only a fresh authenticated renewal of the unchanged path can extend its
  * existing demand lease. Once retiring, closure cannot be cancelled. */
 if(relay_selected&&!relay_retiring&&incoming.at>local_demand.at&&now<incoming.until&&
    incoming.received==local_demand.received&&(incoming.received||(incoming.peer==local_demand.peer&&incoming.peer_boot==local_demand.peer_boot))&&
    incoming.root==local_demand.root&&incoming.root_boot==local_demand.root_boot&&incoming.source_epoch==local_demand.source_epoch){local_demand=incoming;relay_activation_until=incoming.until;}
 bool withdrawn=!local_demand.received&&!incoming.received&&incoming.peer==local_demand.peer&&incoming.peer_boot==local_demand.peer_boot&&incoming.at>local_demand.at&&now>=incoming.until;
 bool expired=relay_profile_set&&!local_demand.received&&(now>=relay_activation_until||withdrawn);
 bool changed=relay_profile_set&&fresh&&(anchor.root!=local_demand.root||anchor.boot!=local_demand.root_boot||anchor.source_epoch!=local_demand.source_epoch);
 bool new_parent=incoming.at>local_demand.at&&now<incoming.until&&!incoming.received&&(local_demand.received||incoming.peer!=local_demand.peer||incoming.peer_boot!=local_demand.peer_boot);
 if(relay_profile_set&&(relay_retiring||!profile_rc||scope.gm||changed||new_parent||expired)){
  relay_retiring=true;relay_activation_until=0;r2_ble_precision_draining(true);r2_relay_service_stop(now);
  if(!r2_relay_service_closed())return;
  if(r2_relay_service_release_path()){r2_service_stop();return;}
  relay_selected=relay_transition=relay_profile_set=relay_retiring=false;
  r2_ble_precision_draining(false);r2_ble_group_relay(false);r2_group_admission_relay_allowed(profile_rc);r2_group_admission_require_anchor();
  if(!atomic_get(&stop_requested))r2_radio_service_activate();
  return;
 }
 if(profile_rc&&!relay_profile_set&&!relay_transition){
  if(incoming.at>local_demand.at&&now<incoming.until&&r2_profile_relay_root(net.identity,incoming.root,incoming.root_boot,incoming.source_epoch)){local_demand=incoming;relay_activation_until=incoming.until;relay_transition=true;}
  /* A direct RC remains on the independently owned normal receiver. A
   * coarse bootstrap is not a relay request; migrate only for authenticated
   * incoming downstream demand or an accepted upstream relay request. */
  if(relay_transition)relay_transition_until=now+R2_CLEANUP_US;
 }
 if(relay_transition&&!relay_selected){
  r2_radio_service_handoff_request(local_demand.root,local_demand.root_boot,local_demand.source_epoch);
  r2_radio_service_stop(now);
  if(r2_radio_service_closed()&&!relay_profile_set){
   int rc=r2_relay_service_path(local_demand.root,local_demand.received?net.identity:local_demand.peer,net.identity,local_demand.root_boot,local_demand.source_epoch);
   if(rc){(void)k_mutex_lock(&lock,K_FOREVER);status.error=rc;(void)k_mutex_unlock(&lock);r2_service_stop();return;}relay_profile_set=true;
  }
  if(relay_profile_set&&r2_radio_service_closed()&&!atomic_get(&stop_requested)&&r2_radio_service_handoff_ready(local_demand.root,local_demand.root_boot,local_demand.source_epoch)){
   r2_relay_service_continuous(profile_rc);
   int rc=r2_relay_service_start(now);if(!rc){relay_selected=true;relay_transition=false;}else if(rc!=-EBUSY){(void)k_mutex_lock(&lock,K_FOREVER);status.error=rc;(void)k_mutex_unlock(&lock);r2_service_stop();return;}
  }
  if(!relay_selected&&now>=relay_transition_until){(void)k_mutex_lock(&lock,K_FOREVER);status.error=-ETIMEDOUT;(void)k_mutex_unlock(&lock);r2_service_stop();return;}
 }
 if(relay_selected){
  /* A server lease controls downstream TX only. Retain the same-GM RX owner,
   * source fences and servo when no child renews; no teardown/retraining. */
  bool publishing=local_demand.received&&now<relay_activation_until;
  r2_relay_service_passive(local_demand.received&&!publishing);
  r2_ble_group_relay(local_demand.received);r2_group_admission_relay_allowed(local_demand.received);
  r2_relay_service_activation(relay_activation_until);
  int64_t trusted=-1;struct r2_join_time bounded={.utc_us=anchor.utc_us,.until=anchor.until,.uncertainty_us=anchor.uncertainty_us};uint64_t upper;
  /* A bounded JOIN clock remains a replay/UTC estimate after its independent
   * source permission expires, just as in the normal direct receiver. Only
   * newly authenticated RC5/GR3 origins authorize timing; this coarse value
   * cannot renew a root permit or a delivered map. */
  if(anchor.source&&anchor.root&&anchor.boot&&anchor.source_epoch&&now>=anchor.at&&r2_group_coarse_upper(&bounded,anchor.at,now,&upper)&&anchor.utc_us<=INT64_MAX-(now-anchor.at))trusted=(int64_t)(anchor.utc_us+now-anchor.at);
  r2_relay_service_tick(scope,&source_work,trusted>=0&&trusted<=INT64_MAX/1000?trusted*1000:-1,trusted>=0,now);
  if(r2_relay_service_error()){(void)k_mutex_lock(&lock,K_FOREVER);status.error=r2_relay_service_error();(void)k_mutex_unlock(&lock);r2_service_stop();}
 }else r2_radio_service_tick(scope,&source_work,utc>=0&&utc<=INT64_MAX/1000?utc*1000:-1,now);
}

#if defined(R2_SWITCHABLE_FIELD)
int r2_normal_service_resume_gm(uint64_t incarnation,uint32_t completed_changes){
 k_mutex_lock(&lock,K_FOREVER);
 if(!status.closed||status.active||status.error||profile_pending||profile_changes==65535){k_mutex_unlock(&lock);return -EBUSY;}
 if(completed_changes>=65535||incarnation!=r2_profile_incarnation(physical_boot,completed_changes+1)){k_mutex_unlock(&lock);return -EINVAL;}
 /* A separately owned RC can be the boot profile. Its first GM entry starts
  * this already configured, proved-idle owner rather than recreating drivers. */
 if(!started){used=started=true;status.normal=true;atomic_set(&stop_requested,1);}
 profile_changes=completed_changes;profile_target_rc=false;profile_pending=true;profile_phase=1;status.profile_pending=true;
 k_mutex_unlock(&lock);
 /* The old supervisor stops this timer after a proved close. Re-arm the
  * existing owner before requesting its asynchronous GM restart. */
 k_timer_start(&service_timer,K_MSEC(2),K_MSEC(2));
 r2_service_supervise();return 0;
}
#endif

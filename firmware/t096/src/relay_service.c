/* SPDX-License-Identifier: Apache-2.0
 * Explicit volatile ONE-relay profile. Stock public controller ownership only.
 * All scheduling/crypto/formatting runs in the serialized BLE owner, never ISR. */
#include "relay_service.h"
#include "rc_shadow.h"
#include "ble_service.h"
#include "ble_transport.h"
#include "owner_budget.h"
#include "group_admission.h"
#include "radio_capture.h"
#include "radio_service.h"
#include "capture.h"
#include <errno.h>
#include <zephyr/bluetooth/bluetooth.h>
#if defined(R2_RELAY_SIMULATION) || defined(R2_FIELD_FIXTURE)
#include "bs_tracing.h"
#endif
#define R2_LOCK_UNIT 10u
#include "health_lock.h"
static struct r2_relay_precision timing;
static struct r2_link_quality upstream_quality;
void r2_relay_service_link_quality(struct r2_link_quality_view *out){*out=upstream_quality.view;}
static struct r2_enrollment enrollment;
static struct r2_relay_status view;
static struct k_spinlock lock;
static struct bt_le_ext_adv *pa;
static struct bt_le_per_adv_sync *sync_object;
static bt_addr_le_t target;
static unsigned own,root,relay,leaf;
static bool local_path,passive_observer;
static bool continuous_receiver;
void r2_relay_service_continuous(bool continuous){continuous_receiver=continuous;}
void r2_relay_service_passive(bool passive){passive_observer=passive;}
static uint64_t witness_delivery,witness_epoch;
static uint64_t path_boot;static uint32_t path_epoch,train_floor;
static uint64_t own_boot,close_until,sync_until,first_rx_until,rx_close_until,last_rx,updated_at,retry_at;
static uint64_t lifecycle,rx_lifecycle,tx_lifecycle,data_until_tick,data_retry_at,pa_requested_at;
static uint32_t published_correction,last_overflow,flushed,cost_refusals,retries;
static unsigned phase_failures,acquisition_retries;
static uintptr_t tx_owner,rx_owner;
static bool selected,active,closing,discovering,found,creating,established,cancelling;
static bool cancel_established,terminated_during_create,pa_started,ext_started,scan_started;
static bool coarse_valid;static uint64_t activation_until,refused_activation;
void r2_relay_service_activation(uint64_t until){activation_until=until;}
static bool initial_start_deferred,field_att_ready;
static uint64_t field_att_second,field_accepted;
static bool field_timing_first,field_renewal_done;
static bool window_recovering;
static uint32_t window_faults,window_recoveries,window_fault_site,fault_line;
static uint64_t window_fault_at,window_fault_deadline,window_recovery_until;
static int error;
static void fault_at(int rc,unsigned line){if(rc&&!error){error=rc;fault_line=line;}}
#define fault(rc) fault_at((rc),__LINE__)
static void scan_received(const struct bt_le_scan_recv_info *info,struct net_buf_simple *data){
 k_spinlock_key_t k=k_spin_lock(&lock);
 if(active&&discovering&&info->sid==12&&info->interval==50&&bt_addr_le_eq(info->addr,&target))found=true;
 k_spin_unlock(&lock,k);
}
static bool matches(struct bt_le_per_adv_sync *object,const bt_addr_le_t *addr,uint8_t sid){
 if(!selected||sid!=12||!bt_addr_le_eq(addr,&target)||
    (sync_object!=object&&!(creating&&!sync_object)))return false;
 if(!sync_object)sync_object=object;
 return true;
}
static void synced(struct bt_le_per_adv_sync *object,struct bt_le_per_adv_sync_synced_info *info){
 k_spinlock_key_t k=k_spin_lock(&lock);
 if(matches(object,info->addr,info->sid))established=true;
 k_spin_unlock(&lock,k);
}
static void terminated(struct bt_le_per_adv_sync *object,const struct bt_le_per_adv_sync_term_info *info){
 k_spinlock_key_t k=k_spin_lock(&lock);
 if(matches(object,info->addr,info->sid)){
  if(creating)terminated_during_create=true;
  sync_object=NULL;creating=established=cancelling=false;found=false;
 }
 k_spin_unlock(&lock,k);
}
static struct bt_le_scan_cb scan_cb={.recv=scan_received};
static struct bt_le_per_adv_sync_cb sync_cb={.synced=synced,.term=terminated};
int r2_relay_service_init(unsigned self,const struct r2_enrollment *roster,uint64_t boot,void (*wake)(void)){
 field_att_ready=field_timing_first=field_renewal_done=false;field_att_second=UINT64_MAX;
 if(!roster||roster->count<2||roster->count>4||self>=roster->count||!boot)return -EINVAL;
 own=self;own_boot=boot;enrollment=*roster;root=relay=leaf=4;(void)wake;
 r2_relay_service_roster(roster);
 bt_le_scan_cb_register(&scan_cb);return bt_le_per_adv_sync_cb_register(&sync_cb);
}
void r2_relay_service_roster(const struct r2_enrollment *r){
 enrollment=*r;root=relay=leaf=4;
 /* This selection exists ONLY inside the explicitly activated test profile.
  * Default autonomous firmware role selection never calls it. */
 for(unsigned p=0;p<enrollment.count;p++){
  if(enrollment.identity[p]==UINT64_C(0x1000000000000001))root=p;
  if(enrollment.identity[p]==UINT64_C(0x1000000000000002))relay=p;
  if(enrollment.identity[p]==UINT64_C(0x1000000000000003))leaf=p;
 }
}
int r2_relay_service_path(uint64_t root_id,uint64_t relay_id,uint64_t local_id,uint64_t root_boot,uint32_t source_epoch){
 if(!enrollment.group.id||!root_id||!relay_id||!local_id||!root_boot||!source_epoch||root_id==relay_id||
    local_id==root_id||!r2_relay_service_closed()||selected)return -EACCES;
 local_path=true;path_boot=root_boot;path_epoch=source_epoch;root=0;relay=1;leaf=2;own=local_id==relay_id?relay:leaf;
 enrollment.count=3;memset(enrollment.identity,0,sizeof enrollment.identity);
 enrollment.identity[0]=root_id;enrollment.identity[1]=relay_id;
 if(own==leaf)enrollment.identity[2]=local_id;
 for(unsigned i=0;i<3;i++)if(enrollment.identity[i]){
  uint8_t key[16];r2_group_pair_key(key,&enrollment.group,enrollment.identity[i],enrollment.identity[i]);
  memcpy(enrollment.address[i],key,6);enrollment.address[i][5]|=0xc0;
 }
 return 0;
}
int r2_relay_service_start(uint64_t now){
 struct r2_capture current;
 if(selected||root>=enrollment.count||relay>=enrollment.count||leaf>=enrollment.count||
    (own!=root&&own!=relay&&own!=leaf)||!r2_capture_now(&current)||!current.epoch||current.epoch>UINT32_MAX)return -EACCES;
 int rc=r2_radio_capture_flush();if(rc<0)return rc;flushed+=rc;
 k_spinlock_key_t k=k_spin_lock(&lock);selected=active=true;k_spin_unlock(&lock,k);++lifecycle;
 if(local_path&&timing.train>train_floor)train_floor=timing.train;
 r2_ble_field_admission_window(true,false);
 r2_relay_precision_init(&timing,own,root,relay,own_boot,current.epoch);
 memset(&upstream_quality,0,sizeof upstream_quality);
 timing.experimental_fresh_us=500000; /* Operator rev2 exception, not a calibrated bound. */
 if(local_path)timing.train=train_floor;
 timing.v14=true;timing.group=enrollment.group;memcpy(timing.identities,enrollment.identity,sizeof timing.identities);
 if(local_path&&own==relay)(void)r2_radio_service_handoff_take(&timing,enrollment.identity[root],path_boot,path_epoch,current.epoch);
 target.type=BT_ADDR_LE_RANDOM;memcpy(target.a.val,enrollment.address[own==relay?root:relay],6);
 retry_at=now;return 0;
}
static void discovery(bool enable){
 k_spinlock_key_t k=k_spin_lock(&lock);discovering=enable;k_spin_unlock(&lock,k);
 if(own!=leaf||enrollment.field_profile||local_path){r2_ble_precision_discovery(enable);return;}
 /* Leaf's connected management owner is closed before activation. Its only
  * listening is bounded public discovery/PA sync inside this400ms window. */
 if(enable&&!scan_started){
  const struct bt_le_scan_param p={.type=BT_LE_SCAN_TYPE_PASSIVE,.interval=32,.window=32};
  int rc=bt_le_scan_start(&p,NULL);fault(rc);if(!rc)scan_started=true;
 }else if(!enable&&scan_started){int rc=bt_le_scan_stop();fault(rc);if(!rc)scan_started=false;}
}
static void sync_close(uint64_t now){
 discovery(false);
 k_spinlock_key_t k=k_spin_lock(&lock);struct bt_le_per_adv_sync *object=sync_object;
 bool issue=object&&(!cancelling||(established&&!cancel_established));
 if(issue){cancelling=true;cancel_established=established;if(!rx_close_until)rx_close_until=now+2000000;}
 k_spin_unlock(&lock,k);
 if(issue){int rc=bt_le_per_adv_sync_delete(object);if(rc&&bt_le_per_adv_sync_lookup_addr(&target,12)==object)fault(rc);}
 if(object&&!bt_le_per_adv_sync_lookup_addr(&target,12)){
  k=k_spin_lock(&lock);if(sync_object==object){sync_object=NULL;creating=established=cancelling=false;found=false;}k_spin_unlock(&lock,k);
 }
 k=k_spin_lock(&lock);bool pending=sync_object||creating;k_spin_unlock(&lock,k);
 r2_ble_sync_acquiring(pending);
 if(!pending){rx_close_until=last_rx=0;rx_owner=0;}
 else if(rx_close_until&&now>=rx_close_until)fault(-ETIMEDOUT);
}
static bool sync_pending(void){
 k_spinlock_key_t k=k_spin_lock(&lock);bool pending=sync_object||creating;k_spin_unlock(&lock,k);return pending;
}
static void pa_close(unsigned reason){
 if(!pa)return;
 (void)reason;
#if defined(R2_FIELD_FIXTURE)
 {struct r2_capture stamp={0};r2_capture_now(&stamp);
  bs_trace_raw(0,"R2_PA_CLOSE reason=%u tick=%llu train=%u data_until=%llu origin_until=%llu activation=%llu phase_failures=%u retries=%u last_rx=%llu\n",
   reason,(unsigned long long)stamp.tick,timing.train,(unsigned long long)data_until_tick,
   (unsigned long long)timing.origin_until_tick,(unsigned long long)activation_until,phase_failures,acquisition_retries,(unsigned long long)last_rx);
 }
#endif
 int rc=0;
 if(ext_started){rc=bt_le_ext_adv_stop(pa);if(!rc)ext_started=false;}
 if(!rc&&pa_started){rc=bt_le_per_adv_stop(pa);if(!rc)pa_started=false;}
 if(!rc){rc=bt_le_ext_adv_delete(pa);if(!rc){pa=NULL;tx_owner=0;data_until_tick=0;}}
 fault(rc);
}
void r2_relay_service_stop(uint64_t now){
 if(!selected)return;
 field_att_ready=field_timing_first=field_renewal_done=false;
 r2_ble_field_admission_window(true,false);
 if(!closing){window_recovering=false;closing=true;k_spinlock_key_t k=k_spin_lock(&lock);active=false;k_spin_unlock(&lock,k);close_until=now+2000000;r2_relay_precision_withdraw(&timing);}
 pa_close(0);sync_close(now);
 struct r2_radio_capture_stats s;r2_radio_capture_stats(&s);
 if(!pa&&!sync_pending()&&!scan_started&&!s.armed)closing=false;
 else if(now>=close_until)fault(-ETIMEDOUT);
}
bool r2_relay_service_closed(void){
 struct r2_radio_capture_stats s;r2_radio_capture_stats(&s);
 return !active&&!closing&&!pa&&!sync_pending()&&!scan_started&&!s.armed;
}
int r2_relay_service_error(void){return error;}
bool r2_relay_service_serving(uint64_t us){
 /* LIVE names a real downstream train with a recent captured correction,
  * not merely a healthy direct receiver or an allocated advertiser. The
  * immutable publication/source/map bounds remain independently enforced. */
 struct r2_capture now;int64_t ns,utc;
 return selected&&active&&!closing&&!error&&own==relay&&!timing.passive_receiver&&
  pa&&pa_started&&ext_started&&coarse_valid&&activation_until>us&&
  r2_capture_now(&now)&&now.epoch==timing.epoch&&
  data_until_tick>now.tick&&timing.phase_admitted&&timing.sent_count&&
  timing.last_tx_tick&&now.tick>=timing.last_tx_tick&&now.tick-timing.last_tx_tick<R2_RADIO_MAX_AGE_TICKS&&
  r2_relay_precision_read(&timing,now,us,&ns,&utc);
}
/* A passive scheduling miss is retained as a failed interval. Revoke output
 * maps immediately, preserve fit/history, and reuse nothing until the original
 * controller drain is proven. API failures and drain-deadline misses stay fatal. */
static bool recover_window(uint64_t us){
 if(!window_recovering)return false;
 r2_ble_field_admission_window(false,false);
 pa_close(6);sync_close(us);
 struct r2_radio_capture_stats stats;r2_radio_capture_stats(&stats);
 uint64_t done=(uint64_t)k_ticks_to_us_floor64(k_uptime_ticks());
 if(done>=window_recovery_until){fault(-ETIMEDOUT);return true;}
 if(error)return true;
 if(!pa&&!sync_pending()&&!scan_started&&!stats.armed){
  window_recovering=false;r2_math_inc(&window_recoveries);
  r2_ble_client_defer(done+500000);
 }
 return true;
}
static void miss_window(uint64_t us,uint64_t deadline,unsigned site){
 if(!window_recovering){
  window_recovering=true;r2_math_inc(&window_faults);
  window_fault_at=us;window_fault_deadline=deadline;window_fault_site=site;
  window_recovery_until=rx_close_until?rx_close_until:us+2000000;
  r2_relay_precision_pause(&timing);memset(timing.map,0,sizeof timing.map);
  timing.upstream.servo.output.valid=false;
  r2_relay_precision_downstream_withdraw(&timing);
 }
 r2_ble_client_defer(us+500000);
 (void)recover_window(us);
}

static bool prepare(const struct r2_source_clock *source,struct r2_capture now,
 struct r2_radio_scope authority,int64_t utc,uint64_t us,bool new_train,
 struct r2_relay_prepared *q,uint8_t ad[190]){
 if(own==root){
  uint64_t ns,revision,expires;
  if(!authority.gm||authority.root!=root||authority.boot!=own_boot||authority.until_us<=us||
     !r2_source_clock_read(source,now,&ns,&revision)||!r2_source_clock_expiry(source,now,&expires))return false;
  uint64_t permit=authority.until_us-us;
  if(permit<=1500000&&permit*1000<=UINT64_MAX-ns&&ns+permit*1000<expires)expires=ns+permit*1000;
  /* Only the signed V14 serving permit gets 150 ms grace. The actual source
   * read, authority and physical TX still require their original freshness. */
  if(timing.v14){if(expires>INT64_MAX-150000000)return false;expires+=150000000;}
  if(!r2_relay_precision_root(&timing,now,authority.source_epoch,ns,expires,utc,revision,enrollment.key[root]))return false;
 }
 if((new_train&&(timing.train==UINT32_MAX||timing.map_generation==UINT32_MAX))||
    (!new_train&&timing.publication==UINT32_MAX)){fault(-EOVERFLOW);return false;}
 if(!r2_relay_precision_prepare(&timing,now,us,new_train,q))return false;
 int rc=enrollment.group.id?r2_group_relay_encode3(ad,&q->packet,q->proof,&enrollment.group,enrollment.identity):r2_relay_encode(ad,&q->packet,q->proof,enrollment.key[own]);
 fault(rc);return !rc;
}
static bool install(const struct r2_relay_prepared *q,const uint8_t ad[190],struct r2_radio_scope authority){
 struct r2_capture fresh;uint64_t us=(uint64_t)k_ticks_to_us_floor64(k_uptime_ticks());
 uint64_t installed_deadline=data_until_tick;
 if((own==root&&us>=authority.until_us)||!r2_capture_now(&fresh)||
    (installed_deadline&&fresh.tick>=installed_deadline)||
    !r2_relay_precision_commit(&timing,q,fresh))return false;
 /* Commit precedes a controller update which may already become visible.
  * A failed/ambiguous call burns its publication/train; never roll it back. */
 const struct bt_data data={.type=BT_DATA_MANUFACTURER_DATA,.data_len=188,.data=ad+2};
 int rc=bt_le_per_adv_set_data(pa,&data,1);
 /* Pinned Zephyr host adv.c:hci_set_per_adv_data returns ENOBUFS before
  * submitting this single COMPLETE_DATA command. All other errors remain
  * hard: neither EIO nor arbitrary controller BUSY implies no state change. */
 if(rc==-ENOBUFS){data_retry_at=us+62500;retry_at=data_retry_at;return false;}
 fault(rc);
 if(rc)return false;
 /* A synchronous update returning after the old installed deadline cannot
  * prove uninterrupted valid payload. Preserve the hard failure, never rescue
  * an expired train merely because replacement data has a later deadline. */
 if(installed_deadline){struct r2_capture done;
  if(!r2_capture_now(&done)||done.epoch!=fresh.epoch||done.tick>=installed_deadline){fault(-ETIMEDOUT);return false;}
 }
 /* Installed payload lifetime is its immutable origin, not preparation
  * map freshness. Receivers independently reject stale corrections. */
 data_until_tick=q->published_until_tick?q->published_until_tick:timing.origin_until_tick;data_retry_at=0;updated_at=us;published_correction=timing.tx_corrections;return true;
}
static bool publish(const struct r2_source_clock *source,struct r2_capture now,
 struct r2_radio_scope authority,int64_t utc,uint64_t us){
 if(us<data_retry_at)return false;
 struct r2_relay_prepared q;uint8_t ad[190];
 return prepare(source,now,authority,utc,us,false,&q,ad)&&install(&q,ad,authority);
}
static void pa_start(const struct r2_source_clock *source,struct r2_capture now,
 struct r2_radio_scope authority,int64_t utc,uint64_t us){
 struct r2_relay_prepared q;uint8_t ad[190];
 /* WAIT_INPUT owns no advertiser and consumes no train or lifecycle. */
 if(!prepare(source,now,authority,utc,us,true,&q,ad))return;
 const struct bt_le_adv_param param={.id=0,.sid=12,.options=BT_LE_ADV_OPT_NO_2M|BT_LE_ADV_OPT_EXT_ADV|BT_LE_ADV_OPT_USE_IDENTITY,.interval_min=50,.interval_max=50};
 const struct bt_le_per_adv_param periodic={.interval_min=50,.interval_max=50};
 int rc=bt_le_ext_adv_create(&param,NULL,&pa);
 if(!rc){++tx_lifecycle;rc=bt_le_per_adv_set_param(pa,&periodic);}
 fault(rc);if(rc)return;
 if(!install(&q,ad,authority)){pa_close(1);return;}
 struct r2_capture fresh;uint64_t current=(uint64_t)k_ticks_to_us_floor64(k_uptime_ticks());
 if(!r2_capture_now(&fresh)||fresh.epoch!=now.epoch||fresh.tick>=q.valid_until_tick||
    (own==root&&current>=authority.until_us)){pa_close(2);return;}
 pa_requested_at=current;rc=bt_le_per_adv_start(pa);if(!rc){pa_started=true;phase_failures=0;}
 if(!rc){rc=bt_le_ext_adv_start(pa,BT_LE_EXT_ADV_START_DEFAULT);if(!rc)ext_started=true;}
 fault(rc);
}
static void follower(uint64_t now,bool window){
 k_spinlock_key_t k=k_spin_lock(&lock);bool cancel=cancelling;k_spin_unlock(&lock,k);
 if(!window||cancel){sync_close(now);return;}
 k=k_spin_lock(&lock);bool pending=sync_object||creating;
 bool acquired=established;
 bool timeout=sync_object&&((!established&&now>=sync_until)||
  ((enrollment.field_profile||local_path)&&!last_rx&&now>=first_rx_until));
 k_spin_unlock(&lock,k);
 r2_ble_sync_acquiring(pending&&!acquired);
 if(timeout){
  if(local_path&&own==relay&&!last_rx)r2_group_admission_direct_opportunity(timing.upstream.accepted,now);
#if defined(R2_FIELD_FIXTURE)
  bs_trace_raw(0,"FIELD_SYNC_CLOSE unit=%u t=%llu cause=first_capture_or_acquisition until=%llu last=%llu\n",own,(unsigned long long)now,(unsigned long long)first_rx_until,(unsigned long long)last_rx);
#endif
  r2_relay_precision_pause(&timing);sync_close(now);return;
 }
 if(pending){if(acquired)discovery(false);return;}
 if(r2_ble_initiating())return;
 int rc=r2_radio_capture_flush();if(rc==-EBUSY)return;if(rc<0){fault(rc);return;}flushed+=rc;
 r2_relay_precision_pause(&timing);discovery(true);
 /* The parent address/SID is already authenticated/bound. Public create_sync
  * acquires SyncInfo itself; waiting for an extra discovery report wastes a
  * complete acquisition round trip inside the finite client window. */
 k=k_spin_lock(&lock);bool create=true;
 if(create){creating=true;terminated_during_create=false;cancel_established=false;sync_until=now+2000000;first_rx_until=now+200000;}
 k_spin_unlock(&lock,k);if(!create)return;
 /* No captured packet exists yet to drive the200ms stale-RX fence. Bound
  * field first-capture widening from this original create request instead;
  * discovery/callback progress cannot refresh it. Public close remains2s. */
 r2_ble_sync_acquiring(true);
 ++rx_lifecycle;rx_owner=0;
 struct bt_le_per_adv_sync_param p={.sid=12,.skip=0,.timeout=100};bt_addr_le_copy(&p.addr,&target);
 struct bt_le_per_adv_sync *object=NULL;rc=bt_le_per_adv_sync_create(&p,&object);
 k=k_spin_lock(&lock);
 if(!rc&&!terminated_during_create){if(sync_object&&sync_object!=object)fault(-EFAULT);else sync_object=object;}
 creating=false;k_spin_unlock(&lock,k);fault(rc);
}
void r2_relay_service_tick(struct r2_radio_scope authority,const struct r2_source_clock *source,
 int64_t utc,bool coarse_fresh,uint64_t us){
 if(!selected)return;
 coarse_valid=coarse_fresh;
 if(!active||closing||error||(!local_path&&activation_until&&us>=activation_until)){r2_relay_service_stop(us);return;}
 if(recover_window(us))return;
 /* Expired source permission suppresses forwarding/output below. It does
  * not revoke an independently bounded coarse anchor or erase clock history;
  * actual root/boot/epoch changes still fence the timing engine. */
 struct r2_capture now;if(!r2_capture_now(&now)||now.epoch!=timing.epoch){fault(-ESTALE);r2_relay_service_stop(us);return;}
 /* Recover after200ms RX silence under the pinned controller cost bound.
  * Map/source expiry is unchanged. Leaf recovery remains within its window. */
 /* Capture then uptime makes the derived physical cutoff conservative:
  * caller time can precede this owner by crypto/queue work. Keep the original
  * absolute second even if a delayed cycle crosses into the next one. */
 timing.passive_receiver=local_path&&passive_observer&&own==relay;
 bool windowed=!continuous_receiver&&(own==leaf||timing.passive_receiver);
 uint64_t entry_us=(uint64_t)k_ticks_to_us_floor64(k_uptime_ticks());
 /* Root-coordinated phase prevents boot-offset-dependent source expiry gaps.
  * Change only after all old-window PA and transport resources have drained. */
 if(windowed&&coarse_fresh&&utc>0&&!sync_pending()&&!scan_started)
  (void)r2_ble_client_align(entry_us,(uint64_t)utc/1000+(entry_us>=us?entry_us-us:0));
 uint64_t window_start=r2_ble_client_window_start(us);
 uint64_t window_end=window_start+(enrollment.field_profile?R2_FIELD_CLIENT_WINDOW_US:400000);
 uint64_t quiet_deadline=window_start+500000;
 bool window=!windowed||r2_ble_client_window_open(entry_us,enrollment.field_profile?R2_FIELD_CLIENT_WINDOW_US:400000);
 bool field_leaf=windowed;
 bool drain_before_close=field_leaf&&!window;
 uint64_t capture_cutoff=now.tick;
 if(field_leaf){
  if(entry_us>=window_end){uint64_t late=(entry_us-window_end)*16;
   capture_cutoff=now.tick>=late?now.tick-late:0;
  }else capture_cutoff=now.tick+(window_end-entry_us)*16;
 }
 if(windowed&&entry_us>=quiet_deadline&&(sync_pending()||scan_started)){
  miss_window(entry_us,quiet_deadline,1);return;
 }
 view.quiet=windowed&&!window;
 if(own!=root){
  if(last_rx&&now.tick>=last_rx+3200000){
#if defined(R2_FIELD_FIXTURE)
   if(windowed&&us>=120000000&&us<145000000)bs_trace_raw(0,"FIELD_CLOSE_REASON t=%llu reason=stale last=%llu nowtick=%llu life=%llu\n",(unsigned long long)us,(unsigned long long)last_rx,(unsigned long long)now.tick,(unsigned long long)rx_lifecycle);
#endif
   r2_relay_precision_pause(&timing);sync_close(us);
  }
  /* Reserve initial admission setup for MTU and validated GATT discovery.
   * Once the established ACL is ready, its bounded JOIN exchange can coexist
   * with PA acquisition. Waiting for disconnect discards the early timing
   * events needed for two corrections inside the finite client window.
   * Discovery readiness grants no timing authority; coarse/source/activation
   * freshness and the separate initiation exclusion remain mandatory. */
  bool acquisition=window&&coarse_fresh;
  /* Both public timing owners honor the SAME transport initiation window.
   * Do not seize an idle scan resource with a new sync first. An existing
   * sync is retained, and always-online relay parents are unaffected. */
  if(local_path&&windowed&&!sync_pending()&&r2_ble_admission_reserves_scan(us))acquisition=false;
  if(windowed){
   if(field_att_second!=window_start){
    field_att_ready=false;field_att_second=window_start;
    field_timing_first=coarse_fresh&&timing.origin_valid&&
     r2_ble_admission_due(us)&&!r2_ble_admission_urgent(us)&&!r2_ble_admission_connected();
    field_renewal_done=false;field_accepted=timing.upstream.accepted;
    /* Normal passive renewal may use the first admission slot when the
     * actual selected map covers this entire400ms window. PA acquisition
     * resumes after authenticated ACL setup in the same window. Requiring
     * next-window coverage before renewing a30s demand can starve forever;
     * this grants no extra source/map lifetime or listening time. */
    if(field_timing_first&&!enrollment.field_profile&&window&&us<window_end){
     struct r2_capture end=now;int64_t relative,utc_value;
     end.tick+=(window_end-us)*16;
     if(r2_relay_precision_read(&timing,end,window_end,&relative,&utc_value))
      field_timing_first=false;
    }
   }
   if(!window)field_att_ready=false;
   if(window&&r2_ble_ready(r2_ble_admission_peer()))field_att_ready=true;
   int64_t sample_ns,sample_utc;
   struct r2_capture renewal_horizon=now;
   renewal_horizon.tick+=(window_start+1200000-us)*16;
   if(field_timing_first&&!field_renewal_done&&us-window_start<200000&&
      timing.upstream.accepted>field_accepted&&timing.origin_until_tick>now.tick&&
      r2_relay_precision_read(&timing,now,us,&sample_ns,&sample_utc)&&
      r2_relay_precision_read(&timing,renewal_horizon,window_start+1200000,&sample_ns,&sample_utc))
    field_renewal_done=true;
   /* A live renewal receives a fresh usable correction first, then closes
    * and drains PA before exposing a bounded late admission opportunity.
    * Late correction means defer renewal, never extend activation or the window. */
   r2_ble_field_admission_window(window&&(!field_timing_first||(field_renewal_done&&!sync_pending())),field_timing_first);
   /* ATT teardown may clear readiness before disconnect completes. It must
    * not cancel a separately owned PA or erase its matching captures. This
    * window-local setup latch grants no source, activation or time authority. */
   acquisition=acquisition&&(!field_timing_first||!field_renewal_done)&&
    (!r2_ble_admission_connected()||field_att_ready)&&
    (field_timing_first||!r2_ble_admission_due(us)||us-window_start>=100000);
  }
#if defined(R2_FIELD_FIXTURE)
  if(windowed&&us>=120000000&&us<145000000&&!acquisition&&sync_pending())bs_trace_raw(0,"FIELD_CLOSE_REASON t=%llu reason=gate window=%u coarse=%u connected=%u att=%u due=%u life=%llu\n",(unsigned long long)us,window,coarse_fresh,r2_ble_admission_connected(),field_att_ready,r2_ble_admission_due(us),(unsigned long long)rx_lifecycle);
#endif
  if(!drain_before_close)follower(us,acquisition);
 }
 r2_link_quality_bind(&upstream_quality,enrollment.identity[own==relay?root:relay],
  local_path?(own==relay?path_boot:0):timing.remote.boot,now.epoch);
 if(local_path&&own==leaf){struct r2_group_client a;r2_group_admission_client(&a);
  r2_link_quality_bind(&upstream_quality,a.peer,a.peer_boot,now.epoch);}
 r2_link_quality_tick(&upstream_quality,own!=root&&window&&coarse_fresh&&!r2_ble_initiating(),us);
 struct r2_radio_capture_stats stats;r2_radio_capture_stats(&stats);
 if(stats.overflow!=last_overflow){last_overflow=stats.overflow;fault(-ENOBUFS);r2_relay_service_stop(us);return;}
 struct r2_radio_record r;
 for(unsigned i=0;i<R2_OWNER_CAPTURE_RX&&r2_radio_capture_pop(&r);i++){
  uint64_t processing_us=(uint64_t)k_ticks_to_us_floor64(k_uptime_ticks());
  if(windowed&&processing_us>=quiet_deadline&&(sync_pending()||scan_started)){miss_window(processing_us,quiet_deadline,2);return;}
  struct r2_capture physical;struct r2_relay_packet p;
  if(r.length!=190||!r.owner||!r2_capture_radio(r.raw,r.epoch,&physical)||!r2_capture_now(&now))continue;
  /* Only immutable captures from inside this nominal field window may be
   * processed before shutdown. A queued correction is not new RF permission. */
  if(field_leaf&&physical.tick>=capture_cutoff)continue;
  if(r.reservation_us>view.reservation_max_us)view.reservation_max_us=r.reservation_us;
  if(r.prepare_us>view.prepare_max_us)view.prepare_max_us=r.prepare_us;
  if(r.receive_window_us>view.rx_window_max_us)view.rx_window_max_us=r.receive_window_us;
  if(r.reservation_us>2655||r.prepare_us>1495||r.receive_window_us>628){
#if defined(R2_FIELD_FIXTURE)
   bs_trace_raw(0,"FIELD_COST unit=%u t=%llu tx=%u reservation=%u prepare=%u window=%u owner=%lx\n",own,(unsigned long long)us,r.tx,r.reservation_us,r.prepare_us,r.receive_window_us,(unsigned long)r.owner);
#endif
   ++cost_refusals;fault(-ERANGE);break;
  }
  unsigned sender=enrollment.group.id?(unsigned)r2_group_slot(enrollment.identity,r2_group_get(r.ad+90,8)):r.ad[88];if(sender>=enrollment.count||(enrollment.group.id?r2_group_relay_decode3(&p,r.ad,190,&enrollment.group,enrollment.identity):r2_relay_decode(&p,r.ad,190,own,enrollment.key[root][own],enrollment.key[sender][own]))){r2_math_inc(&timing.rejected);continue;}
  if(enrollment.group.id)p.origin.relay=relay; /* Local timing path, never GM roster. */
  if(r.tx){
   if(!pa||!pa_started||sender!=own||p.route.train!=timing.train)continue;
   if(tx_owner&&tx_owner!=r.owner){fault(-EFAULT);break;}tx_owner=r.owner;
   uint64_t ns=0,revision=0,expires=0;
   if(own==root){(void)r2_source_clock_read(source,physical,&ns,&revision);(void)r2_source_clock_expiry(source,physical,&expires);}
   uint32_t before=timing.phase_rejected;
   bool corrected=r2_relay_precision_tx(&timing,&p,physical,r.event,r.channel,ns,revision,expires);
   if(corrected){phase_failures=0;acquisition_retries=0;}
   if(own==relay&&timing.phase_rejected!=before)++phase_failures;
  }else{
   k_spinlock_key_t k=k_spin_lock(&lock);bool ready=sync_object&&established&&!cancelling;k_spin_unlock(&lock,k);
   if(!ready||!coarse_fresh||sender!=(own==relay?root:relay))continue;
   if(rx_owner&&rx_owner!=r.owner){fault(-EFAULT);break;}rx_owner=r.owner;last_rx=physical.tick;
   if(enrollment.field_profile&&windowed){
    struct r2_group_client anchor;r2_group_admission_client(&anchor);
    if(!anchor.source)continue;
    if(p.origin.boot!=anchor.boot||p.origin.source_epoch!=anchor.source_epoch||
       p.route.boot!=anchor.peer_boot){
     /* Reject old/delayed origins without turning them into a bootstrap DoS.
      * A fresh different scope only asks for a new bounded challenge. */
     if(p.origin.created_ns<=INT64_MAX&&p.origin.expires_ns<=INT64_MAX&&
        p.origin.utc_offset_ns>=0&&p.origin.utc_offset_ns<=INT64_MAX-(int64_t)p.origin.expires_ns){
      int64_t created=(int64_t)p.origin.created_ns+p.origin.utc_offset_ns;
      int64_t expiry=(int64_t)p.origin.expires_ns+p.origin.utc_offset_ns;
      if(utc>=created&&utc-created<250000000&&expiry>utc){
       r2_group_admission_require_anchor();r2_relay_precision_withdraw(&timing);coarse_valid=false;
      }
     }
     continue;
    }
   }
   if(local_path&&(p.origin.boot!=path_boot||p.origin.source_epoch!=path_epoch))continue;
#if defined(R2_AUTO_ROUTE_FIXTURE)
   extern bool r2_auto_route_fixture_rx(uint16_t event,uint64_t now,bool direct);
   if(!r2_auto_route_fixture_rx(r.event,us,own==relay))continue;
#endif
   uint8_t proof[32]={0};memcpy(proof,r.ad+(enrollment.group.id?74:56),enrollment.group.id?16:32);
   if(p.route.boot==upstream_quality.boot&&physical.epoch==now.epoch&&
      now.tick>=physical.tick&&now.tick-physical.tick<=2000000)
    r2_link_quality_rx(&upstream_quality,p.route.train,r.event,us);
   (void)r2_relay_precision_rx(&timing,&p,proof,physical,r.event,r.channel,now,us,utc);
  }
#if defined(R2_RELAY_SIMULATION) || defined(R2_FIELD_FIXTURE)
  if(windowed||(own==relay&&us>=132000000&&us<134000000))bs_trace_raw(0,"R2_RELAY_OWNER_CAPTURE node=%u tx=%u sender=%u tick=%llu event=%u channel=%u owner=%lx train=%u rx_life=%llu tx_life=%llu phase_ns=%lld admitted=%u reservation_us=%u prepare_us=%u window_us=%u publication=%u count=%u origin_ns=%llu rejected=%u\n",
   own,r.tx,sender,(unsigned long long)physical.tick,r.event,r.channel,(unsigned long)r.owner,p.route.train,
   (unsigned long long)rx_lifecycle,(unsigned long long)tx_lifecycle,(long long)timing.phase_ns,timing.phase_admitted,r.reservation_us,r.prepare_us,r.receive_window_us,
   p.route.publication,p.count,(unsigned long long)p.origin.created_ns,timing.rejected+timing.upstream.rejected);
#endif
 }
 /* Keep the bounded eight-record drain before destroying the matching PA
  * owner; otherwise a last in-window correction is discarded as !ready.
  * Supported close runs in this same cycle with its original2s deadline. */
 if(r2_shadow_enabled()){
  struct r2_capture at;if(r2_capture_now(&at)){
   r2_servo_map_t map;struct r2_relay_origin origin;uint64_t lo;
   if(r2_relay_precision_shadow_map(&timing,at,us,&map,&origin,&lo)&&(map.delivery!=witness_delivery||at.epoch!=witness_epoch)){
    uint32_t remain=r2_relay_precision_valid_for(&timing,at,us,250000);
    if(remain&&at.tick<=UINT64_MAX-(uint64_t)remain*16){
     struct rc_shadow_map frozen={.normal=true,.normal_map=map,.normal_until=at.tick+(uint64_t)remain*16,.map={.effective=map.delivery,.generation=timing.upstream.accepted},.lo=lo,.epoch=at.epoch,
      .parent=enrollment.identity[own==relay?root:relay],.parent_boot=timing.remote.boot,.root=enrollment.identity[root],.root_boot=origin.boot,.root_epoch=origin.source_epoch,.parent_epoch=timing.remote.capture_epoch,.train=timing.remote.train,.accepted=timing.upstream.accepted,.issued=origin.created_ns,.utc_offset=origin.utc_offset_ns};
     r2_shadow_normal_map(&frozen);witness_delivery=map.delivery;witness_epoch=at.epoch;
    }
   }
  }
 }
 uint64_t close_now=(uint64_t)k_ticks_to_us_floor64(k_uptime_ticks());
 bool owned_before_close=windowed&&(sync_pending()||scan_started);
 bool overdue=owned_before_close&&close_now>=quiet_deadline;
 if(windowed&&close_now>=window_end)follower(close_now,false);
 uint64_t closed_now=(uint64_t)k_ticks_to_us_floor64(k_uptime_ticks());
 if(overdue||(owned_before_close&&closed_now>=quiet_deadline)){
  miss_window(closed_now,quiet_deadline,3);return;
 }
 if(error){r2_relay_service_stop(closed_now);return;}
 uint64_t source_ns,source_revision,source_expiry;
 bool root_live=own==root&&authority.gm&&authority.root==root&&authority.boot==own_boot&&authority.until_us>us&&coarse_fresh&&
  r2_source_clock_read(source,now,&source_ns,&source_revision)&&r2_source_clock_expiry(source,now,&source_expiry);
 /* Exhausting supported phase-placement attempts withdraws downstream only.
  * A new explicit activation permits a new bounded acquisition;
  * the old request cannot keep retrying or extend root time authority. */
 if(refused_activation&&activation_until!=refused_activation&&activation_until>us){
  refused_activation=0;acquisition_retries=0;phase_failures=0;
 }
 int64_t recovery_ns,recovery_utc;
 r2_recovery_update(&timing.recovery,r2_relay_precision_read(&timing,now,us,&recovery_ns,&recovery_utc),us);
 bool relay_live=!timing.passive_receiver&&(!local_path||(activation_until&&us<activation_until))&&own==relay&&!refused_activation&&(!activation_until||us<activation_until)&&coarse_fresh&&timing.origin_valid&&!timing.failed&&now.tick<timing.origin_until_tick;
 if(relay_live&&!tx_lifecycle&&!initial_start_deferred){
  /* One bounded supported request toward the middle of the declared cell.
   * It also leaves publication time after the upstream RX completion. The
   * achieved RADIO phase, never this host request, decides admission. */
  initial_start_deferred=true;retry_at=us+7000;
 }
 /* Receiving a renewed root envelope precedes its matching correction.
  * Keep the train discoverable with count0 acquisition while no matching map
  * is fresh. This never resets the estimator, forwards a correction, or
  * extends the immutable deadline. Every actual TX still needs fresh phase
  * admission before it can produce a correction. */
 if(own!=leaf){
  if(pa&&data_until_tick&&now.tick>=data_until_tick){pa_close(3);r2_relay_precision_downstream_withdraw(&timing);}
  if(!root_live&&!relay_live){if(pa){pa_close(4);
    if(own==root)r2_relay_precision_withdraw(&timing);
    else r2_relay_precision_downstream_withdraw(&timing);
    retry_at=us+250000;}}
  else if(!pa&&us>=retry_at){
   /* Retry requests are bounded to three per acquisition. They can establish
    * an envelope, not arbitrary exact-anchor API semantics. */
   pa_start(source,now,authority,utc,us);
  }else if(pa&&(published_correction!=timing.tx_corrections||us-updated_at>=62500))
   (void)publish(source,now,authority,utc,us);
  if(own==relay&&phase_failures>=3){
   pa_close(5);phase_failures=0;
   r2_math_inc(&retries);if(++acquisition_retries>=3){
    {refused_activation=activation_until?activation_until:UINT64_MAX;
     r2_relay_precision_downstream_withdraw(&timing);return;}
   }
   uint32_t delay;uint64_t retry_now=(uint64_t)k_ticks_to_us_floor64(k_uptime_ticks());
   if(!r2_relay_phase_retry(retry_now,pa_requested_at,timing.phase_ns,&delay)){fault(-ERANGE);return;}
   retry_at=retry_now+delay;
#if defined(R2_FIELD_FIXTURE)
   bs_trace_raw(0,"FIELD_PHASE_RETRY t=%llu requested=%llu measured_ns=%lld delay_us=%u attempt=%u\n",(unsigned long long)us,(unsigned long long)pa_requested_at,(long long)timing.phase_ns,delay,acquisition_retries);
#endif
  }
 }
}
void r2_relay_service_snapshot(struct r2_relay_status *out,struct r2_radio_precision_view *precision,uint64_t us){
 struct r2_capture now={0};struct r2_radio_capture_stats stats;r2_radio_capture_stats(&stats);
 bool captured=r2_capture_now_at(&now,&view.sampled_us);
 /* Software UTC projection is anchored to this actual capture, not to the
  * earlier cycle-start clock sampled before expensive publication work.
  * Retain the older us for conservative validity/source deadlines below. */
 int64_t ns=0,utc=0;
 bool map=captured&&active&&coarse_valid&&!error&&r2_relay_precision_read(&timing,now,us,&ns,&utc);
 r2_relay_precision_view(&timing,now,us,precision);
 precision->captured=stats.completed;precision->invalid=stats.invalid;
 view.recovery_lost_at=timing.recovery.lost_at;
 view.recovery_until=timing.recovery.losing?r2_sat_add(timing.recovery.lost_at,R2_V14_RECOVERY_US):0;
 view.recovery_grace_active=r2_recovery_retained(&timing.recovery,map,us);
 view.holdover_bound_known=timing.hold_bound.provenance!=R2_BOUND_UNKNOWN;
 view.held_time=map&&timing.upstream.last_accepted&&now.tick>=timing.upstream.last_accepted&&now.tick-timing.upstream.last_accepted>=R2_RADIO_MAX_AGE_TICKS;
 view.host_notifications=0; /* Retained diagnostic ABI; unexercised callback removed. */
 view.time_relay_hops=map?timing.remote.depth:0;
 view.selected=selected;view.active=active;view.root=root;view.parent=own==root?root:own==relay?root:relay;
 view.root_identity=enrollment.identity[root];view.parent_identity=enrollment.identity[view.parent];
 view.role=own==root?0:own==relay&&!timing.passive_receiver?1:2;view.root_boot=timing.root_boot?timing.root_boot:own==root?own_boot:0;
 view.source_epoch=timing.source_epoch;view.source_revision=timing.revision;
 struct r2_relay_origin origin=timing.origin;
 if(map)(void)r2_relay_precision_selected_origin(&timing,now,us,&origin);
 view.source_created_ns=origin.created_ns;view.source_expires_ns=origin.expires_ns;view.source_frame_ns=origin.frame_ns;
 view.lifecycle=lifecycle;view.rx_lifecycle=rx_lifecycle;view.tx_lifecycle=tx_lifecycle;
 view.upstream_train=timing.remote.train;view.upstream_publication=timing.remote.publication;
 view.train=(own==leaf||timing.passive_receiver)?timing.remote.train:timing.train;view.map_generation=(own==leaf||timing.passive_receiver)?timing.remote.map_generation:timing.map_generation;view.accepted=timing.upstream.accepted;
 view.tx_corrections=timing.tx_corrections;view.rejected=timing.rejected+timing.upstream.rejected;view.duplicates=timing.duplicates;
 view.capture_completed=stats.completed;view.capture_invalid=stats.invalid;view.capture_overflow=stats.overflow;view.flushed=flushed;
 view.cost_refusals=cost_refusals;view.retries=retries;view.phase_ns=timing.phase_ns;view.target_ns=R2_RELAY_SLOT_NS;view.error_ns=timing.phase_ns-(int64_t)R2_RELAY_SLOT_NS;
 view.phase_admitted=timing.phase_admitted;view.bound_ns=timing.bound;view.map_valid=map;view.utc_us=map&&utc>0?(uint64_t)utc/1000:0;
 view.source_until_us=captured&&active&&coarse_valid&&!error&&timing.origin_valid&&now.tick<timing.origin_until_tick?
  us+(timing.origin_until_tick-now.tick)/16:0;
 view.valid_until_us=map?us+r2_relay_precision_valid_for(&timing,now,us,250000):0;
 view.window_faults=window_faults;view.window_recoveries=window_recoveries;view.window_fault_site=window_fault_site;view.fault_line=fault_line;
 view.window_fault_at=window_fault_at;view.window_fault_deadline=window_fault_deadline;view.window_recovering=window_recovering;
 view.update_age_ms=timing.upstream.last_available&&now.tick>=timing.upstream.last_available?(uint32_t)((now.tick-timing.upstream.last_available)/16000):UINT32_MAX;
 view.source_age_ms=map&&ns>=0&&(uint64_t)ns>=origin.created_ns?((uint64_t)ns-origin.created_ns)/1000000:UINT32_MAX;
 view.correction_age_ms=precision->correction_age_valid?precision->correction_age_ms:UINT32_MAX;view.error=error;
 if(!active||error||!coarse_valid){precision->authorized=precision->map_valid=precision->rms_valid=precision->locked=false;view.map_valid=false;}
 *out=view;
}

int r2_relay_service_release_path(void){
 if(!local_path||error||!r2_relay_service_closed())return -EBUSY;
 if(timing.train>train_floor)train_floor=timing.train;
 r2_ble_field_admission_window(true,false);
 selected=false;local_path=false;return 0;
}

int r2_relay_service_profile_reset(uint64_t boot){
 if(!boot||error||!r2_relay_service_closed())return -EBUSY;
 selected=local_path=passive_observer=continuous_receiver=false;own_boot=boot;activation_until=refused_activation=train_floor=0;
 path_boot=path_epoch=witness_delivery=witness_epoch=0;coarse_valid=false;r2_shadow_normal_withdraw();return 0;
}

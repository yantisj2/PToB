/* SPDX-License-Identifier: Apache-2.0
 * Explicit three-unit field profile. One controller owner; no GM election.
 * TIMER3 and the native observation adapter supply every timing capture.
 */
#include "ble_service.h"
#include "capture.h"
#include "radio_capture.h"
#include "radio_audit.h"
#include "core.h"
#include "time_service.h"
#include "rc_boundary.h"
#include "rc_field_policy.h"
#include "rc_field_profile.h"
#include "rc_direct_calibration.h"
#include "rc_gm_adapter.h"
#include "rc_shadow.h"
#if defined(R2_RC_AUTO)
#include "rc_slots.h"
#endif
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/atomic.h>
static struct rc5_routes routes;
static struct rc5_state timing;
static struct rc5_rate_fit fit;
static uint32_t fit_rms=RC5_UNKNOWN;static bool fit_rms_valid;
static struct rc5_clock local_clock;
static struct r2_group_config group;
static struct r2_service_status status;
static struct r2_source_clock source_input,source_work;
static struct r2_clock clock_input,clock_work;
static struct r2_identity identity;
static unsigned node,parent_index;
static int32_t direct_phase_bias;
static uint32_t root_epoch,capture_epoch;
static uint64_t rebind_parent_boot,rebind_root_boot;
static uint32_t rebind_root_epoch,rebind_capture_epoch;
static uint64_t fit_parent_boot,fit_root_boot;
static uint32_t fit_parent_epoch,fit_root_epoch;
static struct rc5_packet published;
static struct rc5_origin current_origin;
static uint8_t current_proof[16];
static struct rc5_correction sent[2];
static unsigned sent_count,publication,train=1,rx,tx,restarts,wire_rejected,stale_tx;
static uint32_t reservation_max,prepare_max,window_max;
static uint64_t publish_ticks,discipline_at,discipline_gap_max,usable_tx_from;
static uint32_t holdover_expiries,holdover_recoveries;
static bool holdover_expired;
/* Explicit operator operating policy; not a measured precision bound. */
#define RC_FIELD_HOLD_TICKS UINT64_C(8000000)
static void holdover_poll(uint64_t tick){
 if(discipline_at&&tick>=discipline_at&&tick-discipline_at>=RC_FIELD_HOLD_TICKS&&!holdover_expired){
  holdover_expired=true;++holdover_expiries;sent_count=0;usable_tx_from=tick;
 }
}
static bool start_prepared,phase_admitted;
static uint64_t root_frame,last_rx,phase_rx,start_delay,requested,restart_at;
static int64_t slot_target_ns,parent_slot_delta_ns,phase;
static bool awaiting_first_tx,running,scanning,creating,parent_synced,found,parent_known;
static const bool local_demand=true;
static struct bt_le_per_adv_sync *sync;
static struct bt_le_ext_adv *pa;
static bt_addr_le_t parent;
static atomic_t fault,stop_requested,started;
static struct k_thread owner_thread;
K_THREAD_STACK_DEFINE(owner_stack,8192);
K_MUTEX_DEFINE(state_lock);
static uint64_t mono_us(void){return k_ticks_to_us_floor64(k_uptime_ticks());}
static void check(bool ok,const char *why){(void)why;if(!ok)atomic_cas(&fault,0,-EIO);}
static bool root_read(const struct r2_capture *event,uint64_t *ns,uint64_t *generation){
 if(node||!clock_work.view.valid||!clock_work.view.source_available||clock_work.fatal||clock_work.stopped)return false;
 return r2_source_clock_read(&source_work,*event,ns,generation);
}
#if defined(R2_RC_AUTO)
static struct rc_slots slot_state;
/* Operator diagnostic override: continuously listen; normal RC stays online. */
static const bool rc6_client_continuous=true;
static bool slot_selected,slot_change_requested,slot_scan_failed,slot_move_pending;
static uint64_t slot_move_until;
static uint64_t upstream_retry_at;
static uint32_t client_quiet_faults,slot_requests,upstream_recoveries;
static bool stop(void);
static uint16_t slot_bits(void){uint16_t bits=0;for(unsigned i=0;i<routes.count;i++)if(routes.node[i].transmitter)bits|=(uint16_t)(routes.node[i].slot&3)<<(2*i);return bits;}
static bool apply_parent_slots(uint16_t bits){
 struct rc5_routes next=routes;
 for(unsigned i=1;i<next.count;i++)if(next.node[i].transmitter&&i!=node){unsigned slot=(bits>>(2*i))&3;if(!slot)return false;next.node[i].slot=slot;}
 if(next.node[node].transmitter&&next.node[node].slot<=next.node[next.node[node].parent].slot)next.node[node].slot=next.node[next.node[node].parent].slot+1;
 if(!rc5_routes_valid(&next))return false;
 bool own_changed=next.node[node].slot!=routes.node[node].slot;
 if(own_changed&&running){
  if(!stop())return false;
  sent_count=0;++train;++restarts;phase_admitted=false;restart_at=0;slot_selected=false;
 }
 bool changed=parent_known&&next.node[parent_index].slot!=routes.node[parent_index].slot;
 if(changed&&sync){int rc=bt_le_per_adv_sync_delete(sync);if(rc)return false;sync=NULL;creating=parent_synced=found=false;}
 routes=next;timing.routes=next;
 if(routes.node[node].transmitter){
  if(!slot_selected)rc_slots_init(&slot_state,routes.node[node].slot,routes.node[parent_index].slot);
  slot_target_ns=routes.node[node].slot*15625000;
  parent_slot_delta_ns=(routes.node[node].slot-routes.node[parent_index].slot)*15625000;
 }
 return true;
}
static bool set_local_slot(uint8_t slot){
 struct rc5_routes next=routes;next.node[node].slot=slot;
 for(unsigned i=node+1;i<next.count;i++)if(next.node[i].transmitter&&next.node[i].slot<=next.node[next.node[i].parent].slot)next.node[i].slot=next.node[next.node[i].parent].slot+1;
 if(!rc5_routes_valid(&next))return false;
 routes=next;timing.routes=next;slot_target_ns=slot*15625000;parent_slot_delta_ns=(slot-routes.node[parent_index].slot)*15625000;return true;
}
static bool downstream_ready(uint64_t tick){struct rc5_stamp st;return running&&phase_admitted&&sent_count&&tx&&rc5_clock_capture(&local_clock,&timing,tick,&st);}
static bool stop(void);
static void slot_hint_update(void);
#endif
#include "rc_field_transport.inc"
/* Bluetooth callbacks only enqueue bounded data. PSK checks run on owner. */
struct discovery {bt_addr_le_t address;uint16_t interval;uint8_t sid,len;int8_t rssi;uint8_t data[RC5_CONTROL_BYTES];};
K_MSGQ_DEFINE(discovery_queue,sizeof(struct discovery),4,4);
static bool scan_data(struct bt_data *d,void *user){struct discovery *p=user;if(d->type==BT_DATA_MANUFACTURER_DATA&&(d->data_len==RC5_CONTROL_BYTES||(d->data_len==23))){p->len=d->data_len;memcpy(p->data,d->data,p->len);return false;}return true;}
static void scan(const struct bt_le_scan_recv_info *i,struct net_buf_simple *ad){
#if defined(R2_SWITCHABLE_FIELD)
 if(!r2_switch_rc_active())return;
#endif
 struct discovery d={.address=*i->addr,.interval=i->interval,.sid=i->sid,.rssi=i->rssi};bt_data_parse(ad,scan_data,&d);
 if(d.len||i->interval)(void)k_msgq_put(&discovery_queue,&d,K_NO_WAIT);
}
static struct bt_le_scan_cb scan_cb={.recv=scan};
struct sync_event {struct bt_le_per_adv_sync *sync;bt_addr_le_t address;bool ended;};
K_MSGQ_DEFINE(sync_queue,sizeof(struct sync_event),4,4);
static void ready(struct bt_le_per_adv_sync *s,struct bt_le_per_adv_sync_synced_info *i){
#if defined(R2_SWITCHABLE_FIELD)
 if(!r2_switch_rc_active())return;
#endif
struct sync_event e={.sync=s,.address=*i->addr};check(!k_msgq_put(&sync_queue,&e,K_NO_WAIT),"sync queue");}
static void ended(struct bt_le_per_adv_sync *s,const struct bt_le_per_adv_sync_term_info *i){
#if defined(R2_SWITCHABLE_FIELD)
 if(!r2_switch_rc_active())return;
#endif
(void)i;struct sync_event e={.sync=s,.ended=true};check(!k_msgq_put(&sync_queue,&e,K_NO_WAIT),"sync end queue");}
static struct bt_le_per_adv_sync_cb sync_cb={.synced=ready,.term=ended};
static void discover(void){
 struct sync_event e;while(!k_msgq_get(&sync_queue,&e,K_NO_WAIT)){
  if(e.ended){if(sync==e.sync){sync=NULL;creating=parent_synced=found=false;}}
  else if(creating&&sync==e.sync&&parent_known&&!bt_addr_le_cmp(&e.address,&parent)){parent_synced=true;creating=false;}
  /* Deletion/cancellation may leave a queued callback; never revive it. */
 }
 struct discovery d;while(!k_msgq_get(&discovery_queue,&d,K_NO_WAIT)){
  if(node&&routes.node[node].parent==0&&d.len==23&&!parent_known&&d.data[0]=='G'&&d.data[1]=='1'&&
    r2_group_get(d.data+3,8)==group.id&&r2_group_get(d.data+11,4)==group.epoch&&r2_group_get(d.data+15,8)==routes.node[0].id){
   /* Discovery is only an address hint. JOIN must authenticate source/boot. */
   parent_index=0;parent=d.address;parent_known=true;parent_slot_delta_ns=routes.node[node].transmitter?routes.node[node].slot*15625000:0;
  }
  if(d.len==RC5_CONTROL_BYTES){struct rc5_control c;
#if defined(R2_RC_AUTO)
   uint8_t occupied;
   if(rc6_occupancy_hint(d.data,d.len,&group,local_boot,&occupied))
    rc_slots_seen(&slot_state,occupied,d.rssi,mono_us());
#endif
   if(parent_known||!node||routes.node[node].parent==0)continue;
   if(!rc5_control_decode(&c,d.data,d.len,&routes,&group)&&c.type==RC5_CAPABILITY&&c.sender<routes.count){
    const struct rc5_node *candidate=&routes.node[c.sender];
    /* Role policy, never a name/address/boot match: the leaf requires RC. */
    bool eligible=rc5_field_candidate(&routes,node,c.sender);
    if(eligible){
#if defined(R2_RC_AUTO)
     if(!apply_parent_slots(c.slots))continue;
#endif
     parent_index=c.sender;parent=d.address;parent_known=true;
     routes.node[node].parent=parent_index;timing.routes=routes;
     parent_slot_delta_ns=routes.node[node].transmitter?(routes.node[node].slot-candidate->slot)*15625000:0;
    }
   }
  }
  if(parent_known&&d.sid==12&&d.interval==50&&!bt_addr_le_cmp(&d.address,&parent))found=true;
 }
}
static void publish(void){
 struct r2_capture now;if(!r2_capture_now(&now))return;
 holdover_poll(now.tick);
 if(!node){uint64_t ns,generation;if(!root_read(&now,&ns,&generation))return;
  if(!current_origin.expires||ns-current_origin.issued>=250000000){
   if(ns>UINT64_MAX-RC5_SOURCE_NS||clock_work.view.utc_us<0||clock_work.view.utc_us>INT64_MAX/1000)return;
   int64_t utc=clock_work.view.utc_us*1000;
   uint64_t sampled=(uint64_t)clock_work.view.mono_us,at=mono_us();
   if(at<sampled||at-sampled>(uint64_t)(INT64_MAX-utc)/1000)return;
   utc+=(int64_t)(at-sampled)*1000;
   if(ns>INT64_MAX)return;
   current_origin=(struct rc5_origin){.root=routes.node[0].id,.boot=local_boot,.epoch=root_epoch,.issued=ns,.expires=ns+RC5_SOURCE_NS,.frame=root_frame,.utc_offset=utc-(int64_t)ns};
   check(!rc5_origin_sign(current_proof,&current_origin,&routes,&group),"root proof");
  }
 }
 if(!current_origin.expires)return;
 if(publication==UINT32_MAX){check(false,"publication overflow");return;}
 published=(struct rc5_packet){.origin=current_origin,.sender=node,.boot=local_boot,.train=train,.publication=++publication,.epoch=capture_epoch};memcpy(published.proof,current_proof,16);
 for(unsigned i=0;!holdover_expired&&i<sent_count;i++)published.correction[published.count++]=sent[i];
 uint8_t ad[RC5_BYTES];if(rc5_encode(ad,&published,&routes,&group)){check(false,"encode");return;}
 struct bt_data d={.type=BT_DATA_MANUFACTURER_DATA,.data_len=188,.data=ad+2};check(!bt_le_per_adv_set_data(pa,&d,1),"PA data");
 struct r2_capture done;if(r2_capture_now(&done)&&done.tick>=now.tick)publish_ticks=done.tick-now.tick;
}
static void create(void){
 struct bt_le_adv_param p={.id=0,.sid=12,.options=BT_LE_ADV_OPT_EXT_ADV|BT_LE_ADV_OPT_USE_IDENTITY|BT_LE_ADV_OPT_NO_2M,.interval_min=32,.interval_max=32};
 struct bt_le_per_adv_param periodic={.interval_min=50,.interval_max=50};
 int rc=bt_le_ext_adv_create(&p,NULL,&pa);if(rc){check(false,"PA create");return;}
 check(!bt_le_per_adv_set_param(pa,&periodic),"PA interval");publish();
}
static void start(void){struct r2_capture now;if(!pa||!r2_capture_now(&now))return;requested=now.tick;awaiting_first_tx=true;
 int rc=0;
#if defined(R2_RC_AUTO) && defined(R2_RC_FIELD_TEST)
 extern int r2_rc_auto_start_fault(unsigned,uint64_t,bool);rc=r2_rc_auto_start_fault(node,now.tick,slot_move_pending);
#endif
 if(!rc)rc=bt_le_per_adv_start(pa);
 if(rc){
#if defined(R2_RC_AUTO)
  if(slot_move_pending&&(rc==-EAGAIN||rc==-EBUSY)){slot_move_until=now.tick;return;}
#endif
  check(false,"PA start");return;
 }
 running=true;check(!bt_le_ext_adv_start(pa,BT_LE_EXT_ADV_START_DEFAULT),"extended start");
#if defined(R2_RC_AUTO)
 slot_hint_update();
#endif
}
static bool stop(void){if(!running)return true;
 int extended=bt_le_ext_adv_stop(pa),periodic=bt_le_per_adv_stop(pa);
 bool closed=!extended&&!periodic;check(closed,"PA owners stopped");if(closed){running=false;start_prepared=false;
#if defined(R2_RC_AUTO)
 slot_hint_update();
#endif
 }return closed;
}
static void captures(struct r2_capture now){
 struct r2_radio_record r;while(r2_radio_capture_pop(&r)){
  struct r2_capture physical;struct rc5_packet packet;
  if(!r.owner||r.length!=RC5_BYTES||!r2_capture_radio(r.raw,r.epoch,&physical)){++wire_rejected;continue;}
#if defined(R2_RC_AUTO)
  if(node&&!routes.node[node].transmitter&&!rc6_client_continuous&&physical.tick/16%1000000>=400000){++client_quiet_faults;check(false,"physical precision event outside client window");continue;}
#endif
  bool decoded=node&&parent_index==0&&!r.tx?rc5_gm_packet(&gm_context,&timing,&packet,r.ad,r.length,&group,now.tick):!rc5_decode(&packet,r.ad,r.length,&routes,&group);
  if(!decoded){if(node&&parent_index==0&&!r.tx&&!gm_context.authenticated)lt_renew_at=0;++wire_rejected;continue;}
  if(r.reservation_us>reservation_max)reservation_max=r.reservation_us;
  if(r.prepare_us>prepare_max)prepare_max=r.prepare_us;
  if(r.receive_window_us>window_max)window_max=r.receive_window_us;
  if(r.tx){
   /* A drained controller can still leave queued records from its old train.
    * They must never calibrate this start or enter its correction history. */
   if(!rc5_field_tx_current(&packet,node,local_boot,train,capture_epoch,running,physical.tick,requested)){++stale_tx;continue;}
   ++tx;if(awaiting_first_tx){start_delay=physical.tick-requested;awaiting_first_tx=false;}
   if(node&&phase_rx){struct rc5_stamp st;
    bool known=rc5_clock_capture(&local_clock,&timing,physical.tick,&st)&&st.ns>=current_origin.frame;
    if(known)phase=(int64_t)((st.ns-current_origin.frame)%RC5_FRAME_NS);
    int64_t clearance=15625000/2-(int64_t)r.reservation_us*1000,error=slot_target_ns-phase;
    if(clearance<=0){check(false,"reservation exceeds slot");return;}
    phase_admitted=known&&error<=clearance&&error>=-clearance;
    if(known&&!phase_admitted){
     lt_acquiring=true;lt_quiesce(true);if(!stop())return;sent_count=0;++train;++restarts;
     uint64_t base=physical.tick-start_delay+(uint64_t)((int64_t)1000000+error*2/125);
     while(base<now.tick+32000)base+=1000000;
     restart_at=base;continue;
    }else{lt_acquiring=false;lt_quiesce(false);}
   }
   struct rc5_correction c={0};bool usable=false;
   if(!node){uint64_t ns,generation;usable=root_read(&physical,&ns,&generation);
    if(usable){root_frame=ns;c=(struct rc5_correction){.publication=packet.publication,.event=r.event,.channel=r.channel,.generation=(uint32_t)generation,.tx_ns=ns,.bound=RC5_UNKNOWN};}}
   else {struct rc5_stamp st;usable=!holdover_expired&&physical.tick>=usable_tx_from&&rc5_clock_capture(&local_clock,&timing,physical.tick,&st);
    if(usable)c=(struct rc5_correction){.publication=packet.publication,.event=r.event,.channel=r.channel,.generation=st.generation,.age_us=st.age_us,.tx_ns=st.ns,.bound=st.bound};}
   if(!usable)sent_count=0;
   else {if(sent_count)sent[1]=sent[0];sent[0]=c;if(sent_count<2)++sent_count;}
   if(running)publish();
  }else if(parent_known&&packet.sender==parent_index){
#if defined(R2_RC_AUTO)
   if(parent_index&&packet.slot!=routes.node[parent_index].slot){lt_renew_at=0;continue;}
#endif
   if(timing.bound&&(packet.boot!=timing.parent_boot||packet.origin.boot!=timing.root_boot||packet.origin.epoch!=timing.root_epoch||packet.epoch!=timing.parent_epoch||packet.train!=timing.parent_train)&&
      (packet.boot!=rebind_parent_boot||packet.origin.boot!=rebind_root_boot||packet.origin.epoch!=rebind_root_epoch||packet.epoch!=rebind_capture_epoch||packet.train!=timing.parent_train)){
    /* New authenticated scope prompts a challenge; held output stays local. */
    rebind_parent_boot=packet.boot;rebind_root_boot=packet.origin.boot;rebind_root_epoch=packet.origin.epoch;rebind_capture_epoch=packet.epoch;lt_renew_at=0;
   }
   ++rx;last_rx=phase_rx=physical.tick;struct r2_capture available;if(!r2_capture_now(&available))continue;
   if(r2_radio_audit_selected(r.event)){
    struct r2_radio_audit_record q={.kind=R2_AUDIT_RX,.peer_boot=packet.boot,.root_boot=packet.origin.boot,
     .epoch=r.epoch,.peer_epoch=packet.epoch,.root_epoch=packet.origin.epoch,.train=packet.train,
     .publication=packet.publication,.event=r.event,.channel=r.channel,.address_raw=r.address_raw,.raw=r.raw,
     .tick=physical.tick,.available=available.tick,.crc=r2_crc32(r.ad,r.length)};r2_radio_audit_put(&q);
   }
   if(rc5_receive(&timing,&packet,physical.tick,r.event,r.channel,available.tick)){
    if(fit_parent_boot!=timing.parent_boot||fit_root_boot!=timing.root_boot||fit_parent_epoch!=timing.parent_epoch||fit_root_epoch!=timing.root_epoch){
     memset(&fit,0,sizeof fit);sent_count=0;fit_parent_boot=timing.parent_boot;fit_root_boot=timing.root_boot;fit_parent_epoch=timing.parent_epoch;fit_root_epoch=timing.root_epoch;
    }
    current_origin=timing.reference.origin;memcpy(current_proof,timing.reference.proof,16);(void)rc5_rate_add(&fit,&timing.reference);
    fit_rms_valid=rc5_rate_rms(&fit,&fit_rms);
    /* Hardware TX/RX ADDRESS captures: no simulator bias constant imported. */
    struct rc5_rate rate={.ppb=fit.ppb,.rx_tx_bias_ns=direct_phase_bias,.bias_bound_ns=RC5_UNKNOWN,.uncertainty_ppb=RC5_UNKNOWN,.capture_ns=RC5_UNKNOWN,.link_bound=RC5_UNKNOWN};
    holdover_poll(available.tick);
    struct r2_capture activation;bool activation_known=r2_capture_now(&activation);
    bool disciplined=rc5_clock_discipline(&local_clock,&timing,&fit,&rate);
    if(disciplined){
     if(activation_known)r2_shadow_map(&local_clock,&timing,activation.tick,activation.epoch);
     uint64_t accepted_at=timing.reference.completed_at;
     if(discipline_at&&accepted_at>=discipline_at&&accepted_at-discipline_at>discipline_gap_max)discipline_gap_max=accepted_at-discipline_at;
     if(holdover_expired){++holdover_recoveries;sent_count=0;usable_tx_from=accepted_at;}
     discipline_at=accepted_at;holdover_expired=false;
    }
    if(r2_radio_audit_selected(timing.reference.event)){
     struct r2_capture done={0};(void)r2_capture_now(&done);
     struct r2_radio_audit_record q={.kind=R2_AUDIT_MATCH,.peer_boot=timing.parent_boot,.root_boot=timing.root_boot,
      .epoch=r.epoch,.peer_epoch=timing.parent_epoch,.root_epoch=timing.root_epoch,.train=timing.parent_train,
      .publication=timing.reference.publication,.event=timing.reference.event,.channel=timing.reference.channel,
      .address_raw=(uint32_t)timing.reference.rx,.raw=(uint32_t)timing.reference.rx,
      .tick=timing.reference.rx,.ns=timing.reference.parent_tx_ns,.generation=timing.reference.generation,
      .available=timing.reference.completed_at,.original_available=timing.reference.original_available,.followup=timing.reference.followup_rx,
      .activation_lo=activation_known?activation.tick:0,.activation_hi=done.epoch==r.epoch?done.tick:0,
      .disciplined=disciplined};r2_radio_audit_put(&q);
    }
   }
  }
 }
}
static void snapshot_publish(struct r2_capture now){
 struct rc5_stamp stamp={0};bool valid=node&&rc5_clock_read(&local_clock,&timing,now.tick,&stamp);
 uint64_t utc=0;if(valid){if(stamp.ns>INT64_MAX)valid=false;else{int64_t ns=(int64_t)stamp.ns,off=current_origin.utc_offset;if((off>0&&ns>INT64_MAX-off)||(off<0&&(uint64_t)ns<(uint64_t)(-(off+1))+1))valid=false;else utc=(uint64_t)(ns+off)/1000;}}
 k_mutex_lock(&state_lock,K_FOREVER);
 status.at=mono_us();status.cycles++;status.rx=rx;status.authenticated=timing.accepted;status.rejected=wire_rejected+timing.rejected;
 status.serving=!node&&running;status.follower=node&&valid;status.follower_clock_valid=valid;status.follower_utc=utc;status.time_sampled_us=status.at;
 status.source_epoch=current_origin.epoch;status.root_boot=current_origin.boot;status.source_frame_ns=current_origin.frame;
 uint64_t remaining=valid&&now.tick>=discipline_at&&now.tick-discipline_at<RC_FIELD_HOLD_TICKS?RC_FIELD_HOLD_TICKS-(now.tick-discipline_at):0;
 status.service_until=valid&&status.at<=UINT64_MAX-remaining/16?status.at+remaining/16:0;status.authority_until=status.service_until;
 status.source_eligible=!node&&clock_work.view.source_available;status.peer_delivery_confirmed=lt_responses>0;
 status.service_receipts=lt_bindings;status.precision.accepted=timing.accepted;status.precision.map_valid=valid;status.precision.authorized=valid;status.precision.locked=valid&&fit.valid&&stamp.pll_tracking;
 status.precision.fit_valid=fit.valid;status.precision.samples=fit.count;status.precision.rms_valid=valid&&fit_rms_valid;status.precision.rms_ns=fit_rms;
 status.precision.drift_valid=fit.valid;status.precision.drift_ppb=fit.ppb;status.precision.trim_ppb=local_clock.count?local_clock.map[(local_clock.head+3)%4].ppb:0;
 status.precision.correction_age_valid=valid;status.precision.correction_age_ms=stamp.age_us/1000;
 status.relay.selected=parent_known;status.relay.active=running;status.relay.parent=parent_index;status.relay.accepted=timing.accepted;status.relay.tx_corrections=tx;
 uint64_t update_ms=timing.reference.valid&&now.tick>=timing.reference.completed_at?(now.tick-timing.reference.completed_at)/16000:UINT64_MAX;
 uint64_t source_ms=valid&&stamp.ns>=current_origin.issued?(stamp.ns-current_origin.issued)/1000000:UINT64_MAX;
 status.relay.update_age_ms=update_ms>UINT32_MAX?UINT32_MAX:(uint32_t)update_ms;status.relay.source_age_ms=source_ms>UINT32_MAX?UINT32_MAX:(uint32_t)source_ms;
 status.relay.held_time=valid&&stamp.ns>=current_origin.expires;
 status.relay.root_identity=valid?current_origin.root:0;status.relay.parent_identity=timing.bound?routes.node[routes.node[node].parent].id:0;
 status.relay.root_boot=current_origin.boot;status.relay.source_epoch=current_origin.epoch;status.relay.source_created_ns=current_origin.issued;status.relay.source_expires_ns=current_origin.expires;status.relay.source_frame_ns=current_origin.frame;
 status.relay.upstream_train=timing.parent_train;status.relay.train=train;status.relay.map_generation=stamp.generation;status.relay.root=0;status.relay.role=routes.node[node].transmitter?1:2;
 status.relay.phase_ns=phase;status.relay.target_ns=slot_target_ns;status.relay.error_ns=slot_target_ns-phase;status.relay.phase_admitted=phase_admitted;
 #if defined(R2_RC_AUTO)
 status.relay.upstream_recoveries=upstream_recoveries;status.relay.sync_creating=creating;status.relay.sync_active=parent_synced;status.relay.phase_acquiring=lt_acquiring;
 status.relay.quiet=node&&!routes.node[node].transmitter&&!rc6_client_continuous&&now.tick/16%1000000>=320000;status.relay.slot=routes.node[node].slot;status.relay.slot_scans=slot_state.scans;status.relay.slot_scan_verified=slot_state.verified;status.relay.slot_scan_failures=slot_state.scan_failures;status.relay.slot_scan_owned_us=slot_state.scan_owned_total_us;status.relay.slot_changes=slot_state.changes;status.relay.slot_rollbacks=slot_state.rollbacks;status.relay.slot_busy=slot_state.busy;status.relay.slot_requests=slot_requests;status.relay.client_quiet_faults=client_quiet_faults;
#endif
 status.relay.retries=restarts;status.relay.start_delay_ticks=start_delay;status.relay.publish_ticks=publish_ticks;status.relay.stale_tx=stale_tx;
 status.relay.discipline_gap_max_ticks=discipline_gap_max;status.relay.holdover_expiries=holdover_expiries;status.relay.holdover_recoveries=holdover_recoveries;
 status.relay.reservation_max_us=reservation_max;status.relay.prepare_max_us=prepare_max;status.relay.rx_window_max_us=window_max;
 status.relay.correction_age_ms=stamp.age_us/1000;status.relay.map_valid=valid;status.relay.utc_us=utc;status.relay.bound_ns=RC5_UNKNOWN;
 k_mutex_unlock(&state_lock);
}
static void owner(void *a,void *b,void *c){(void)a;(void)b;(void)c;
 static bool callbacks_registered;
 if(!callbacks_registered){bt_le_scan_cb_register(&scan_cb);bt_le_per_adv_sync_cb_register(&sync_cb);callbacks_registered=true;}lt_init();
 while(!atomic_get(&stop_requested)&&!atomic_get(&fault)){
  k_mutex_lock(&state_lock,K_FOREVER);source_work=source_input;clock_work=clock_input;uint64_t deadline=status.deadline;k_mutex_unlock(&state_lock);
  if(deadline&&mono_us()>=deadline)break;
  struct r2_capture now;if(!r2_capture_now(&now)){if(r2_capture_error())check(false,"capture owner failed");k_sleep(K_MSEC(1));continue;}
  if(!capture_epoch)capture_epoch=(uint32_t)now.epoch;else if(capture_epoch!=now.epoch){check(false,"capture epoch changed");continue;}
  if(!node&&root_epoch!=clock_work.view.source_epoch){stop();sent_count=0;current_origin.expires=0;root_frame=0;root_epoch=clock_work.view.source_epoch;++train;}
  discover();
#if defined(R2_RC_AUTO)
  bool leaf=node&&!routes.node[node].transmitter;
  uint64_t us=now.tick/16,offset=us%1000000;
  bool window=rc6_client_continuous||!leaf||offset<320000;
  if(leaf&&!window){
   if(scanning){int rc=bt_le_scan_stop();check(!rc,"client scan quiet");if(!rc)scanning=false;}
   if(sync){int rc=bt_le_per_adv_sync_delete(sync);check(!rc,"client sync quiet");if(!rc){sync=NULL;creating=parent_synced=found=false;}}
   if(offset>=400000&&(lt_conn||scanning||sync)){++client_quiet_faults;check(false,"client quiet deadline");}
  }
  lt_guard(now.tick,window);lt_poll(now.tick,window);
#else
  bool window=true;lt_guard(now.tick,true);lt_poll(now.tick,true);
#endif
  uint64_t ns,generation;bool root_ready=!node&&root_epoch&&root_read(&now,&ns,&generation);
  if(running&&(!lt_live(now.tick)||(!node&&!root_ready))){stop();sent_count=0;++train;}
  if(root_ready&&!running&&lt_live(now.tick)&&!lt_conn){if(!pa)create();publish();if(publication)start();}
  if(node&&scanning&&!lt_conn&&lt_live(now.tick)&&found&&!creating&&!sync){
   struct bt_le_per_adv_sync_param q={.addr=parent,.sid=12,.skip=0,.timeout=25};int rc=bt_le_per_adv_sync_create(&q,&sync);
   if(!rc)creating=true;else if(rc!=-EAGAIN&&rc!=-EBUSY)check(false,"sync create");
  }
  if(parent_synced&&scanning
#if defined(R2_RC_AUTO)
     &&!slot_state.scanning
#endif
    ){check(!bt_le_scan_stop(),"scan stop");scanning=false;}
  if(node&&window&&!creating&&!scanning&&!lt_conn&&!lt_paused
#if defined(R2_RC_AUTO)
     &&(!parent_synced||slot_state.scanning)
#endif
    ){struct bt_le_scan_param q={.type=BT_LE_SCAN_TYPE_PASSIVE,.interval=32,.window=32};int rc=bt_le_scan_start(&q,NULL);
     if(!rc)scanning=true;
#if defined(R2_RC_AUTO)
     else if(slot_state.scanning)slot_scan_failed=true;
     else if(rc!=-EAGAIN&&rc!=-EBUSY)check(false,"scan start");
#else
     else check(false,"scan start");
#endif
    }
  captures(now);
#if defined(R2_RC_AUTO)
  /* Phase placement must never hold recovery paused after its map expires.
   * After a real two-second correction gap, retire a stale synchronization
   * and reuse authenticated admission with the existing bounded/backoff path. */
  if(node&&holdover_expired&&lt_acquiring){lt_acquiring=false;lt_quiesce(false);restart_at=0;start_prepared=false;}
  if(node&&parent_known&&discipline_at&&now.tick>=discipline_at&&now.tick-discipline_at>=32000000&&now.tick>=upstream_retry_at&&!lt_conn){
   if(sync){int rc=bt_le_per_adv_sync_delete(sync);check(!rc,"expired upstream sync drain");if(rc)continue;sync=NULL;}
   creating=parent_synced=found=false;lt_acquiring=false;lt_quiesce(false);lt_renew_at=0;
   upstream_retry_at=now.tick+160000000;++upstream_recoveries;
  }
  rc_slots_listen(&slot_state,now.tick/16,scanning&&!creating&&!lt_conn&&!lt_paused);
  if(node&&routes.node[node].transmitter&&timing.bound&&lt_live(now.tick)&&!lt_conn){
   if((!slot_selected||slot_change_requested)&&!slot_state.scanning&&!slot_move_pending){
    slot_state.parent_slot=routes.node[parent_index].slot;
    if(rc_slots_begin(&slot_state,now.tick/16,slot_selected,(uint32_t)lt_random())){slot_change_requested=false;slot_scan_failed=false;}
   }
   if(slot_state.scanning&&!scanning&&!creating&&!lt_paused){struct bt_le_scan_param q={.type=BT_LE_SCAN_TYPE_PASSIVE,.interval=32,.window=32};int rc=bt_le_scan_start(&q,NULL);if(!rc)scanning=true;else slot_scan_failed=true;}
   if(slot_state.scanning&&rc_slots_finish(&slot_state,now.tick/16,(uint32_t)lt_random())){
    if(scanning){int rc=bt_le_scan_stop();if(!rc)scanning=false;else{slot_scan_failed=true;check(false,"slot scan owner drain");}}
    if(slot_scan_failed){if(!slot_state.restoring)rc_slots_rollback(&slot_state,now.tick/16);slot_scan_failed=false;}
    if(slot_state.moving){
     if(stop()){sent_count=0;++train;++restarts;phase_admitted=false;restart_at=0;
      if(!set_local_slot(slot_state.target)){rc_slots_rollback(&slot_state,now.tick/16);(void)set_local_slot(slot_state.old_slot);slot_selected=true;continue;}
      slot_move_pending=true;slot_move_until=now.tick+32000000;slot_hint_update();}
    }else {slot_selected=slot_selected||slot_state.available;slot_state.rescan=false;}
   }
   if(slot_move_pending&&downstream_ready(now.tick)){rc_slots_commit(&slot_state,now.tick/16);slot_move_pending=false;slot_selected=true;slot_hint_update();}
   if(slot_move_pending&&now.tick>=slot_move_until){
    if(stop()){rc_slots_rollback(&slot_state,now.tick/16);check(set_local_slot(slot_state.old_slot),"restore old slot");
     sent_count=0;++train;++restarts;phase_admitted=false;restart_at=0;slot_move_pending=false;slot_selected=true;slot_hint_update();}
   }
  }
#endif
  struct rc5_stamp st;bool ready_clock=node&&rc5_clock_capture(&local_clock,&timing,now.tick,&st);
#if defined(R2_RC_FIELD_TEST)
  /* Fixture-only controller disturbance, executed by the normal PA owner.
   * Return a physical-counter pause; no timestamp or source is synthesized. */
  extern uint64_t r2_rc_field_test_pause(unsigned,uint64_t);
  uint64_t pause=running&&ready_clock?r2_rc_field_test_pause(node,now.tick):0;
  if(pause&&stop()){sent_count=0;++train;restart_at=requested;
   while(restart_at<=now.tick)restart_at+=1000000;
   restart_at+=pause;}
#endif
  if(node&&routes.node[node].transmitter&&!running&&!lt_conn&&ready_clock&&last_rx&&lt_live(now.tick)
#if defined(R2_RC_AUTO)
    &&(slot_selected||slot_move_pending)&&!slot_state.scanning
#endif
   ){
   if(!pa)create();
   /* First request uses slot target with no assumed device start latency.
    * Subsequent placement uses the actual captured request-to-TX duration. */
   if(!restart_at){restart_at=last_rx+(uint64_t)parent_slot_delta_ns*2/125;while(restart_at<now.tick+32000)restart_at+=1000000;}
   /* Encoding and synchronous HCI publication are preparation, not part of
    * the calibrated start request. Finish them before waiting for its tick. */
   if(!start_prepared){publish();start_prepared=true;}
   lt_acquiring=true;lt_quiesce(true);
#if defined(R2_RC_FIELD_TEST)
   extern uint32_t r2_rc_field_test_work(void);
   uint32_t work_us=r2_rc_field_test_work();if(work_us)k_busy_wait(work_us);
#endif
   /* Publication and quiescence can block. Select the next phase only after
    * they finish, then wait once instead of polling a 200 us aperture. */
   struct r2_capture due;if(!r2_capture_now(&due))continue;
   uint32_t wait_us;if(!rc5_field_start_wait(&restart_at,due.tick,&wait_us)){check(false,"start tick overflow");continue;}
   if(wait_us)k_busy_wait(wait_us);
   if(atomic_get(&stop_requested)||atomic_get(&fault)||(deadline&&mono_us()>=deadline))continue;
   start();restart_at=0;start_prepared=false;
  }
  r2_shadow_poll();
  if(r2_capture_now(&now)){holdover_poll(now.tick);if(running&&holdover_expired&&published.count){publish();(void)r2_capture_now(&now);}snapshot_publish(now);}k_sleep(K_USEC(1000));
 }
 r2_service_stop();
 bool closed=stop();
 if(pa){int rc=bt_le_ext_adv_delete(pa);if(!rc)pa=NULL;else closed=false;}
 if(sync){int rc=bt_le_per_adv_sync_delete(sync);if(!rc)sync=NULL;else closed=false;}
 if(scanning){int rc=bt_le_scan_stop();if(!rc)scanning=false;else closed=false;}
 if(!lt_close())closed=false;
 k_sleep(K_MSEC(75));
 struct r2_radio_capture_stats capture;r2_radio_capture_stats(&capture);if(capture.armed)closed=false;
 check(closed,"native owners drained");
 k_mutex_lock(&state_lock,K_FOREVER);status.error=atomic_get(&fault);status.active=!closed;status.serving=status.follower=status.follower_clock_valid=false;status.closed=closed;status.closed_at=closed?mono_us():0;k_mutex_unlock(&state_lock);
}
int r2_service_configure(const struct r2_identity *id,const struct r2_enrollment *e){
 if(!id||id->result||!id->boot||!e||!e->boundary_field||!r2_group_valid(&e->group))return -EINVAL;
 identity=*id;group=e->group;local_boot=id->boot;
 /* Configured test cohort defines wire identities, not discovered addresses. */
 if(!rc5_field_routes(e->boundary_profile,&routes))return -EINVAL;
#if defined(R2_RC_AUTO)
 if(e->auto_routes.count)routes=e->auto_routes;
#endif
 direct_phase_bias=rc_direct_pps_correction(e->boundary_profile);
 uint64_t silicon=strtoull(id->silicon,NULL,16);for(node=0;node<routes.count;node++)if(routes.node[node].id==silicon)break;if(!node||node==routes.count)return -EACCES;
#if defined(R2_RC_AUTO)
 if(!rc5_routes_valid(&routes))return -EACCES;
 rc_slots_init(&slot_state,routes.node[node].slot,routes.node[routes.node[node].parent].slot);
 for(unsigned i=node+1;i<routes.count;i++)if(routes.node[i].transmitter&&routes.node[i].parent==node)slot_state.max_slot=2;
#endif
 rc5_init(&timing,&routes,node);local_clock.hold_ticks=RC_FIELD_HOLD_TICKS;slot_target_ns=routes.node[node].transmitter?routes.node[node].slot*15625000:0;
 uint8_t derived[16];r2_group_pair_key(derived,&group,silicon,silicon);bt_addr_le_t own={.type=BT_ADDR_LE_RANDOM};memcpy(own.a.val,derived,6);own.a.val[5]|=0xc0;
#if !defined(R2_SWITCHABLE_FIELD)
 int rc=bt_id_create(&own,NULL);if(rc<0)return rc;
#else
 bt_addr_le_t ids[CONFIG_BT_ID_MAX];size_t count=CONFIG_BT_ID_MAX;bt_id_get(ids,&count);bool bound=false;
 for(size_t i=0;i<count;i++)if(!bt_addr_le_cmp(&ids[i],&own))bound=true;
 if(!bound)return -EACCES;
#endif
 status.configured=status.closed=status.normal=status.field_profile=true;status.self=node;status.root=0;status.coordinator=status.standby=255;status.gm_capable=!node;status.profile_rc=true;status.group_id=group.id;status.key_epoch=group.epoch;
 for(unsigned i=0;i<routes.count&&i<4;i++)status.identities[i]=routes.node[i].id;
 snprintf(status.name,sizeof status.name,"%s",id->unit);return 0;
}
void r2_service_clock(const struct r2_clock *clock){r2_service_supervise();struct r2_source_clock source;r2_time_source_snapshot(&source);k_mutex_lock(&state_lock,K_FOREVER);clock_input=*clock;source_input=source;k_mutex_unlock(&state_lock);}
int r2_service_preflight(uint32_t seconds,uint64_t arrival){(void)seconds;(void)arrival;return status.configured&&!atomic_get(&started)?0:-EACCES;}
static int service_launch(uint64_t arrival,uint64_t deadline,bool normal){
 int rc=r2_service_preflight(1,arrival);if(rc)return rc;
 atomic_set(&started,1);k_mutex_lock(&state_lock,K_FOREVER);
 status.active=true;status.closed=false;status.normal=normal;status.deadline=deadline;
 k_mutex_unlock(&state_lock);
 k_thread_create(&owner_thread,owner_stack,K_THREAD_STACK_SIZEOF(owner_stack),owner,NULL,NULL,NULL,8,0,K_NO_WAIT);
 k_thread_name_set(&owner_thread,"r2_boundary");return 0;
}
int r2_service_run(uint64_t arrival){return service_launch(arrival,0,true);}
int r2_service_start(uint32_t seconds,uint64_t arrival){
 if(!seconds){r2_service_stop();return 0;}
 uint64_t deadline;if(!rc5_field_deadline(arrival,seconds,mono_us(),&deadline))return -ETIMEDOUT;
 return service_launch(arrival,deadline,false);
}
void r2_service_stop(void){if(!atomic_get(&started))return;k_mutex_lock(&state_lock,K_FOREVER);if(!status.closing){status.closing=true;status.close_deadline=mono_us()+2000000;}k_mutex_unlock(&state_lock);atomic_set(&stop_requested,1);}
void r2_service_supervise(void){if(status.closing&&!status.closed&&mono_us()>=status.close_deadline)atomic_cas(&fault,0,-ETIMEDOUT);}
int r2_service_close_result(void){struct r2_service_status s;r2_service_snapshot(&s);
 /* The lifetime fault stays visible. Successful proved owner closure is a
  * separate result, so a contained fault can use supported maintenance. */
 #if defined(R2_SWITCHABLE_FIELD)
 if(s.closed&&!s.active&&atomic_get(&started)&&k_thread_join(&owner_thread,K_NO_WAIT))return -EAGAIN;
#endif
 return s.closed&&!s.active?0:s.error?s.error:-EAGAIN;
}
void r2_service_snapshot(struct r2_service_status *out){k_mutex_lock(&state_lock,K_FOREVER);*out=status;k_mutex_unlock(&state_lock);if(atomic_get(&fault))out->error=atomic_get(&fault);}
void r2_service_ui(struct r2_ui_model *m){struct r2_service_status s;r2_service_snapshot(&s);m->profile_supported=true;m->profile_rc=true;m->profile_switching=false;m->ble_active=s.active&&!s.closing;m->root=0;m->coordinator=m->standby=255;m->precision=s.precision;m->rx=s.rx;m->authenticated=s.authenticated;m->gm_capable=!node;m->source_eligible=s.source_eligible;m->relay_selected=parent_known;m->time_relay_hops=parent_known?routes.node[parent_index].depth:0;m->relay_upstream=s.relay.parent;m->boundary_clock=true;m->rc_source_held=s.relay.held_time;m->rc_update_age_ms=s.relay.update_age_ms;m->rc_source_age_ms=s.relay.source_age_ms;
 bool live=s.active&&!s.error&&!s.closing;m->ble_active=live;m->sync_attempt=live&&!s.follower_clock_valid;m->scope_active=live&&(s.serving||s.follower);m->role=m->scope_active?(s.serving?R2_UI_GM:R2_UI_FOLLOWER):R2_UI_NO_ROLE;m->authenticated_gm=live&&s.follower;m->gm_source_fresh=live&&s.serving&&m->clock.source_available;
 r2_ui_identity_name(m->root_name,routes.node[0].id,NULL);r2_ui_identity_name(m->upstream_name,node&&s.relay.selected?routes.node[s.relay.parent].id:0,NULL);
 r2_time_precision_snapshot(&m->pps_precision);
 if(node){/* Local GNSS remains a witness, never selected fallback. */m->clock.valid=false;m->coarse_valid=false;r2_ui_select_ble_time(m,s.follower_utc,s.time_sampled_us,mono_us(),s.service_until,live&&s.follower_clock_valid);}
}
int r2_service_relay(uint64_t session,uint32_t seconds,uint64_t arrival){(void)session;(void)seconds;(void)arrival;return -ENOTSUP;}
int r2_service_relay_cancel(uint64_t session){(void)session;return -ENOTSUP;}
int r2_service_name(const char *name){if(!name||strlen(name)>=sizeof status.name)return -EINVAL;k_mutex_lock(&state_lock,K_FOREVER);snprintf(status.name,sizeof status.name,"%s",name);k_mutex_unlock(&state_lock);return 0;}
uint32_t r2_service_stack_unused(void){size_t unused;if(atomic_get(&started)&&!k_thread_stack_space_get(&owner_thread,&unused))return unused;return UINT32_MAX;}
uint32_t r2_radio_service_stack_unused(void){return r2_service_stack_unused();}

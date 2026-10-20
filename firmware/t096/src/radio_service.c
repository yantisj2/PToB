/* SPDX-License-Identifier: Apache-2.0
 * Public periodic advertising/sync lifecycle, serialized with connected BLE.
 * Callbacks publish lifecycle facts only. Exact captured PDU bytes supply time.
 */
#include "radio_service.h"
#include "role_announcement.h"
#include "health.h"
#include "group_precision.h"
#include "group_admission.h"
#include "ble_service.h"
#include "ble_transport.h"
#include "owner_budget.h"
#include "capture.h"
#include "radio_capture.h"
#include "radio_audit.h"
#include "rc_shadow.h"
#include "core.h"
#ifdef R2_LORA
#include "lora_source.h"
#include "lora_service.h"
#endif
#include <errno.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/sys/atomic.h>
#define R2_LOCK_UNIT 9u
#include "health_lock.h"
static struct r2_radio_precision precision;
static uint64_t shadow_delivery,shadow_epoch;
static struct r2_relay_origin generic_origin;
static uint64_t generic_frame;
static uint8_t generic_proof[32];
static struct r2_enrollment enrollment;
static unsigned own,service_self;
static struct bt_le_ext_adv *advertiser;
static struct r2_role_record role_status;
static struct r2_group_config role_group;
static uint64_t role_next_us;
static atomic_t role_sequence,role_update_errors;
static atomic_t gm_radio_state;
void r2_radio_service_role(const struct r2_role_record *r,const struct r2_group_config *g){
 /* Same serialized CONTROL owner as radio_service_tick; no ISR/callback work. */
 role_status=r?*r:(struct r2_role_record){0};
 if(g)role_group=*g;
}
static struct bt_le_per_adv_sync *sync_object;
static struct k_spinlock cb_lock;
static bt_addr_le_t target;
static bool discovering, found, creating, established, cancelling,
    cancel_established, terminated_during_create;
static bool pa_started, ext_started, closing,pausing,public_client;
static void gm_radio_publish(void){atomic_set(&gm_radio_state,(advertiser?1u:0)|(pa_started?2u:0)|(ext_started?4u:0));}
static struct r2_group_client client_clock;
static struct r2_radio_scope client_scope;
static bool client_anchor_valid(uint64_t now){
 /* Coarse replay-age evidence has its own growing uncertainty budget. It is
  * not a five-second source permission or a precision fit deadline. Original
  * signed source expiry is checked separately on every timing operation. */
 return client_clock.source&&now>=client_clock.at&&
        now<r2_group_anchor_deadline(client_clock.at,client_clock.uncertainty_us);
}
static int64_t client_utc_offset,follower_utc_offset;
static uint64_t close_until, retry_at, updated_at, sync_until;
static bool opportunity_seen,opportunity_complete;
static uint64_t refresh_requested_until;
static uint32_t train, publication, last_overflow, last_published_capture;
static atomic_t error;
static struct r2_radio_scope active_scope, native_scope;
struct pt_input {
 struct r2_radio_scope grant;
 struct r2_source_clock source;
 struct r2_enrollment roster;
 struct r2_group_client client;
 int64_t utc; uint64_t at;
 uint32_t source_epoch;bool source_ready, stopped,quality_listening;
};
struct pt_output {
 struct r2_radio_scope scope;
 struct r2_radio_precision_view view;
 struct r2_link_quality_view quality;
 struct r2_group_client context,bootstrap;
 uint64_t utc, sampled, valid_until, source_frame, payload_until,capture_epoch,capture_tick,capture_until;
 uint32_t payload_sequence;
 uint8_t payload[R2_RELAY_AD_BYTES], address[6]; size_t length;
 bool utc_valid, acquisition;
};
/* Thread-only mailbox. Never acquired by ISR or Bluetooth callbacks.
 * Service -> PT is the only nested mutex order; PT never acquires service. */
K_MUTEX_DEFINE(pt_lock);
static struct pt_input pt_input;
static struct pt_output pt_output;
static struct r2_link_quality direct_quality;
static bool require_anchor, refresh_anchor,continuous_rc;
static uint32_t reset_requested,reset_ack,reset_flushes;
static uint32_t installed_payload,pt_cycles,pt_wall_max_us,pt_capture_age_max_us;
static uint64_t native_close_until;
static bool native_owned,native_gm_started;
static struct {
 r2_servo_clock_t servo;
 uint64_t root,boot,counter_epoch,last_available,last_accepted;
 uint32_t epoch,train,remote_capture;
 bool pending,valid;
} handoff;
void r2_radio_service_handoff_request(uint64_t root,uint64_t boot,uint32_t epoch){
 (void)k_mutex_lock(&pt_lock,K_FOREVER);
 if(handoff.root!=root||handoff.boot!=boot||handoff.epoch!=epoch){
  handoff.root=root;handoff.boot=boot;handoff.epoch=epoch;handoff.pending=true;handoff.valid=false;
 }
 (void)k_mutex_unlock(&pt_lock);
}
bool r2_radio_service_handoff_ready(uint64_t root,uint64_t boot,uint32_t epoch){
 (void)k_mutex_lock(&pt_lock,K_FOREVER);
 bool ready=!handoff.pending&&handoff.root==root&&handoff.boot==boot&&handoff.epoch==epoch;
 (void)k_mutex_unlock(&pt_lock);return ready;
}
bool r2_radio_service_handoff_take(struct r2_relay_precision *to,uint64_t root,uint64_t boot,uint32_t epoch,uint64_t counter_epoch){
 if(!to||!r2_radio_service_closed())return false;
 (void)k_mutex_lock(&pt_lock,K_FOREVER);
 bool valid=handoff.valid&&!atomic_get(&error)&&!native_owned&&pt_input.stopped&&handoff.root==root&&handoff.boot==boot&&handoff.epoch==epoch&&handoff.counter_epoch==counter_epoch&&to->self==to->relay;
 if(valid){
  /* Reindex only the same authenticated GM into the RC's local route. All
   * old radio originals/corrections remain absent. Output requires a NEW
   * authenticated origin and captured correction in the new owner. */
  to->upstream.servo=handoff.servo;to->upstream.servo.output.valid=false;to->upstream.servo.last_sequence=0;
  to->upstream.scope=(struct r2_radio_scope){.boot=boot,.source_epoch=epoch,.publication_epoch=handoff.train,.root=to->root,.follower=true};
  to->upstream.local_epoch=counter_epoch;to->upstream.train=handoff.train;to->upstream.remote_capture=handoff.remote_capture;
  to->upstream.last_available=handoff.last_available;to->upstream.last_accepted=handoff.last_accepted;
  to->root_boot=boot;to->source_epoch=epoch;
  to->remote=(struct r2_relay_route){.boot=boot,.train=handoff.train,.capture_epoch=handoff.remote_capture,.map_generation=handoff.train,.transmitter=to->root,.parent=to->root};
 }
 handoff.valid=false;(void)k_mutex_unlock(&pt_lock);return valid;
}
static struct r2_radio_scope native_grant;
K_THREAD_STACK_DEFINE(pt_stack,8192);
static struct k_thread pt_thread;
static void precision_owner(void *,void *,void *);
static uint64_t service_now(void){return k_ticks_to_us_floor64(k_uptime_ticks());}
static void output_read(struct pt_output *out,uint64_t now){
 (void)k_mutex_lock(&pt_lock,K_FOREVER);*out=pt_output;bool stopped=pt_input.stopped;bool source_ready=pt_input.source_ready;uint32_t epoch=pt_input.source_epoch;(void)k_mutex_unlock(&pt_lock);
 /* The snapshot can advance while a caller waits/preempts. Validate it at
  * a fresh post-copy time, never the caller's earlier observation time. */
 now=service_now();
 /* PT goes idle when its coarse anchor expires. Bound the copied acquisition
  * request too, so that stale output cannot keep PA discovery ahead of the
  * fresh authenticated JOIN needed to recover. This grants no time authority. */
 out->acquisition=out->acquisition&&!stopped&&now>=out->bootstrap.at&&
   now<r2_group_anchor_deadline(out->bootstrap.at,out->bootstrap.uncertainty_us);
 if(out->scope.gm&&(!source_ready||epoch!=out->scope.source_epoch)){out->scope.gm=false;out->view.authorized=false;}
 if(stopped||now<out->sampled||now>=out->scope.until_us){out->scope.gm=false;if(stopped||!out->acquisition)out->scope.follower=false;out->view.authorized=false;}
 struct r2_capture capture;
 bool capture_valid=r2_capture_now(&capture)&&capture.epoch==out->capture_epoch&&capture.tick>=out->capture_tick&&capture.tick<out->capture_until;
 if(!capture_valid||stopped||now<out->sampled||now>=out->valid_until){out->view.map_valid=out->view.locked=false;out->utc_valid=out->context.source=false;}
}
void r2_radio_service_source(const struct r2_source_clock *source,bool ready,uint32_t epoch){
 (void)k_mutex_lock(&pt_lock,K_FOREVER);
#ifdef R2_LORA
 bool withdraw_own=!ready&&pt_output.scope.gm;
#endif
 pt_input.source=*source;pt_input.source_ready=ready;pt_input.source_epoch=epoch;(void)k_mutex_unlock(&pt_lock);
#ifdef R2_LORA
 /* A follower's local PPS is a witness only, never its export permission. */
 if(withdraw_own)r2_lora_source_withdraw();
#endif
}
void r2_radio_service_activate(void){(void)k_mutex_lock(&pt_lock,K_FOREVER);pt_input.stopped=false;handoff.root=0;handoff.pending=handoff.valid=false;(void)k_mutex_unlock(&pt_lock);}
void r2_radio_service_withdraw(void){
#ifdef R2_LORA
 r2_lora_source_withdraw();
#endif
 (void)k_mutex_lock(&pt_lock,K_FOREVER);pt_input.stopped=true;(void)k_mutex_unlock(&pt_lock);
}
static void fault(int rc) {
  if(rc)atomic_cas(&error,0,rc);
}
static bool same(struct r2_radio_scope a, struct r2_radio_scope b) {
  return ((a.root_identity&&b.root_identity)?a.root_identity==b.root_identity:a.root==b.root) && a.boot == b.boot &&
         a.source_epoch == b.source_epoch && a.publication_epoch == b.publication_epoch && a.gm == b.gm &&
         a.follower == b.follower;
}
static void scan_received(const struct bt_le_scan_recv_info *info,
                          struct net_buf_simple *data) {
  k_spinlock_key_t k = k_spin_lock(&cb_lock);
  if (discovering && info->sid == 12 && info->interval == R2_PRECISION_INTERVAL_UNITS &&
      bt_addr_le_eq(info->addr, &target))
    found = true;
  k_spin_unlock(&cb_lock, k);
}
static bool object_matches(struct bt_le_per_adv_sync *object,
                           const bt_addr_le_t *addr, uint8_t sid) {
  if (sid != 12 || !bt_addr_le_eq(addr, &target) ||
      (sync_object != object && !(creating && !sync_object)))
    return false;
  if (!sync_object)
    sync_object = object;
  return true;
}
static void synced(struct bt_le_per_adv_sync *object,
                   struct bt_le_per_adv_sync_synced_info *info) {
  k_spinlock_key_t k = k_spin_lock(&cb_lock);
  if (object_matches(object, info->addr, info->sid))
    established = true;
  k_spin_unlock(&cb_lock, k);
}
static void terminated(struct bt_le_per_adv_sync *object,
                       const struct bt_le_per_adv_sync_term_info *info) {
  k_spinlock_key_t k = k_spin_lock(&cb_lock);
  if (object_matches(object, info->addr, info->sid)) {
    if (creating)
      terminated_during_create = true;
    sync_object = NULL;
    established = creating = cancelling = false;
    found = false;sync_until=0;
  }
  k_spin_unlock(&cb_lock, k);
}
static struct bt_le_scan_cb scan_cb = {.recv = scan_received};
static struct bt_le_per_adv_sync_cb sync_cb = {.synced = synced,
                                               .term = terminated};
int r2_radio_service_init(unsigned self, const struct r2_enrollment *r) {
  if (!r || self >= r->count)
    return -EINVAL;
  own = service_self = self;
  enrollment = *r;
  pt_input.roster=*r;pt_input.stopped=true;
  bt_le_scan_cb_register(&scan_cb);
  int rc=bt_le_per_adv_sync_cb_register(&sync_cb);
  if(!rc){k_thread_create(&pt_thread,pt_stack,K_THREAD_STACK_SIZEOF(pt_stack),precision_owner,NULL,NULL,NULL,8,0,K_NO_WAIT);k_thread_name_set(&pt_thread,"r2_precision");}
  return rc;
}
void r2_radio_service_roster(const struct r2_enrollment *r){(void)k_mutex_lock(&pt_lock,K_FOREVER);pt_input.roster=*r;(void)k_mutex_unlock(&pt_lock);}
static void begin_close(uint64_t now) {
  r2_ble_precision_draining(true);
  if (!closing) {
    closing = true;
    (void)k_mutex_lock(&pt_lock,K_FOREVER);native_gm_started=false;
    close_until = now + 2000000;
    /* PT may have withdrawn first. Consume that original intent even after
     * its scope recovers; CONTROL cannot renew its resource-drain deadline. */
    if(native_close_until&&native_close_until<close_until)close_until=native_close_until;
    native_close_until=close_until;(void)k_mutex_unlock(&pt_lock);
  }
  k_spinlock_key_t k = k_spin_lock(&cb_lock);
  discovering = false;
  k_spin_unlock(&cb_lock, k);
  r2_ble_precision_discovery(false);

}
static void close_poll(uint64_t now) {
  if (advertiser) {
    int rc = 0;
    if (ext_started) {
      rc = bt_le_ext_adv_stop(advertiser);
      if (!rc)
        ext_started = false;
    }
    if (!rc && pa_started) {
      rc = bt_le_per_adv_stop(advertiser);
      if (!rc)
        pa_started = false;
    }
    if (!rc) {
      rc = bt_le_ext_adv_delete(advertiser);
      if (!rc)
        advertiser = NULL;
    }
    fault(rc);
  }
  k_spinlock_key_t k = k_spin_lock(&cb_lock);
  struct bt_le_per_adv_sync *object = sync_object;
  bool issue = object && (!cancelling || (established && !cancel_established));
  if (issue) {
    cancelling = true;
    cancel_established = established;
  }
  k_spin_unlock(&cb_lock, k);
  if (issue) {
    int rc = bt_le_per_adv_sync_delete(object);
    /* A natural termination may win between the callback snapshot and delete.
     * The public lookup proves release; never treat a still-owned object as
     * closed merely because delete returned an error. */
    if (rc && bt_le_per_adv_sync_lookup_addr(&target, 12) == object)
      fault(rc);
  }
  if (object && !bt_le_per_adv_sync_lookup_addr(&target, 12)) {
    k = k_spin_lock(&cb_lock);
    if (sync_object == object) {
      sync_object = NULL;
      creating = established = cancelling = false;
    }
    k_spin_unlock(&cb_lock, k);
  }
  struct r2_radio_capture_stats capture;
  r2_radio_capture_stats(&capture);
  k = k_spin_lock(&cb_lock);
  bool pending = sync_object || creating;
  k_spin_unlock(&cb_lock, k);
  r2_ble_sync_acquiring(pending);
  /* HCI closure may sleep across the original deadline. Require a fresh
   * observation of release; synchronous cleanup cannot erase late ownership. */
  uint64_t observed=service_now();if(observed<now)observed=now;
  if(observed>=close_until)fault(-ETIMEDOUT);
  if (!advertiser && !pending && !capture.armed) {
    r2_ble_precision_draining(false);
    closing = false;
    (void)k_mutex_lock(&pt_lock,K_FOREVER);native_close_until=0;native_owned=false;(void)k_mutex_unlock(&pt_lock);
    if(!pausing){native_scope = (struct r2_radio_scope){0};retry_at = now + 250000;}
    pausing=false;sync_until=0;
  }
  gm_radio_publish(); /* Includes direct stop/close paths without another tick. */
}
void r2_radio_service_stop(uint64_t now) {
  r2_radio_service_withdraw();begin_close(now);
  close_poll(now);
}
bool r2_radio_service_closed(void) {
  k_spinlock_key_t k = k_spin_lock(&cb_lock);
  bool pending = sync_object || creating;
  k_spin_unlock(&cb_lock, k);
  struct r2_radio_capture_stats capture;
  r2_radio_capture_stats(&capture);
  /* Resource closure is independent of the retained fault. Including error
   * here keeps a drained ENOBUFS owner permanently stuck in shutdown. */
  return !closing && !advertiser && !pending && !capture.armed;
}
int r2_radio_service_error(void) { return atomic_get(&error); }
static uint64_t decoded_frame;
static int generic_decode(struct r2_precision_packet *out,const uint8_t *ad,size_t length){
 struct r2_relay_packet p;int rc=r2_group_relay_decode3(&p,ad,length,&enrollment.group,enrollment.identity);
 if(rc)return rc;
 if(p.route.depth)return -EACCES;
 decoded_frame=p.origin.frame_ns;
 *out=(struct r2_precision_packet){.boot=p.origin.boot,.created_ns=p.origin.created_ns,
  .expires_ns=p.origin.expires_ns,.utc_offset_ns=p.origin.utc_offset_ns,.source_epoch=p.origin.source_epoch,
  .train=p.route.train,.publication=p.route.publication,.capture_epoch=p.route.capture_epoch,
  .root=p.origin.root,.flags=1,.count=p.count};
 for(unsigned i=0;i<p.count;i++)out->correction[i]=(struct r2_precise_correction){
  p.correction[i].publication,p.correction[i].event,p.correction[i].channel,
  p.correction[i].ticks,p.correction[i].generation,p.origin.expires_ns};
 return 0;
}
static int generic_encode(uint8_t ad[190],const struct r2_precision_packet *p){
 /* Same V14 policy as the relay owner: immutable source deadline plus the
  * operator's150ms serving margin; no187.5ms envelope/correction churn. */
 if(p->expires_ns>INT64_MAX-150000000)return -EOVERFLOW;
 uint64_t permit=p->expires_ns+150000000;
 bool restriction=generic_origin.boot==p->boot&&generic_origin.source_epoch==p->source_epoch&&
  generic_origin.frame_ns==generic_frame&&p->created_ns>=generic_origin.created_ns&&
  p->created_ns<generic_origin.expires_ns&&permit<generic_origin.expires_ns;
 if(restriction){
  /* A tighter source/control deadline narrows the existing signed scope;
   * do not discard physical corrections for sub-microsecond projection drift. */
  generic_origin.expires_ns=permit;
  int rc=r2_group_origin_sign3(generic_proof,&generic_origin,&enrollment.group,enrollment.identity);if(rc)return rc;
 }else if(!generic_origin.boot||generic_origin.boot!=p->boot||generic_origin.source_epoch!=p->source_epoch||
    generic_origin.frame_ns!=generic_frame||p->created_ns<generic_origin.created_ns||
    p->created_ns>=generic_origin.expires_ns||permit<generic_origin.expires_ns||
    (generic_origin.expires_ns-p->created_ns<=500000000&&permit>generic_origin.expires_ns)){
  generic_origin=(struct r2_relay_origin){p->root,(p->root+1)%4,p->boot,p->source_epoch,
   p->created_ns,permit,generic_frame,p->utc_offset_ns,0,0};
  int rc=r2_group_origin_sign3(generic_proof,&generic_origin,&enrollment.group,enrollment.identity);if(rc)return rc;
 }
 struct r2_relay_packet q={.origin=generic_origin,.route={.transmitter=p->root,.parent=p->root,
  .boot=p->boot,.train=p->train,.publication=p->publication,.capture_epoch=p->capture_epoch,
  .map_generation=p->train,.bound_ns=R2_RELAY_BOUND_UNKNOWN}};
 for(unsigned i=0;i<p->count;i++){
  const struct r2_precise_correction *c=&p->correction[i];
  if(!generic_frame||c->ticks>UINT64_MAX/125||c->ticks*125/2<generic_origin.created_ns||
     c->ticks*125/2>=generic_origin.expires_ns||c->expires_ns>INT64_MAX-150000000||c->expires_ns+150000000<generic_origin.expires_ns)continue;
  /* Source-clock revisions qualify each captured sample upstream. GR route
   * generation fences this transport train, including count-zero envelopes. */
  q.correction[q.count++]=(struct r2_relay_correction){c->publication,c->event,c->channel,c->ticks,p->train};
 }
 return r2_group_relay_encode3(ad,&q,generic_proof,&enrollment.group,enrollment.identity);
}
static bool pt_public_client;
static void follower_poll(uint64_t now) {
  /* Public clients already budget the first200ms for urgent guest initiation.
   * Do not grab sync acquisition first and block that same transient owner.
   * This is admission ordering inside the existing window, not extra RX duty. */
  if(pt_public_client&&!sync_object&&!creating&&
     r2_ble_admission_reserves_scan(now)){
    discovering=false;r2_ble_sync_acquiring(false);r2_ble_precision_discovery(false);return;
  }
  k_spinlock_key_t snapshot = k_spin_lock(&cb_lock);
  bool timed_out = sync_until && !established && now >= sync_until;
  k_spin_unlock(&cb_lock, snapshot);
  if (timed_out) {
    begin_close(now);
    close_poll(now);
    return;
  }
  k_spinlock_key_t k = k_spin_lock(&cb_lock);
  bool need = !sync_object && !creating;
  discovering = need;
  bool create = need && now>=retry_at && (found||pt_public_client) && !r2_ble_initiating();
  if (create) {
    creating = true;
    if(!sync_until)sync_until = now + 2000000;
    terminated_during_create = false;
    cancel_established = false;
  }
  bool acquiring = creating || (sync_object && !established);
  k_spin_unlock(&cb_lock, k);
  r2_ble_sync_acquiring(acquiring);
  r2_ble_precision_discovery(need||(pt_public_client&&sync_object&&!established));
  if (!create)
    return;
  struct bt_le_per_adv_sync_param param = {
      .sid = 12, .skip = 0, .timeout = 100};
  bt_addr_le_copy(&param.addr, &target);
  struct bt_le_per_adv_sync *object = NULL;
  int rc = bt_le_per_adv_sync_create(&param, &object);
  k = k_spin_lock(&cb_lock);
  if (!rc && !terminated_during_create) {
    if (sync_object && sync_object != object)
      fault(-EFAULT);
    else
      sync_object = object;
  }
  creating = false;
  k_spin_unlock(&cb_lock, k);
  /* The pinned host maps HCI Command Disallowed to EACCES and cleans up
   * failed create objects. Retry that controller contention only when no
   * object escaped, under the SAME original two-second acquisition bound.
   * This is not an authentication error or permission extension. */
  if(!object&&(rc==-EACCES||rc==-EBUSY||rc==-EAGAIN)){retry_at=now+20000;return;}
  fault(rc);
}

/* PT is the sole owner of capture associations, origin MACs, servo and maps.
 * No Bluetooth calls, admission crypto, GATT completion wait, or formatting. */
static uint64_t follower_frame;
static int prepare_data(const struct r2_source_clock *source,struct r2_capture current,uint64_t now,struct pt_output *out){
 uint64_t ns,generation,expiry;int64_t translated;
 if(!r2_source_utc_read(source,current,active_scope.source_epoch,&translated)||!r2_source_clock_read(source,current,&ns,&generation)||!r2_source_clock_expiry(source,current,&expiry)||ns>INT64_MAX||active_scope.until_us<=now)return -ESTALE;
 uint64_t permit=active_scope.until_us-now;
 if(permit<=1500000&&permit*1000<=UINT64_MAX-ns&&ns+permit*1000<expiry)expiry=ns+permit*1000;
 if(publication==UINT32_MAX)return -EOVERFLOW;
 struct r2_precision_packet p={.boot=active_scope.boot,.source_epoch=active_scope.source_epoch,.root=own,.train=train,.publication=++publication,.capture_epoch=current.epoch,.flags=1,.created_ns=ns,.expires_ns=expiry,.utc_offset_ns=source->utc_offset_ns,.count=precision.sent_count};
 memcpy(p.correction,precision.sent,sizeof p.correction);
 out->length=enrollment.group.id?R2_RELAY_AD_BYTES:R2_PRECISION_AD_BYTES;
 int rc=enrollment.group.id?generic_encode(out->payload,&p):r2_precision_encode(out->payload,&p,enrollment.key[own]);
 if(!rc){out->payload_until=now+(expiry-ns)/1000;out->payload_sequence=publication;updated_at=now;last_published_capture=precision.accepted;}
 return rc;
}
/* Control admission is independent of delivered-clock ownership. Local source
 * qualification may open control discovery, but an empty grant cannot evict a
 * still bounded authenticated BLE anchor. Only the PT owner reads client_clock. */
static bool direct_grant_live(const struct pt_input *in,uint64_t now){
 const struct r2_radio_scope *g=&in->grant;
 return !in->stopped&&g->root<in->roster.count&&g->boot&&g->source_epoch&&
  g->publication_epoch&&now<g->until_us&&
  (g->follower||(g->gm&&in->source_ready&&in->source_epoch==g->source_epoch));
}
static bool retain_public_timing(const struct pt_input *in,uint64_t now){
 return !in->client.passive&&public_client&&client_anchor_valid(now)&&
  !in->stopped&&!direct_grant_live(in,now);
}
static void precision_step(uint64_t now){
 bool request_refresh=false,request_anchor=false;struct pt_input in;struct pt_output out;
 /* PT is the sole output writer: reading its previous value needs no lock.
  * Keep the entire multi-producer input snapshot coherent under its own lock. */
 out=pt_output;
 (void)k_mutex_lock(&pt_lock,K_FOREVER);in=pt_input;(void)k_mutex_unlock(&pt_lock);
 if(retain_public_timing(&in,now)){
  /* Retain the original anchor if admission has no replacement. Never create
   * a new timestamp or extend its source/replay deadlines here. */
  if(!in.client.source)in.client=client_clock;
  in.client.passive=true;
 }
 if(in.grant.root<in.roster.count)in.grant.root_identity=in.roster.identity[in.grant.root];
 if(!public_client)enrollment=in.roster;
 /* A refreshed causal JOIN anchor changes context, never the signed PA map. */
 if(in.client.at!=client_clock.at||in.client.passive!=public_client){
  bool was_public=public_client;public_client=in.client.passive;client_clock=in.client;
  if(!public_client){
   /* Same-source promotion changes roster representation, not the clock. */
   if(was_public&&same(active_scope,in.grant))follower_utc_offset=client_utc_offset;
   enrollment=in.roster;own=service_self;
  }
  if(public_client&&client_clock.source){own=0;enrollment.count=2;memset(enrollment.identity,0,sizeof enrollment.identity);enrollment.identity[0]=in.roster.identity[0];enrollment.identity[1]=client_clock.root;
   uint8_t address[16];r2_group_pair_key(address,&enrollment.group,client_clock.root,client_clock.root);memcpy(enrollment.address[1],address,6);enrollment.address[1][5]|=0xc0;
   client_scope=(struct r2_radio_scope){.boot=client_clock.boot,.until_us=client_clock.until,.source_epoch=client_clock.source_epoch,.publication_epoch=client_clock.publication,.root_identity=client_clock.root,.root=1,.follower=true};
   if(!was_public&&same(active_scope,client_scope))client_utc_offset=follower_utc_offset;
  }else if(public_client)client_scope=(struct r2_radio_scope){0};
 }
 struct r2_radio_scope scope=public_client?client_scope:in.grant;
 int64_t utc=in.utc;
 if(public_client){
  if(client_clock.source&&client_scope.until_us&&now>=client_scope.until_us&&now-client_scope.until_us>=1500000&&refresh_requested_until!=client_scope.until_us){refresh_requested_until=client_scope.until_us;request_refresh=true;}
  if(!client_anchor_valid(now))scope=(struct r2_radio_scope){0};
  utc=client_clock.source?(int64_t)(client_clock.utc_us+now-client_clock.at)*1000:-1;
 }else if(utc>=0&&now>=in.at&&(now-in.at)<(uint64_t)(INT64_MAX-utc)/1000)utc+=(int64_t)(now-in.at)*1000;
 const struct r2_source_clock *source=&in.source;
 struct r2_capture current={0};bool captured=r2_capture_now(&current);
 bool acquisition=public_client&&client_anchor_valid(now)&&scope.follower;out.acquisition=acquisition&&!in.stopped;
 if(in.stopped||!captured||(!acquisition&&scope.until_us<=now)||scope.root>=enrollment.count||(scope.gm&&(!in.source_ready||in.source_epoch!=scope.source_epoch)))scope=(struct r2_radio_scope){0};
 if(!same(scope,active_scope)){
  active_scope=scope;out.length=0;out.payload_until=0;out.payload_sequence=0;
  if(scope.gm){train=scope.publication_epoch;publication=0;generic_frame=0;memset(&generic_origin,0,sizeof generic_origin);last_published_capture=UINT32_MAX;updated_at=0;}
  follower_frame=0;
 }
 active_scope=scope;
 bool shadow_changed=precision.scope.boot!=scope.boot||precision.scope.source_epoch!=scope.source_epoch||precision.scope.root_identity!=scope.root_identity||precision.local_epoch!=(captured?current.epoch:0)||precision.scope.follower!=scope.follower||precision.scope.gm!=scope.gm;
 if(shadow_changed){r2_shadow_normal_withdraw();shadow_delivery=shadow_epoch=0;}
 r2_radio_precision_scope(&precision,scope,captured?current.epoch:0);
 struct r2_radio_capture_stats stats;r2_radio_capture_stats(&stats);
 if(stats.overflow!=last_overflow){last_overflow=stats.overflow;(void)k_mutex_lock(&pt_lock,K_FOREVER);pt_input.stopped=true;(void)k_mutex_unlock(&pt_lock);fault(-ENOBUFS);r2_shadow_normal_withdraw();shadow_delivery=shadow_epoch=0;scope=(struct r2_radio_scope){0};r2_radio_precision_scope(&precision,scope,current.epoch);}
 r2_link_quality_bind(&direct_quality,scope.root_identity,scope.boot,captured?current.epoch:0);
 r2_link_quality_tick(&direct_quality,scope.follower&&!in.stopped&&in.quality_listening,now);
 if(scope.gm||scope.follower){
  struct r2_radio_record record;
  for (unsigned i = 0; i < R2_OWNER_CAPTURE_RX && r2_radio_capture_pop(&record); i++) {
    struct r2_capture physical;
    struct r2_precision_packet p;
    if (!r2_capture_radio(record.raw, record.epoch, &physical))
      continue;
    /* Own TX uses the own recipient tag only to validate the exact queued PDU;
     * receiver validates its distinct root-to-self tag before association. */
    if (record.length != (enrollment.group.id?R2_RELAY_AD_BYTES:R2_PRECISION_AD_BYTES) ||
        (enrollment.group.id?generic_decode(&p,record.ad,record.length):r2_precision_decode(&p, record.ad, record.length, own,
                            enrollment.key[scope.root][own])))
      continue;
    struct r2_capture processed;
    if(r2_capture_now(&processed)&&processed.epoch==physical.epoch&&processed.tick>=physical.tick){
     uint64_t age=(processed.tick-physical.tick)/(R2_CAPTURE_HZ/1000000);if(age>pt_capture_age_max_us)pt_capture_age_max_us=age>UINT32_MAX?UINT32_MAX:(uint32_t)age;
    }
    if (record.tx && scope.gm) {
      uint64_t ns, generation, expiry;
      if (r2_source_clock_read(source, physical, &ns, &generation) &&
          r2_source_clock_expiry(source, physical, &expiry)){
        if(enrollment.group.id&&!generic_frame)generic_frame=ns;
        bool saved=r2_radio_precision_tx(&precision, &p, physical, record.event,
                                    record.channel, ns, generation, expiry,
                                    now);
        if(saved)r2_ble_timing_lamps(true,p.count!=0,now);
        if(saved&&r2_radio_audit_selected(record.event)){
          struct r2_radio_audit_record q={.kind=R2_AUDIT_TX,.peer_boot=p.boot,.root_boot=p.boot,
            .epoch=record.epoch,.peer_epoch=p.capture_epoch,.root_epoch=p.source_epoch,.train=p.train,
            .publication=p.publication,.event=record.event,.channel=record.channel,
            .address_raw=record.address_raw,.raw=record.raw,.tick=physical.tick,.ns=ns,
            .wire_ticks=precision.sent[0].ticks,.generation=generation,
            .crc=r2_crc32(record.ad,record.length)};
          struct r2_capture audit_now;if(r2_capture_now(&audit_now)&&audit_now.epoch==physical.epoch)q.available=audit_now.tick;
          r2_radio_audit_put(&q);
        }
      }
    } else if (!record.tx && scope.follower) {
#if defined(R2_AUTO_ROUTE_FIXTURE)
      /* Model timing loss independently of still-audible G1/JOIN. This hook
       * is absent from every field image and grants no timing authority. */
      extern bool r2_auto_route_fixture_rx(uint16_t event,uint64_t now,bool direct);
      if(!r2_auto_route_fixture_rx(record.event,now,true))continue;
#endif
      if(public_client&&p.root==scope.root&&p.created_ns<=INT64_MAX&&p.expires_ns<=INT64_MAX&&p.utc_offset_ns>=0&&p.utc_offset_ns<=INT64_MAX-(int64_t)p.expires_ns){
        int64_t created=(int64_t)p.created_ns+p.utc_offset_ns,expires=(int64_t)p.expires_ns+p.utc_offset_ns;
        uint64_t guard=client_clock.uncertainty_us+(now-client_clock.at)/500+100;
        if(utc>=created&&expires>utc+(int64_t)guard*1000){
          if(p.boot!=scope.boot||p.source_epoch!=scope.source_epoch||p.train!=scope.publication_epoch){
            /* MAC+currently fresh envelope may request a new causal binding;
             * it cannot silently install a new source scope. */
            request_anchor=true;client_clock.source=false;
            client_scope=(struct r2_radio_scope){0};scope=client_scope;r2_shadow_normal_withdraw();shadow_delivery=shadow_epoch=0;
            r2_radio_precision_scope(&precision,scope,current.epoch);continue;
          }
          uint64_t remaining=(uint64_t)(expires-utc)/1000-guard;
          if(remaining<=(enrollment.group.id?1650000:1500000)){client_utc_offset=p.utc_offset_ns;client_scope.until_us=scope.until_us=now+remaining;active_scope.until_us=scope.until_us;r2_radio_precision_scope(&precision,scope,current.epoch);}
        }
      }
      /* Count authenticated current-parent packets before fitting. Failed
       * association/source/map evidence cannot become clock authority here. */
      if(p.root==scope.root&&p.boot==scope.boot&&p.source_epoch==scope.source_epoch&&
         p.train==scope.publication_epoch&&physical.epoch==current.epoch&&
         current.tick>=physical.tick&&current.tick-physical.tick<=2000000)
      {
        r2_link_quality_rx(&direct_quality,p.train,record.event,now);
        r2_ble_timing_lamps(false,p.count!=0,now);
      }
      if (r2_capture_now(&current)) {
        if(enrollment.group.id?r2_radio_precision_rx_serving_origin(&precision,&p,physical,record.event,record.channel,current,now,utc):r2_radio_precision_rx(&precision, &p, physical, record.event,
                                    record.channel, current, now, utc)){follower_utc_offset=p.utc_offset_ns;if(enrollment.group.id)follower_frame=decoded_frame;}
        (void)r2_radio_precision_step(&precision,current,now);
      }
    }
  }

  if(scope.gm&&(precision.accepted!=last_published_capture||now-updated_at>=100000)){
   int rc=prepare_data(&in.source,current,now,&out);if(rc){out.length=0;if(rc!=-ESTALE)fault(rc);}
  }
 }
 uint64_t sampled=now;int64_t relative=0;
 bool paired=r2_capture_now_at(&current,&sampled);
 r2_radio_precision_view(&precision,current,sampled,&out.view);
 out.view.captured=stats.completed;out.view.invalid=stats.invalid;out.view.train=precision.train;out.view.publications=publication;
 out.quality=direct_quality.view;
 out.scope=active_scope;out.sampled=sampled;out.source_frame=follower_frame;out.capture_epoch=current.epoch;out.capture_tick=current.tick;out.capture_until=0;
 out.valid_until=active_scope.until_us;
 if(precision.last_available&&paired&&current.tick>=precision.last_available){
  uint64_t age=current.tick-precision.last_available;
  uint64_t remaining=age<R2_CAPTURE_HZ*3/2?(R2_CAPTURE_HZ*3/2-age)/(R2_CAPTURE_HZ/1000000):0;
  out.capture_until=precision.last_available+R2_CAPTURE_HZ*3/2;
  remaining=remaining>remaining/500+1?remaining-remaining/500-1:0;
  if(sampled+remaining<out.valid_until)out.valid_until=sampled+remaining;
 }else out.valid_until=sampled;
 int64_t offset=public_client?client_utc_offset:follower_utc_offset;
 out.utc_valid=paired&&scope.follower&&r2_radio_precision_read(&precision,current,sampled,&relative)&&relative>=0&&offset>=0&&relative<=INT64_MAX-offset&&sampled<out.valid_until;
 out.utc=out.utc_valid?(uint64_t)(relative+offset)/1000:0;
 out.bootstrap=client_clock;out.bootstrap.passive=public_client;out.bootstrap.until=scope.until_us;
 out.bootstrap.source=public_client&&client_anchor_valid(sampled)&&sampled<scope.until_us;
 out.context=(struct r2_group_client){.passive=public_client,.source=out.utc_valid,.root=scope.root<enrollment.count?enrollment.identity[scope.root]:0,.boot=scope.boot,.source_epoch=scope.source_epoch,.publication=scope.publication_epoch,.utc_us=out.utc,.at=sampled,.until=scope.until_us,.map_until=out.valid_until};
 if(scope.root<enrollment.count)memcpy(out.address,enrollment.address[scope.root],6);
 if(pt_cycles<UINT32_MAX)++pt_cycles;
 out.view.pt_cycles=pt_cycles;out.view.pt_wall_max_us=pt_wall_max_us;out.view.pt_capture_age_max_us=pt_capture_age_max_us;
 struct rc_shadow_map shadow;r2_servo_map_t delivered;
 bool publish_shadow=r2_shadow_enabled()&&out.utc_valid&&scope.root_identity&&out.valid_until>sampled&&
  r2_radio_precision_map(&precision,&delivered)&&(delivered.delivery!=shadow_delivery||current.epoch!=shadow_epoch);
 if(publish_shadow){
  uint64_t remaining=out.valid_until-sampled;
  if(remaining>(UINT64_MAX-current.tick)/16)publish_shadow=false;
  else{
   uint64_t until=current.tick+remaining*16;
   if(precision.last_available<=UINT64_MAX-R2_CAPTURE_HZ*3/2&&until>precision.last_available+R2_CAPTURE_HZ*3/2)until=precision.last_available+R2_CAPTURE_HZ*3/2;
   shadow=(struct rc_shadow_map){.normal=true,.normal_map=delivered,.normal_until=until,
    .map={.effective=precision.last_available,.generation=precision.accepted},
    .lo=current.tick,.epoch=current.epoch,.parent=scope.root_identity,.parent_boot=scope.boot,
    .root=scope.root_identity,.root_boot=scope.boot,.root_epoch=scope.source_epoch,
    .parent_epoch=precision.remote_capture,.train=precision.train,.accepted=precision.accepted,.utc_offset=offset};
  }
 }
#ifdef R2_LORA
 /* Immutable exports originate here, where the real source and delivered
  * follower map are owned. Local GNSS is never consulted for a follower. */
 if(r2_lora_source_needed()){
 struct rc8_export lora={0};
 struct rc8_scope lscope={.emitter=in.roster.identity[service_self],.emitter_boot=1,.stream=1,
  .root=scope.root_identity,.root_boot=scope.boot,.parent=scope.follower?scope.root_identity:0,.parent_boot=scope.follower?scope.boot:0,.source_epoch=scope.source_epoch,.scale=RC8_UTC,
  .kind=scope.gm?RC8_OWN_GM:RC8_SELECTED_BLE};
 bool lvalid=false;
 if(paired&&!in.stopped&&sampled<scope.until_us&&scope.until_us-sampled<=(UINT64_MAX-current.tick)/16){
  uint64_t until=current.tick+(scope.until_us-sampled)*16;
  if(scope.gm&&in.source_ready&&in.source_epoch==scope.source_epoch)
   lvalid=r2_lora_source_own(&lora,source,lscope,current,until);
  else if(scope.follower&&out.utc_valid&&out.view.locked&&out.valid_until>sampled&&r2_radio_precision_map(&precision,&delivered)){
   uint64_t limit=current.tick+(out.valid_until-sampled)*16;
   lvalid=r2_lora_source_follower(&lora,&delivered,lscope,current,limit,
    precision.origin_created_ns,precision.origin_expires_ns,offset,precision.accepted);
  }
 }
 if(lvalid)r2_lora_source_publish(&lora);else r2_lora_source_withdraw();
 struct rc8_quality lquality={.known=RC8_Q_ROLE|RC8_Q_HOP|(scope.follower?RC8_Q_LOCK:0),
  .values=(scope.follower&&out.view.locked?RC8_Q_LOCK:0),.role=scope.gm?1:scope.follower?2:0,.hop=scope.follower?1:0};
 if(out.view.correction_age_valid){lquality.known|=RC8_Q_CORRECTION_AGE;lquality.correction_age_ms=out.view.correction_age_ms;}
 if(out.view.drift_valid){lquality.known|=RC8_Q_RATE;lquality.rate_ppb=out.view.drift_ppb;}
 if(out.view.rms_valid){lquality.known|=RC8_Q_RESIDUAL;lquality.residual_ns=out.view.rms_ns;}
 if(lvalid){lquality.known|=RC8_Q_MAP_LIFE;lquality.map_life_ms=(uint32_t)((lora.until_tick-current.tick)/16000);}
 /* A missing lock is reported as measured loss. No parent PPS/GNSS fact is
  * inferred from this device's local receiver or the selected alias. */
 r2_lora_quality_publish(&lscope,&lquality);
 }else r2_lora_source_withdraw();
#endif
 (void)k_mutex_lock(&pt_lock,K_FOREVER);pt_output=out;
 if(native_owned&&!native_close_until&&(!same(native_grant,out.scope)||(!out.scope.gm&&!out.scope.follower)))native_close_until=now+2000000;
 refresh_anchor|=request_refresh;require_anchor|=request_anchor;(void)k_mutex_unlock(&pt_lock);
 /* Actual mailbox publication lies between paired lo and witness-map hi. */
 if(publish_shadow){r2_shadow_normal_map(&shadow);shadow_delivery=delivered.delivery;shadow_epoch=current.epoch;}
 /* Include the witness publication in the next owner-cost snapshot. */
 uint64_t wall=service_now()-now;if(wall>pt_wall_max_us)pt_wall_max_us=wall>UINT32_MAX?UINT32_MAX:(uint32_t)wall;
}
/* No precision fitting/capture processing or bulk snapshots until a live
 * authorized source/follower context exists. Scalar checks remain cheap and
 * one final step withdraws old state when eligibility disappears. */
static bool precision_needed(uint64_t now){
 (void)k_mutex_lock(&pt_lock,K_FOREVER);
 bool needed=!pt_input.stopped&&(retain_public_timing(&pt_input,now)||
  (pt_input.client.passive?(pt_input.client.source&&now>=pt_input.client.at&&
   now<r2_group_anchor_deadline(pt_input.client.at,pt_input.client.uncertainty_us)):
   direct_grant_live(&pt_input,now)));
 (void)k_mutex_unlock(&pt_lock);return needed;
}
static uint32_t idle_capture_discards;
uint32_t r2_radio_service_idle_discards(void){return __atomic_load_n(&idle_capture_discards,__ATOMIC_RELAXED);}
static void idle_capture_drain(void){
 /* An expired anchor stops discipline, not the asynchronous controller
  * producer. Drain only OUR still-owned receiver while it closes. Never
  * consume records after ownership has passed to the relay service. */
 (void)k_mutex_lock(&pt_lock,K_FOREVER);
 if(native_owned){
  /* Keep ownership through the bounded pop. CONTROL cannot release it and
   * start a different producer between the ownership check and queue read. */
  struct r2_radio_record record;
  for(unsigned i=0;i<R2_OWNER_CAPTURE_RX&&r2_radio_capture_pop(&record);i++)
   if(idle_capture_discards<UINT32_MAX)__atomic_store_n(&idle_capture_discards,idle_capture_discards+1,__ATOMIC_RELAXED);
 }
 (void)k_mutex_unlock(&pt_lock);
}
static void precision_poll(uint64_t now,bool *was_running){
 uint64_t handoff_root=0,handoff_boot=0;uint32_t handoff_epoch=0;
 (void)k_mutex_lock(&pt_lock,K_FOREVER);
 if(handoff.pending&&pt_input.stopped){
  /* PT is the sole history writer. Snapshot before the final withdrawal
   * clears its old scope; copying is thread-only and under the mailbox lock. */
  handoff_root=handoff.root;handoff_boot=handoff.boot;handoff_epoch=handoff.epoch;
  handoff.valid=precision.scope.follower&&!precision.scope.gm&&precision.scope.root_identity==handoff.root&&precision.scope.boot==handoff.boot&&precision.scope.source_epoch==handoff.epoch&&precision.train&&precision.remote_capture&&!precision.servo.fault&&precision.servo.fit_valid&&precision.servo.session==precision.train;
  if(handoff.valid){handoff.servo=precision.servo;handoff.counter_epoch=precision.local_epoch;handoff.train=precision.train;handoff.remote_capture=precision.remote_capture;handoff.last_available=precision.last_available;handoff.last_accepted=precision.last_accepted;}
 }
 if(reset_requested!=reset_ack&&pt_input.stopped&&!native_owned){
  /* Only the original PT owner clears estimator state, between jobs. */
  int flushed=r2_radio_capture_flush();
  if(flushed<0){atomic_cas(&error,0,flushed);(void)k_mutex_unlock(&pt_lock);return;}
  reset_flushes=(uint32_t)flushed>UINT32_MAX-reset_flushes?UINT32_MAX:reset_flushes+(uint32_t)flushed;
  memset(&precision,0,sizeof precision);memset(&pt_output,0,sizeof pt_output);memset(&direct_quality,0,sizeof direct_quality);
  client_clock=(struct r2_group_client){0};client_scope=active_scope=(struct r2_radio_scope){0};
  public_client=false;client_utc_offset=follower_utc_offset=0;shadow_delivery=shadow_epoch=0;
  generic_frame=0;memset(&generic_origin,0,sizeof generic_origin);last_published_capture=UINT32_MAX;updated_at=0;
  require_anchor=refresh_anchor=false;reset_ack=reset_requested;*was_running=false;
  handoff.root=0;handoff.pending=handoff.valid=false;
 }
 (void)k_mutex_unlock(&pt_lock);
 bool needed=precision_needed(now);
 if(needed||*was_running)precision_step(now);
 if(!needed)idle_capture_drain();
 *was_running=needed;
 if(handoff_root){
  /* Publish ACK only after the final old-owner job/queue drain, not before
   * it. This also fences leaf handovers which deliberately import no fit. */
  (void)k_mutex_lock(&pt_lock,K_FOREVER);
  if(pt_input.stopped&&handoff.root==handoff_root&&handoff.boot==handoff_boot&&handoff.epoch==handoff_epoch)handoff.pending=false;
  (void)k_mutex_unlock(&pt_lock);
 }
}
static void precision_owner(void *a,void *b,void *c){
 bool was_running=false;
 for(;;){precision_poll(service_now(),&was_running);k_sleep(K_MSEC(2));}
}
static bool native_live(const struct pt_output *out,uint64_t now){
 struct pt_output current;output_read(&current,now);
 return same(out->scope,current.scope)&&current.scope.gm&&out->payload_until>now&&current.scope.until_us>now;
}
static int install_data(const struct pt_output *out){
 /* This CONTROL-owned stack copy remains immutable through synchronous HCI.
  * PT may replace its mailbox while the controller call retains these bytes. */
 struct bt_data data={.type=BT_DATA_MANUFACTURER_DATA,.data_len=out->length-2,.data=out->payload+2};
 r2_health_operation(R2_HEALTH_PA,R2_HEALTH_ADV_DATA,-EINPROGRESS);
 int rc=bt_le_per_adv_set_data(advertiser,&data,1);
 r2_health_operation(R2_HEALTH_PA,R2_HEALTH_ADV_DATA,rc);
 if(!rc&&!native_live(out,service_now()))return -ESTALE;
 if(!rc)installed_payload=out->payload_sequence;
 return rc;
}
static void install_role_status(const struct pt_output *out,uint64_t now){
 if(now<role_next_us||!advertiser||!native_live(out,now))return;
 role_next_us=now+1000000; // One bounded owner update/s, no extra advertising set.
 struct r2_role_record r=role_status;
 if(!r.sender||r.root!=out->scope.root_identity||r.root_boot!=out->scope.boot||r.source_epoch!=out->scope.source_epoch||
    now<r.created_us||now-r.created_us>=r.valid_us||(uint32_t)atomic_get(&role_sequence)==UINT32_MAX)return;
 r.valid_us-=(uint32_t)(now-r.created_us);r.created_us=now;
 atomic_inc(&role_sequence);r.sequence=(uint32_t)atomic_get(&role_sequence);
 uint8_t wire[R2_ROLE_AD_BYTES];if(r2_role_encode(wire,&r,&role_group))return;
 const struct bt_data ad={.type=BT_DATA_MANUFACTURER_DATA,.data_len=sizeof wire-2,.data=wire+2};
 r2_health_operation(R2_HEALTH_PA,R2_HEALTH_ADV_DATA,-EINPROGRESS);
 int rc=bt_le_ext_adv_set_data(advertiser,&ad,1,NULL,0);
 r2_health_operation(R2_HEALTH_PA,R2_HEALTH_ADV_DATA,rc);
 // Optional diagnostic metadata failure does not withdraw healthy timing.
 // Its old signed sequence is rejected on subsequent SS3 polls; keep a count.
 if(rc&&(uint32_t)atomic_get(&role_update_errors)!=UINT32_MAX)atomic_inc(&role_update_errors);
}
void r2_radio_service_gm_diagnostics(uint32_t out[6]){
 out[0]=(uint32_t)atomic_get(&role_sequence);out[1]=(uint32_t)atomic_get(&role_update_errors);
 uint32_t state=(uint32_t)atomic_get(&gm_radio_state);
 out[2]=!!(state&1);out[3]=!!(state&2);out[4]=!!(state&4);out[5]=(uint32_t)atomic_get(&error);
}
static int start_gm(const struct pt_output *out){
 if(!out->length||!native_live(out,service_now()))return -ESTALE;
 const struct bt_le_adv_param param={.id=0,.sid=12,.options=BT_LE_ADV_OPT_NO_2M|BT_LE_ADV_OPT_EXT_ADV|BT_LE_ADV_OPT_USE_IDENTITY,.interval_min=50,.interval_max=50};
 r2_health_operation(R2_HEALTH_PA,R2_HEALTH_ADV_CREATE,-EINPROGRESS);
 int rc=bt_le_ext_adv_create(&param,NULL,&advertiser);
 r2_health_operation(R2_HEALTH_PA,R2_HEALTH_ADV_CREATE,rc);
 const struct bt_le_per_adv_param periodic={.interval_min=R2_PRECISION_INTERVAL_UNITS,.interval_max=R2_PRECISION_INTERVAL_UNITS};
 if(!rc){r2_health_operation(R2_HEALTH_PA,R2_HEALTH_ADV_PARAM,-EINPROGRESS);rc=bt_le_per_adv_set_param(advertiser,&periodic);r2_health_operation(R2_HEALTH_PA,R2_HEALTH_ADV_PARAM,rc);}
 if(!rc)rc=install_data(out);
 if(!rc)install_role_status(out,service_now());
 if(!rc&&!native_live(out,service_now()))rc=-ESTALE;
 if(!rc){r2_health_operation(R2_HEALTH_PA,R2_HEALTH_PA_START,-EINPROGRESS);rc=bt_le_per_adv_start(advertiser);r2_health_operation(R2_HEALTH_PA,R2_HEALTH_PA_START,rc);if(!rc)pa_started=true;}
 if(!rc&&!native_live(out,service_now()))rc=-ESTALE;
 if(!rc){r2_health_operation(R2_HEALTH_PA,R2_HEALTH_EXT_START,-EINPROGRESS);rc=bt_le_ext_adv_start(advertiser,BT_LE_EXT_ADV_START_DEFAULT);r2_health_operation(R2_HEALTH_PA,R2_HEALTH_EXT_START,rc);if(!rc)ext_started=true;}
 if(!rc&&!native_live(out,service_now()))rc=-ESTALE;
 if(!rc){(void)k_mutex_lock(&pt_lock,K_FOREVER);native_gm_started=true;(void)k_mutex_unlock(&pt_lock);}
 return rc;
}
/* Align direct public clients just as the relay owner does, but only from
 * a live authenticated delivered clock and after old PA resources drain. */
static void direct_window_align(const struct pt_output *out,uint64_t now){
 const struct r2_group_client *c=&out->context;
 if(!c->passive||!c->source||now<c->at||now>=c->map_until||
    c->utc_us>UINT64_MAX-(now-c->at)||!r2_radio_service_closed())return;
 (void)r2_ble_client_align(now,c->utc_us+now-c->at);
}
static void tick_owned(struct r2_radio_scope scope,const struct r2_source_clock *source,int64_t utc,uint64_t now){
 (void)source; /* Main supplies source revisions independently of this owner. */
 (void)k_mutex_lock(&pt_lock,K_FOREVER);pt_input.grant=scope;pt_input.utc=utc;pt_input.at=now;
 pt_input.quality_listening=false;
 bool refresh=refresh_anchor,require=require_anchor;refresh_anchor=require_anchor=false;(void)k_mutex_unlock(&pt_lock);
 if(require)r2_group_admission_require_anchor();else if(refresh)r2_group_admission_refresh_anchor();
 struct pt_output out;output_read(&out,now);scope=out.scope;pt_public_client=out.context.passive;
 direct_window_align(&out,service_now());
 if(error)scope=(struct r2_radio_scope){0};
 (void)k_mutex_lock(&pt_lock,K_FOREVER);bool retire=native_close_until!=0;(void)k_mutex_unlock(&pt_lock);
 if(retire&&!closing)begin_close(now);
 if(closing){close_poll(now);return;}
 if(pt_public_client&&!continuous_rc&&!r2_ble_client_window_open(now,400000)){
  if(opportunity_seen){opportunity_complete=true;opportunity_seen=false;}
  k_spinlock_key_t k=k_spin_lock(&cb_lock);bool pending=sync_object||creating;discovering=false;k_spin_unlock(&cb_lock,k);r2_ble_precision_discovery(false);
  if(pending){pausing=true;begin_close(now);uint64_t quiet_until=r2_ble_client_window_start(now)+500000;if(quiet_until<close_until)close_until=quiet_until;(void)k_mutex_lock(&pt_lock,K_FOREVER);native_close_until=close_until;(void)k_mutex_unlock(&pt_lock);close_poll(now);}return;
 }
 k_spinlock_key_t k=k_spin_lock(&cb_lock);bool pending=sync_object||creating;k_spin_unlock(&cb_lock,k);
 if(!same(scope,native_scope)){
  if(native_scope.gm||native_scope.follower||advertiser||pending){begin_close(now);close_poll(now);return;}
  if(now<retry_at)return;
  native_scope=scope;installed_payload=0;
  (void)k_mutex_lock(&pt_lock,K_FOREVER);native_grant=scope;native_owned=scope.gm||scope.follower;(void)k_mutex_unlock(&pt_lock);
  if(scope.follower){k=k_spin_lock(&cb_lock);target.type=BT_ADDR_LE_RANDOM;memcpy(target.a.val,out.address,6);found=false;k_spin_unlock(&cb_lock,k);}
 }
 native_scope=scope;
 (void)k_mutex_lock(&pt_lock,K_FOREVER);native_grant=scope;native_owned=scope.gm||scope.follower;(void)k_mutex_unlock(&pt_lock);
 if(scope.gm){int rc=!advertiser?start_gm(&out):out.payload_sequence!=installed_payload?install_data(&out):0;
  if(!rc)install_role_status(&out,service_now());
  if(rc){if(rc!=-ESTALE)fault(rc);begin_close(service_now());}
 }else if(scope.follower){follower_poll(now);if(pt_public_client&&(sync_object||creating||discovering))opportunity_seen=true;
  (void)k_mutex_lock(&pt_lock,K_FOREVER);pt_input.quality_listening=native_owned&&!closing&&!r2_ble_initiating();(void)k_mutex_unlock(&pt_lock);
 }
}
void r2_radio_service_tick(struct r2_radio_scope scope,const struct r2_source_clock *source,int64_t utc,uint64_t now){
 tick_owned(scope,source,utc,now);
 gm_radio_publish();
}
void r2_radio_service_view(struct r2_radio_precision_view *out,uint64_t now){struct pt_output s;output_read(&s,now);*out=s.view;}
void r2_radio_service_client(const struct r2_group_client *c,const struct r2_enrollment *r,uint64_t now){
 (void)now;if(!c||!r||!r->group.id)return;(void)k_mutex_lock(&pt_lock,K_FOREVER);pt_input.client=*c;pt_input.roster=*r;(void)k_mutex_unlock(&pt_lock);
}
void r2_radio_service_client_state(struct r2_group_client *out,uint64_t now){struct pt_output s;output_read(&s,now);*out=s.bootstrap;out->source=out->source&&s.scope.follower&&now<s.scope.until_us;}
bool r2_radio_service_client_clock(uint64_t *utc,uint64_t now){struct pt_output s;output_read(&s,now);if(!utc||!s.utc_valid)return false;*utc=s.utc+now-s.sampled;return true;}
void r2_radio_service_context(struct r2_group_client *out,uint64_t now){struct pt_output s;output_read(&s,now);*out=s.context;}
void r2_radio_service_time(struct r2_group_client *out,uint64_t *frame,uint64_t now){struct pt_output s;output_read(&s,now);*out=s.context;*frame=s.source_frame;}
void r2_radio_service_link_quality(struct r2_link_quality_view *out){
 (void)k_mutex_lock(&pt_lock,K_FOREVER);*out=pt_output.quality;(void)k_mutex_unlock(&pt_lock);
}
bool r2_radio_service_direct_opportunity(uint32_t *accepted){if(!accepted||!opportunity_complete)return false;struct pt_output s;output_read(&s,service_now());opportunity_complete=false;*accepted=s.view.accepted;return true;}

int r2_radio_service_supervise(uint64_t now){
 (void)k_mutex_lock(&pt_lock,K_FOREVER);uint64_t until=native_close_until;(void)k_mutex_unlock(&pt_lock);
 if(until&&now>=until)fault(-ETIMEDOUT);
 return atomic_get(&error);
}

bool r2_radio_service_serving(uint64_t now){struct pt_output out;output_read(&out,now);(void)k_mutex_lock(&pt_lock,K_FOREVER);bool started=native_gm_started;(void)k_mutex_unlock(&pt_lock);return started&&out.scope.gm;}

uint32_t r2_radio_service_stack_unused(void){
#ifdef CONFIG_INIT_STACKS
 size_t unused;if(!k_thread_stack_space_get(&pt_thread,&unused)&&unused<=UINT32_MAX)return (uint32_t)unused;
#endif
 return UINT32_MAX;
}

void r2_radio_service_snapshot(struct r2_radio_precision_view *view,struct r2_group_client *time,
 struct r2_group_client *coarse,uint64_t *frame,uint64_t now){
 struct pt_output s;output_read(&s,now);*view=s.view;*time=s.context;*frame=s.source_frame;
 *coarse=s.bootstrap;coarse->source=coarse->source&&s.scope.follower&&now<s.scope.until_us;
}

void r2_radio_service_continuous(bool rc){continuous_rc=rc;}
int r2_radio_service_reset(uint32_t generation){
 if(!generation||!r2_radio_service_closed())return -EBUSY;
 (void)k_mutex_lock(&pt_lock,K_FOREVER);pt_input.stopped=true;pt_input.grant=(struct r2_radio_scope){0};pt_input.client=(struct r2_group_client){0};
 reset_requested=generation;bool done=reset_ack==generation;(void)k_mutex_unlock(&pt_lock);return done?0:-EAGAIN;
}
uint32_t r2_radio_service_profile_flushes(void){(void)k_mutex_lock(&pt_lock,K_FOREVER);uint32_t count=reset_flushes;(void)k_mutex_unlock(&pt_lock);return count;}

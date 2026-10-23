/* SPDX-License-Identifier: Apache-2.0
 * Serialized owner: every new group peer authenticates on one transient
 * connection before negotiating a control role or a bounded freshness bootstrap. */
#include "group_admission.h"
#include "ble_transport.h"
#include "timing_events.h"
#ifdef R2_LORA
#include "lora_service.h"
#endif
#include <errno.h>
#include <string.h>
#if defined(R2_SERVICE_CLIENTS) || defined(R2_FIELD_FIXTURE) || defined(R2_RC_DYNAMIC_FIXTURE)
#include <zephyr/sys/printk.h>
#endif
#if defined(R2_RC_DYNAMIC_FIXTURE)
extern bool r2_rc7_fixture_gm_hot(void);
#endif
static struct r2_join_exchange exchange;
static struct r2_group_client client;
static struct r2_join_record pending;
static uint64_t sequence, retry_at, close_at;
static bool refresh_required;
void r2_group_admission_refresh_anchor(void){refresh_required=true;r2_ble_client_bootstrap(true,true);}
void r2_group_admission_require_anchor(void){
 client.source=false;r2_group_admission_refresh_anchor();
}
static bool active, waiting, promote, sent_demand, control_requested;
/* G5 STATUS request/response/confirmation stays on the original JOIN owner. */
static uint8_t gm5_phase;static bool gm5_send,gm5_done;
static uint64_t control_denied_until;
static struct r2_local_demand demand;
static bool received_request,relay_allowed=true,relay_ready;
static uint64_t pending_parent,pending_parent_boot,pending_parent_until,direct_receive_since;
void r2_group_admission_relay_allowed(bool allowed){relay_allowed=allowed;}
void r2_group_admission_relay_ready(bool ready){relay_ready=ready;}
static uint32_t direct_accepted;static unsigned missed_opportunities;static bool poor_direct;
static uint64_t direct_progress_at,direct_receive_since_quality,discovery_since;
static bool direct_delivery_seen;
void r2_group_admission_direct_opportunity(uint32_t accepted,uint64_t now){
 /* Completed cold-discovery attempts only. Once PA delivery is observed,
  * JOIN refresh, map warmup and correction count are not radio-loss evidence. */
 if(direct_delivery_seen&&now>=direct_progress_at&&now-direct_progress_at<3000000){missed_opportunities=0;discovery_since=0;return;}
 if(!discovery_since)discovery_since=now;
 if(accepted!=direct_accepted){direct_accepted=accepted;missed_opportunities=0;}
 else {if(missed_opportunities<3)++missed_opportunities;
  if(missed_opportunities>=3&&now>=discovery_since&&now-discovery_since>=3000000)poor_direct=true;}
}
void r2_group_admission_direct_quality(const struct r2_link_quality_view *q,uint64_t now){
 bool good=q->healthy&&q->last_rx&&now>=q->last_rx&&now-q->last_rx<3000000;
 direct_receive_since_quality=good?q->last_rx:0;
 if(q->received){direct_delivery_seen=true;direct_progress_at=q->last_rx;}
 if(q->poor){poor_direct=true;direct_receive_since=0;}
 else if(good){poor_direct=false;direct_receive_since=direct_receive_since_quality=q->last_rx;missed_opportunities=0;discovery_since=0;}
}
static struct r2_group_client timing_context,coarse_context;
static struct r2_local_demand queued_demand;
bool r2_group_admission_mesh_contact(uint64_t now){
 return (active&&exchange.peer&&exchange.peer!=exchange.self&&exchange.peer_boot&&
         (exchange.proved||exchange.accepted)&&now<exchange.until)||
        (client.peer&&client.peer_boot&&now>=client.at&&now<client.until)||
        (demand.peer&&demand.peer_boot&&now>=demand.at&&now<demand.until);
}
bool r2_group_admission_coordination_direct(const struct r2_gm_coordination *c,uint64_t now){
 /* A relay's root identity is not direct root contact. The original JOIN
  * peer/boot and current direct physical PA map must both name this root.
  * This permits normal source readmission before promotion to GM control;
  * requiring an existing promoted slot here would deadlock cold return. */
 return c&&c->root&&c->boot&&c->epoch&&now<c->until&&
  client.source&&client.peer==c->root&&client.peer_boot==c->boot&&
  r2_gm_coordination_matches(c,client.root,client.boot,client.source_epoch)&&
  now>=client.at&&now<client.until&&timing_context.source&&
  timing_context.peer==c->root&&r2_gm_coordination_matches(c,timing_context.root,timing_context.boot,timing_context.source_epoch)&&
  now>=timing_context.at&&now<timing_context.map_until&&now<timing_context.until;
}
void r2_group_admission_path_view(uint64_t out[4]){
 out[0]=poor_direct;out[1]=direct_progress_at;out[2]=demand.received?0:demand.peer;out[3]=demand.until;
}
uint64_t r2_group_admission_pending_until(uint64_t now){return queued_demand.until>now?queued_demand.until:0;}
void r2_group_admission_timing(const struct r2_group_client *c){
 /* A temporarily unavailable map is not a new authenticated root. Compare
  * concrete scopes, never a no-map snapshot against an empty coarse cache. */
 bool scoped=c->root&&c->boot&&c->source_epoch;
 if(scoped&&coarse_context.root&&(c->root!=coarse_context.root||c->boot!=coarse_context.boot||c->source_epoch!=coarse_context.source_epoch)){
  coarse_context=(struct r2_group_client){0};
 }
 if(scoped&&queued_demand.root&&(c->root!=queued_demand.root||c->boot!=queued_demand.root_boot||c->source_epoch!=queued_demand.source_epoch))
  queued_demand=(struct r2_local_demand){0};
 timing_context=*c;
 if(c->source&&c->map_until>c->at&&c->until>c->at)coarse_context=*c;
}
static bool same_timing(const struct r2_local_demand *d,const struct r2_group_client *c){
 return d->root==c->root&&d->root_boot==c->boot&&d->source_epoch==c->source_epoch;
}
static void admitted(struct r2_local_demand value,uint64_t now){
 value.at=now;demand=value;
#if defined(R2_SERVICE_CLIENTS) || defined(R2_FIELD_FIXTURE)
 printk("R2_LOCAL_DEMAND peer=%llu root=%llu boot=%llu epoch=%u until=%llu t=%llu\n",
  (unsigned long long)demand.peer,(unsigned long long)demand.root,(unsigned long long)demand.root_boot,
  demand.source_epoch,(unsigned long long)demand.until,(unsigned long long)now);
#endif
}
void r2_group_admission_demand(struct r2_local_demand *out){*out=demand;}
void r2_group_admission_parent_unavailable(uint64_t parent,uint64_t now){
 if(demand.received||!parent||demand.peer!=parent||now>=demand.until)return;
 demand.at=now;demand.until=0;poor_direct=true;refresh_required=true;
 r2_ble_admission_policy(0,true);r2_ble_client_bootstrap(true,true);
}
static struct r2_join_display recent[R2_GROUP_DISPLAY_CACHE];
/* A declined request is temporary capacity/source evidence, not a lifetime
 * ban. A coarse-only request made before eligibility is not a refusal at all.
 * Retrying negotiates the existing authenticated cap4 exchange; it grants no
 * membership, source permission or HOT authority by itself. */
static bool control_blocked(uint64_t now) {
  return now < control_denied_until;
}
static void control_result(const struct r2_join_record *r, uint64_t now) {
  if(exchange.control)control_denied_until=0;
  else if(!exchange.server&&control_requested&&(r->capabilities&1))
    control_denied_until=now>UINT64_MAX-5000000?UINT64_MAX:now+5000000;
}
static bool control_available(const struct r2_network *n) {
  return !n->gm5.enabled && !control_blocked(n->now) && n->admission.eligible && (n->capabilities & 1) && r2_ble_control_available();
}
static struct r2_join_record message(const struct r2_network *n, unsigned kind,
                                     uint64_t now) {
  struct r2_join_record r = {
      .kind = kind,
      .capabilities = n->capabilities,
      .control = exchange.control,
      .sender = n->identity,
      .receiver = exchange.peer,
      .sender_boot = n->boot,
      .receiver_boot = exchange.peer_boot,
      .client_nonce = exchange.server ? exchange.remote_nonce : exchange.nonce,
      .server_nonce = exchange.server ? exchange.nonce : exchange.remote_nonce,
      .sequence = ++sequence,
      .created_us = now};
  memcpy(r.name, n->name, 25);
  if ((kind == R2_JOIN_PROOF || kind == R2_JOIN_ACCEPT) && n->ready &&
      n->view.available && n->view.relative_ready && n->choice < n->count &&
      n->service_until > now && n->service_until - now <= 1500000 &&
      now >= n->view.evidence_us) {
    r.root = n->choice == n->self ? n->identity : n->peer[n->choice].identity;
    r.root_boot = n->root_boot;
    r.source_epoch = r2_network_source_epoch(n);
    r.publication = n->publication;
    r.utc_us = n->view.utc_second * 1000000 + now - n->view.evidence_us;
    r.remaining_us = (uint32_t)(n->service_until - now);
    r.source_flags = 1;r.source_bound_us=n->map_bound_us;
  }
  if((kind==R2_JOIN_PROOF||kind==R2_JOIN_ACCEPT)&&!r.source_flags&&
     timing_context.source&&now>=timing_context.at&&now<timing_context.map_until&&now<timing_context.until){
   r.root=timing_context.root;r.root_boot=timing_context.boot;r.source_epoch=timing_context.source_epoch;
   r.publication=timing_context.publication;r.utc_us=timing_context.utc_us+now-timing_context.at;
   r.remaining_us=(uint32_t)(timing_context.until-now);r.source_flags=1;r.source_bound_us=n->map_bound_us;
  }
  /* A last actually validated map may supply coarse bootstrap during a
   * transient ACL interruption. Its original source expiry and explicit
   * uncertainty survive; this never makes timing_context.map_until live. */
  if((kind==R2_JOIN_PROOF||kind==R2_JOIN_ACCEPT)&&!r.source_flags&&coarse_context.source&&
     now>=coarse_context.at&&now<coarse_context.until){
   uint64_t age=now-coarse_context.at,bound=n->map_bound_us+(age+499)/500+64;
   if(bound<=50000){
    r.root=coarse_context.root;r.root_boot=coarse_context.boot;r.source_epoch=coarse_context.source_epoch;
    r.publication=coarse_context.publication;r.utc_us=coarse_context.utc_us+age;
    r.remaining_us=(uint32_t)(coarse_context.until-now);r.source_flags=1;r.source_bound_us=(uint32_t)bound;
   }
  }
  /* PSK possession is independent of UTC. A cold client asks for service;
   * an already timed client waits for real missed listening opportunities. */
  if(kind==R2_JOIN_PROOF&&!exchange.server&&!exchange.control&&
     !(n->capabilities&1)&&
     ((poor_direct&&demand.until<=now)||(!demand.received&&now<demand.until&&exchange.peer==demand.peer&&
      ((now>=demand.at&&now-demand.at>=R2_LOCAL_DEMAND_RENEW_US)||exchange.peer_boot!=demand.peer_boot)))&&
     !(demand.received&&now<demand.until)&&client.passive&&(exchange.peer_capabilities&4)){
   r.root=r.root_boot=r.utc_us=0;r.source_epoch=r.publication=0;
   r.remaining_us=0;r.source_flags=0;r.source_bound_us=0;r.demand=1;
  }
  if(kind==R2_JOIN_ACCEPT&&received_request){
   bool allowed=relay_allowed&&(n->capabilities&4)&&!n->local_gm&&exchange.peer!=timing_context.root&&!(demand.until>now&&!demand.received);
   bool live=relay_ready&&timing_context.source&&now>=timing_context.at&&now<timing_context.map_until&&now<timing_context.until;
   r.demand=allowed?(live?2:4):3;
   /* Any selected scope is the server's authenticated choice. No client
    * synchronization claim or source lifetime is inferred from demand. */
   if(!r.source_flags){
    r.root=r.root_boot=r.utc_us=0;r.source_epoch=r.publication=0;
    r.remaining_us=0;r.source_bound_us=0;
    if(allowed&&timing_context.root&&timing_context.boot&&timing_context.source_epoch&&timing_context.publication){
     r.root=timing_context.root;r.root_boot=timing_context.boot;
     r.source_epoch=timing_context.source_epoch;r.publication=timing_context.publication;
    }
   }
   if(!allowed&&!n->local_gm){r.root=r.root_boot=r.utc_us=0;r.source_epoch=r.publication=0;
    r.remaining_us=0;r.source_flags=0;r.source_bound_us=0;}
  }
  /* JOIN coarse bootstrap keeps its older1.5s ceiling. GR3's independent
   *1650ms serving permission must not make this existing codec reject JOIN. */
  if(r.remaining_us>1500000)r.remaining_us=1500000;
  return r;
}
static void accepted_peer(struct r2_network *n, const struct r2_join_record *r,
                          uint64_t causal_sent, uint64_t now) {
  if(!exchange.control)r2_ble_admission_completed(exchange.peer,now);
  if((n->capabilities&4)&&r->demand==1&&!exchange.control&&exchange.server){received_request=true;}
  /* A fresh completed proof without demand cancels only this peer's still
   * pending acquisition. It cannot withdraw an aggregate live service lease
   * which may also cover other clients. No extra record or wire field. */
  if(exchange.server&&!exchange.control&&!r->demand&&queued_demand.peer==r->sender&&queued_demand.peer_boot==r->sender_boot)
    queued_demand=(struct r2_local_demand){0};
  /* Existing control participants are not demoted by a guest's lack of a
   * control slot. A new capable node joining a full core becomes a client. */
  control_result(r,now);
  if(n->gm5.enabled&&r->source_flags&&r->root==r->sender&&r->root_boot==r->sender_boot&&r->root!=n->identity){
   /* Current causal legacy JOIN can inhibit election but never grants G5 HOT.
    * The bounded negative hold is separate from its original timing permit. */
   if(n->gm5.role!=R2_GM5_ACTIVE){n->gm5.active=r->root;n->gm5.active_boot=r->root_boot;n->gm5.active_until=now+R2_GM5_STATUS_US;}
  }
  bool passive =
      !(n->capabilities & 1) || (!n->admission.eligible&&!r2_ble_control_member()) ||
      (!exchange.server && !r2_ble_control_member() && !exchange.control &&
       r->source_flags && r->root!=n->identity && (r->capabilities & 1));
  /* G5 inventory JOIN cannot temporarily demote a serving ACTIVE into the
   * legacy public-client timing path before STATUS/selection is resolved.
   * Publish the current G5 owner policy immediately, as tick does. */
#if defined(R2_GM5_NATIVE)
  if(n->gm5.enabled)passive=!r2_gm5_control_peer(n)&&!n->local_gm;
#endif
  {
#if defined(R2_GM5_NATIVE)
    if(n->gm5.enabled)client.passive=passive;
    else
#endif
    if(passive)client.passive=true;
    /* Bootstrap without usable time cannot renew or revoke an
     * independently authorized timing scope. Its original expiry and coarse
     * anchor continue to fence it in the timing owner. */
    struct r2_join_time time;
    bool timed=r2_group_join_time(&time, r, causal_sent, now, n->map_bound_us);
#if defined(R2_SERVICE_CLIENTS) || defined(R2_FIELD_FIXTURE)
    printk("R2_CLIENT_BOOTSTRAP self=%llu source=%u timed=%u round=%llu bound=%u remaining=%u root=%llu t=%llu\n",
           (unsigned long long)n->identity,r->source_flags,timed,
           (unsigned long long)(now-causal_sent),n->map_bound_us,r->remaining_us,
           (unsigned long long)r->root,(unsigned long long)now);
#endif
    /* A downstream proof authenticates a guest, not a selected parent. In
     * passive server mode it must not replace the replay/UTC anchor which
     * keeps our independent upstream receiver alive during admission. */
    /* GM-profile admission can be the server of an incoming independent
     * root proof while still waiting for its own source. The RC guest fence
     * must not disable this ordinary coarse acquisition path. Require the
     * authenticated peer's own boot/root, never a downstream inherited map. */
    bool incoming_root=(n->capabilities&1)&&!(n->capabilities&4)&&
      (r->capabilities&1)&&r->root==r->sender&&r->root!=n->identity&&
      r->root_boot==r->sender_boot;
    bool selected_time=timed&&(!exchange.server||!passive||incoming_root);
#ifdef R2_LORA
    struct r2_gm_coordination coord;r2_lora_coordination(&coord);
    if(r2_gm_coordination_live(&coord,n->identity,now))selected_time=selected_time&&
      r->sender==coord.root&&r2_gm_coordination_matches(&coord,r->root,r->root_boot,r->source_epoch);
#endif
    if(selected_time){refresh_required=false;if(!n->gm5.enabled||n->gm5.bridge)r2_ble_admission_anchor_deadline(exchange.peer,time.until);} /* JOIN is coarse bootstrap, never PA health. */
    if (selected_time)
      client = (struct r2_group_client){.passive = client.passive,
                                        .source = true,
                                        .peer=exchange.peer,.peer_boot=exchange.peer_boot,
                                        .root = r->root,
                                        .boot = r->root_boot,
                                        .source_epoch = r->source_epoch,
                                        .publication = r->publication,
                                        .utc_us = time.utc_us,
                                        .at = now,
                                        .until = time.until,
                                        .uncertainty_us = time.uncertainty_us};
  }
  if(passive&&!n->gm5.enabled){
    n->profile_client = n->self;
    r2_ble_group_client(true);
  }
}
void r2_group_admission_client(struct r2_group_client *out) { *out = client; }
/* A validated same-scope clock is already stronger freshness evidence than
 * another guest exchange. Use the same independent source-association budget
 * used when serving this map to peers, plus drift/quantization; never an RX
 * arrival, cached unavailable map, new scope or authentication result alone. */
static void anchor_from_live_map(const struct r2_network *n,uint64_t now){
 const struct r2_group_client *c=&timing_context;
 if(!client.source||!c->source||client.root!=c->root||client.boot!=c->boot||
    client.source_epoch!=c->source_epoch||!n->map_bound_us||n->map_bound_us>50000||
    now<c->at||now>=c->map_until||now>=c->until)return;
 uint64_t age=now-c->at,bound=n->map_bound_us+(age+499)/500+64;
 if(bound>=100000||c->until-now<=bound||c->utc_us>UINT64_MAX-age)return;
 /* The captured map already clipped this original source deadline. Repeated
  * coarse-anchor refresh must not subtract the same bound each control tick. */
 client.utc_us=c->utc_us+age;client.at=now;client.until=c->until;
 client.uncertainty_us=(uint32_t)bound;client.publication=c->publication;
 refresh_required=false;
}
/* One original transport resource deadline owns JOIN and STATUS. Phase expiry
 * may shorten it; a causal CONTACT->ACQUIRE transition never lengthens it. */
static void admission_clip(const struct r2_network *n,uint64_t now){
 if(!n->gm5.enabled)return;
 uint64_t until=r2_ble_admission_deadline();
 if(until<exchange.until)exchange.until=until;
 uint64_t phase=n->gm5.attempt_until;
 /* A retained ACTIVE issuer can reconnect until its original obligation ends.
  * This never lengthens an existing JOIN or its native resource deadline. */
 if(n->gm5.role==R2_GM5_ACTIVE&&phase&&now>=phase&&
    now<n->gm5.issued_until)phase=n->gm5.issued_until;
 if(phase&&phase<exchange.until)exchange.until=phase;
}
void r2_group_admission_gm5_policy(struct r2_network *n,uint64_t now){
#if defined(R2_GM5_NATIVE)
 uint64_t attempted=r2_ble_gm5_attempt_at();
 if(attempted)r2_gm5_poll_completed(n,attempted);
 r2_ble_gm5_policy(true,r2_gm5_control_peer(n),r2_gm5_poll_due(n,now)||n->gm5.bridge);
 /* This window gates new work. Transport alone rechecks retained ownership. */
 bool cold=n->gm5.role==R2_GM5_COLD&&!n->gm5.bridge;
 if(cold)r2_ble_client_window(r2_gm5_poll_due(n,now));
#else
 (void)n;(void)now;
#endif
}
void r2_group_admission_tick(struct r2_network *n, uint64_t now) {
  if (!n->group.id)
    return;
#if defined(R2_RC_DYNAMIC_FIXTURE)
  static uint64_t hot_report;
  if(r2_rc7_fixture_gm_hot() && now>=hot_report){
    hot_report=now+1000000;
    printk("HOT_ADMISSION id=%llx t=%llu eligible=%u gm=%u gps=%u candidate=%u "
           "relative=%u passive=%u blocked=%u member=%u active=%u server=%u "
           "control=%u ready=%u profile_client=%u choice=%u coordinator=%u hot_refusal=%u\n",
           (unsigned long long)n->identity,(unsigned long long)now,n->admission.eligible,
           n->gm_mask,n->gnss_mask,n->candidate.available,n->relative_source,
           client.passive,control_blocked(now),r2_ble_control_member(),active,
           exchange.server,exchange.control,r2_ble_ready(r2_ble_admission_peer()),
           n->profile_client,n->choice,n->coordinator,r2_network_hot_refusal(n));
  }
#endif
  if(active)admission_clip(n,now);
  anchor_from_live_map(n,now);
  if(now>=pending_parent_until)pending_parent=pending_parent_boot=pending_parent_until=0;
  bool cancel_pending=pending_parent&&!poor_direct&&direct_receive_since_quality;
  bool publishing=demand.received&&now<demand.until;
  /* A serving RC still needs its root's coarse replay anchor after RX loss.
   * Its downstream requester cannot become that upstream clock. The current
   * authenticated scope selects this identity; no unit name or address pin. */
  uint64_t parent=publishing?demand.root:!demand.received&&now<demand.until?demand.peer:cancel_pending?pending_parent:0;
  bool seek_relay=!publishing&&(poor_direct||parent!=0);
#ifdef R2_LORA
  struct r2_gm_coordination coord;r2_lora_coordination(&coord);
  if(r2_gm_coordination_live(&coord,n->identity,now)){
   /* Seek the canonical root directly, not TC01's immediate relay parent.
    * Existing owners still retire through their original asynchronous bounds.
    * Wrong-root/relay time cannot survive as the selected public clock. */
   parent=coord.root;seek_relay=false;
   if(client.source&&(client.peer!=coord.root||
      !r2_gm_coordination_matches(&coord,client.root,client.boot,client.source_epoch)))
    r2_group_admission_require_anchor();
  }
#endif
#if defined(R2_GM5_NATIVE)
  if(n->gm5.enabled){
   uint64_t selected=r2_gm5_control_peer(n);
   if(selected&&!r2_ble_control_member())parent=selected;
   else if(!parent&&n->gm5.role==R2_GM5_COLD&&n->gm5.active&&
           n->gm5.active!=n->identity&&now<n->gm5.active_until)
    /* Spend this existing sparse opportunity renewing the known incumbent,
     * not cycling unrelated inventory past its original contact expiry.
     * This is a discovery preference only; JOIN/STATUS must still prove it. */
    parent=n->gm5.active;
   seek_relay=false;
  }
#endif
  r2_ble_admission_policy(parent,seek_relay);
  /* Only authenticated, originally bounded upstream time inhibits local
   * candidacy from disrupting an existing service during control admission. */
  n->observed_root=client.source?client.root:0;n->observed_boot=client.boot;
  n->observed_epoch=client.source_epoch;n->observed_until=client.source?client.until:0;
  if(r2_ble_admission_scan_opportunity()&&(!client.source||now>=client.until)&&!n->local_gm)
   r2_group_admission_direct_opportunity(direct_accepted,now);
  if(queued_demand.until){
   if(now>=queued_demand.until||!relay_allowed||queued_demand.peer==timing_context.root||(!demand.received&&demand.until>now)||n->local_gm||!(n->capabilities&4)||(queued_demand.root&&timing_context.root&&timing_context.boot&&timing_context.source_epoch&&!same_timing(&queued_demand,&timing_context)))
    queued_demand=(struct r2_local_demand){0};
   else if(timing_context.source&&now>=timing_context.at&&now<timing_context.map_until&&now<timing_context.until){
    if(!queued_demand.root){queued_demand.root=timing_context.root;queued_demand.root_boot=timing_context.boot;
     queued_demand.source_epoch=timing_context.source_epoch;}
    admitted(queued_demand,now);queued_demand=(struct r2_local_demand){0};
   }
  }
  if(!(n->capabilities&1)||(!n->admission.eligible&&!r2_ble_control_member()))client.passive=true;
  if(!n->gm5.enabled&&client.passive&&n->admission.eligible&&(n->capabilities&1)&&!control_blocked(now)){
   client.passive=false;n->profile_client=255;r2_ble_group_client(false);
  }
  if(client.passive){
   /* Availability scheduling reserves two62.5ms PA opportunities below the
    * unchanged249900us rejection ceiling. Refresh at100ms, urgent at125ms;
    * retry cooldown is unrelated to anchor freshness or relay demand. */
   uint64_t elapsed=now>=client.at?now-client.at:UINT64_MAX;
   uint64_t guard=elapsed==UINT64_MAX?UINT64_MAX:client.uncertainty_us+elapsed/500+100;
   r2_ble_client_bootstrap(cancel_pending||refresh_required||!client.source||now>=client.until||guard>=100000||(!(demand.received&&now<demand.until)&&(poor_direct||parent)&&(demand.until<=now||(now>=demand.at&&now-demand.at>=R2_LOCAL_DEMAND_RENEW_US))),
                          refresh_required||!client.source||now>=client.until||guard>=125000);
  }
  bool ready = r2_ble_ready(r2_ble_admission_peer());
  uint64_t peer = r2_ble_peer_identity(r2_ble_admission_peer(), NULL);
#if defined(R2_GM5_NATIVE)
  if(n->gm5.enabled){
   bool control=r2_gm5_control_peer(n)!=0;
   client.passive=!control&&!n->local_gm;
   r2_ble_group_client(client.passive);
   if(!n->gm5.bridge)r2_ble_client_bootstrap(r2_gm5_poll_due(n,now)||control,false);
   /* Retain the original causal anchor for ordinary COLD timing reception
    * and a later appointment. Independent PT reception grants no HOT role. */
  }
  if(n->gm5.enabled&&active&&gm5_send&&ready&&exchange.accepted&&now<exchange.until){
   uint8_t wire[R2_GM5_STATUS_BYTES];int rc=r2_gm5_status_encode(n,&exchange,gm5_phase,wire);
   if(!rc)rc=r2_ble_send(r2_ble_admission_peer(),wire);
   if(!rc){gm5_send=false;
    if(gm5_phase==2){gm5_done=true;promote=r2_gm5_wants_control(n,exchange.peer);exchange.control=promote;close_at=now+50000;r2_gm5_poll_completed(n,now);r2_ble_admission_completed(exchange.peer,now);}
   }
  }
#endif
  if (active && ready && close_at && now >= close_at && now<exchange.until && promote) {
    int slot = r2_ble_admission_promote();
    if (slot >= 0) {
      r2_timing_event(R2_TIMING_GM_CONTROL,0,exchange.peer,7,(uint32_t)slot,exchange.control);
      r2_network_joined(n,(unsigned)slot,&exchange,now);
      client.passive=false; /* A control promotion does not destroy causal time. */
      n->profile_client = 255;
      r2_ble_group_client(!(n->capabilities & 1));
      active = waiting = promote = false;
      close_at = 0;
      retry_at = now + 100000;
      return;
    }
    if (slot != -EAGAIN) {
      r2_ble_admission_release();
      active = waiting = promote = false;
    }
  }
  if (active && (!r2_ble_admission_connected() || peer != exchange.peer || now >= exchange.until ||
                 (close_at && now >= close_at && !promote))) {
    r2_timing_event(R2_TIMING_GM_CONTROL,-ETIMEDOUT,exchange.peer,6,pending.kind,
      (now>=exchange.until)|(!r2_ble_admission_connected()<<1)|((peer!=exchange.peer)<<2)|((close_at&&now>=close_at)<<3));
    if (ready)
      r2_ble_admission_release();
    active = waiting = promote = false;
    close_at = 0;
#if defined(R2_GM5_NATIVE)
    if(n->gm5.enabled)r2_gm5_poll_completed(n,now);
#endif
    retry_at = now + 200000;
  }
  if (!active && ready && peer && (n->gm5.enabled&&!n->gm5.bridge?r2_ble_admission_deadline()>now:now>=retry_at)) {
    uint64_t nonce;
    if (r2_ble_admission_nonce(&nonce)) {
      r2_ble_admission_release();
      return;
    }
    if (r2_group_join_begin(&exchange, r2_ble_admission_server(), n->identity,
                            peer, n->boot, nonce, now)) {
      r2_ble_admission_release();
      return;
    }
    admission_clip(n,now);
    active = true;
    r2_timing_event(R2_TIMING_GM_CONTROL,0,exchange.peer,3,exchange.server,5000);
    sequence = 0;sent_demand=false;received_request=false;gm5_phase=0;gm5_send=gm5_done=false;
    promote = false;
    if (!exchange.server) {
      exchange.control = control_available(n);
      control_requested=exchange.control;
      pending = message(n, R2_JOIN_REQUEST, now);
      exchange.request_at = now;
      waiting = true;
    }
  }
  if (active && waiting && ready) {
    /* GATT discovery completes independently at the two ends. An authenticated
     * incoming REQUEST may precede our own discovery completion; retain this
     * bounded exchange, then stamp the response when actually submitted. */
    uint64_t echo = pending.echo_us;
    pending = message(n, pending.kind, now);
    pending.echo_us = echo;
    uint8_t wire[R2_GROUP_JOIN_BYTES];
    int rc = r2_group_join_encode(wire, &pending, &n->group);
    if (!rc)
      rc = r2_ble_send(r2_ble_admission_peer(), wire);
    if (!rc) {
      r2_timing_event(R2_TIMING_GM_CONTROL,0,exchange.peer,4,pending.kind,
                      now<exchange.until?(uint32_t)((exchange.until-now)/1000):0);
      if (pending.kind == R2_JOIN_REQUEST) exchange.request_at = now;
      if (pending.kind == R2_JOIN_CHALLENGE) exchange.challenge_at = now;
      if (pending.kind == R2_JOIN_PROOF) exchange.proof_at = now;
      waiting = false;
      if(pending.demand==1)sent_demand=true;
      if (pending.kind == R2_JOIN_ACCEPT) {
        if(pending.demand==2||pending.demand==4){
         struct r2_local_demand value={pending.root,pending.root_boot,exchange.peer,exchange.peer_boot,
          now,now+R2_LOCAL_DEMAND_LEASE_US,pending.source_epoch,true};
         if(pending.demand==2)admitted(value,now);
         else{queued_demand=value;
#if defined(R2_SERVICE_CLIENTS) || defined(R2_FIELD_FIXTURE)
          printk("R2_LOCAL_PENDING peer=%llu root=%llu until=%llu t=%llu\n",(unsigned long long)value.peer,
           (unsigned long long)value.root,(unsigned long long)value.until,(unsigned long long)now);
#endif
         }
        }
        exchange.accepted = true;
        promote = exchange.control;
        close_at = now + 100000;
        if(n->gm5.enabled&&(exchange.peer_capabilities&1)){close_at=exchange.until;gm5_phase=0;gm5_send=false;}

      }
    } else if (rc != -EAGAIN && rc != -ENOMEM && rc != -ENOBUFS) {
      r2_ble_admission_release();
      active = waiting = promote = false;
    }
  }
  if (client.passive&&!n->gm5.enabled) {
    n->profile_client = n->self;
    r2_ble_group_client(true);
  }
}
void r2_group_admission_receive(struct r2_network *n,
                                const struct r2_ble_event *event,
                                uint64_t now) {
  if(active)admission_clip(n,now);
  if (event->peer != r2_ble_admission_peer() || now < event->at)
    return;
#if defined(R2_GM5_NATIVE)
  if(n->gm5.enabled&&event->length==R2_GM5_STATUS_BYTES&&event->data[0]==19&&event->data[1]<3){
   if(!exchange.server&&gm5_phase!=0)return;
   unsigned expected=exchange.server?(gm5_phase==1?2:0):1;
   if(!active||gm5_done||event->data[1]!=expected||r2_gm5_status_receive(n,&exchange,expected,event->data,event->length,now))return;
   if(exchange.server&&expected==0){gm5_phase=1;gm5_send=true;}
   else if(!exchange.server){gm5_phase=2;gm5_send=true;}
   else{gm5_done=true;promote=r2_gm5_wants_control(n,exchange.peer);exchange.control=promote;close_at=now+50000;r2_gm5_poll_completed(n,now);r2_ble_admission_completed(exchange.peer,now);}
   return;
  }
#endif
  /* G5 inventory above uses its original authenticated JOIN lifetime.
   * Legacy JOIN/coarse-time retains its independent receive-age guard. */
  if(now-event->at>=500000)return;
  struct r2_join_record r;
  if (r2_group_join_decode(&r, event->data, event->length, &n->group))
    return;
  if (!active) {
    if (r.kind != R2_JOIN_REQUEST || r.receiver != n->identity ||
        !r2_ble_admission_server())
      return;
    uint64_t nonce;
    if(r2_ble_admission_nonce(&nonce))return;
    uint8_t address[16];
    r2_group_pair_key(address, &n->group, r.sender, r.sender);
    address[5] |= 0xc0;
    if (r2_ble_admission_bind(r.sender, address) ||
        r2_group_join_begin(&exchange, true, n->identity, r.sender, n->boot,
                            nonce, now))
      return;
    admission_clip(n,now);
    active = true;
    waiting = promote = false;
    sequence = 0;sent_demand=false;received_request=false;gm5_phase=0;gm5_send=gm5_done=false;
    close_at = 0;
  }
  int receive_rc=r2_group_join_receive(&exchange,&r,now);
  r2_timing_event(R2_TIMING_GM_CONTROL,receive_rc,r.sender,5,r.kind,
                  exchange.control|(exchange.server<<1));
  if (receive_rc)
    return;
  if (exchange.server && r.kind == R2_JOIN_REQUEST) {
    exchange.control =
        exchange.control && control_available(n) && (r.capabilities & 1);
    pending = message(n, R2_JOIN_CHALLENGE, now);
    pending.echo_us = exchange.request_at;
    exchange.challenge_at = now;
    waiting = true;
  } else if (!exchange.server && r.kind == R2_JOIN_CHALLENGE) {
    pending = message(n, R2_JOIN_PROOF, now);
    pending.echo_us = exchange.challenge_at;
    exchange.proof_at = now;
    waiting = true;
  } else if (exchange.server && r.kind == R2_JOIN_PROOF) {
    r2_group_join_remember(recent, &exchange, now);
    accepted_peer(n, &r, exchange.challenge_at, now);
#if defined(R2_SERVICE_CLIENTS) || defined(R2_FIELD_FIXTURE)
    printk("R2_CLIENT_ADMIT self=%llu peer=%llu control=%u name=%s t=%llu\n",
           (unsigned long long)n->identity, (unsigned long long)exchange.peer,
           exchange.control, exchange.name, (unsigned long long)now);
#endif
    pending = message(n, R2_JOIN_ACCEPT, now);
    pending.echo_us = exchange.proof_at;
    waiting = true;
  } else if (!exchange.server && r.kind == R2_JOIN_ACCEPT) {
    r2_group_join_remember(recent, &exchange, now);
#if defined(R2_SERVICE_CLIENTS) || defined(R2_FIELD_FIXTURE)
    printk("R2_CLIENT_ADMIT self=%llu peer=%llu control=%u name=%s t=%llu\n",
           (unsigned long long)n->identity, (unsigned long long)exchange.peer,
           exchange.control, exchange.name, (unsigned long long)now);
#endif
    accepted_peer(n, &r, exchange.proof_at, now);
    if(sent_demand&&r.demand==4){pending_parent=exchange.peer;pending_parent_boot=exchange.peer_boot;pending_parent_until=now+R2_LOCAL_DEMAND_LEASE_US;}
    else if(exchange.peer==pending_parent&&exchange.peer_boot==pending_parent_boot)pending_parent=pending_parent_boot=pending_parent_until=0;
    /* PENDING is a bounded retry hint, never permission to abandon a direct
     * owner for a relay whose captured map/publication is not live yet. */
    if(sent_demand&&r.demand==2&&!(demand.received&&now<demand.until)&&client.passive&&client.source&&(exchange.peer_capabilities&4)&&
       client.root==r.root&&client.boot==r.root_boot&&client.source_epoch==r.source_epoch)
     demand=(struct r2_local_demand){r.root,r.root_boot,exchange.peer,exchange.peer_boot,
      now,now+R2_LOCAL_DEMAND_LEASE_US,r.source_epoch,false};
    promote = exchange.control;
    /* The fixed client has received the final authenticated ACCEPT; there is
     * no remaining protocol response to send. Release on the next owner poll
     * so precision acquisition can use the rest of its client window. Generic
     * admission keeps its existing close behavior. TX callback ownership is
     * still drained by the transport before connection reuse. */
    close_at = now + (r2_ble_admission_peer()<R2_BLE_ADMISSION_PEER?2000:50000);
    if(n->gm5.enabled&&(exchange.peer_capabilities&1)){close_at=exchange.until;gm5_phase=0;gm5_send=true;}

  }
}

void r2_group_admission_reset(void){
 relay_allowed=true;relay_ready=false;
 exchange=(struct r2_join_exchange){0};client=(struct r2_group_client){0};pending=(struct r2_join_record){0};
 gm5_phase=0;gm5_send=gm5_done=false;
 sequence=retry_at=close_at=control_denied_until=0;refresh_required=active=waiting=promote=sent_demand=control_requested=received_request=poor_direct=false;
 demand=queued_demand=(struct r2_local_demand){0};timing_context=coarse_context=(struct r2_group_client){0};direct_accepted=missed_opportunities=0;
 direct_progress_at=direct_receive_since_quality=discovery_since=0;direct_delivery_seen=false;r2_ble_admission_policy(0,false);
 pending_parent=pending_parent_boot=pending_parent_until=direct_receive_since=0;
}

/* SPDX-License-Identifier: Apache-2.0 */
#include "direct_control_state.h"
#include <errno.h>
#include <string.h>

static void count(uint32_t *v) { if (*v != UINT32_MAX) { ++*v; } }
static uint64_t earlier(uint64_t a, uint64_t b) { return a < b ? a : b; }
static int refuse(struct v12_direct_control_state *s, uint8_t reason, int error)
{
  if (s) { count(&s->refused); s->last_refusal = reason; }
  return -error;
}
static void clear_role(struct v12_direct_control_state *s)
{
  s->active_root = s->pending_root = s->standby = V12_DC_NONE;
  s->active_until_us = s->pending_until_us = s->view_until_us = 0;
  s->active_root_boot = s->pending_root_boot = s->view_root_boot = 0;
  s->standby_ready_until_us = 0;
  s->view.available = false;
  s->service_received_until_us=s->service_acked_until_us=0;
  s->service_decision=0;s->service_source_epoch=0;s->service_peer=V12_DC_NONE;
  s->phase = V12_DC_IDLE; s->uncertain=false;
}
static int clock_now(struct v12_direct_control_state *s, uint64_t now)
{
  if (!s) { return -EINVAL; }
  if (now < s->now_us || now > UINT64_MAX - V12_DC_COARSE_MAX_AGE_US) {
    clear_role(s); s->running = false; s->queue_count = 0;
    return refuse(s, V12_DC_REF_CLOCK, ERANGE);
  }
  s->now_us = now;
  return 0;
}
static struct v12_dc_member *member(struct v12_direct_control_state *s, uint8_t wire)
{
  for (unsigned i = 0; i < V12_CONTROL_MAX_GMS; ++i) {
    if (s->members[i].occupied && s->members[i].wire == wire) { return &s->members[i]; }
  }
  return NULL;
}
static const struct v12_dc_member *cmember(const struct v12_direct_control_state *s,
                                          uint8_t wire)
{
  for (unsigned i = 0; i < V12_CONTROL_MAX_GMS; ++i) {
    if (s->members[i].occupied && s->members[i].wire == wire) { return &s->members[i]; }
  }
  return NULL;
}
static bool live(const struct v12_direct_control_state *s, const struct v12_dc_member *m)
{
  return m && m->occupied && m->connected && m->authenticated &&
    (m->wire == s->self_wire || s->now_us < m->live_until_us);
}
static bool coarse_live(const struct v12_direct_control_state *s,
                         const struct v12_dc_coarse *c)
{
  return c->available && !c->qualified && c->generation &&
    c->evidence_us <= s->now_us && s->now_us < c->valid_until_us &&
    s->now_us - c->evidence_us < V12_DC_COARSE_MAX_AGE_US;
}
static uint64_t coord_boot(const struct v12_direct_control_state *s)
{
  const struct v12_dc_member *m = cmember(s, s->coordinator_wire);
  return m ? m->boot : 0;
}
static uint32_t source_epoch(const struct v12_direct_control_state *s,
                             const struct v12_dc_member *m)
{
  return m && coarse_live(s, &m->coarse) ? m->coarse.generation : 0;
}
static void retire(struct v12_direct_control_state *s, uint64_t decision)
{
  if (decision > s->retired_decision) { s->retired_decision = decision; }
}
static void discard_head(struct v12_direct_control_state *s)
{
  s->queue_head = (uint8_t)((s->queue_head + 1u) % V12_CONTROL_QUEUE_LIMIT);
  --s->queue_count;
}
static int queue(struct v12_direct_control_state *s, uint8_t kind, uint8_t target,
                  uint8_t root, uint64_t root_boot, uint64_t decision,
                  const struct v12_dc_coarse *coarse, uint8_t extra_flags)
{
  while (s->queue_count && s->outgoing[s->queue_head].record.expires_us <= s->now_us) {
    discard_head(s); count(&s->queue_expired);
  }
  if (s->queue_count == V12_CONTROL_QUEUE_LIMIT) {
    count(&s->queue_drops); return refuse(s, V12_DC_REF_QUEUE, ENOBUFS);
  }
  const struct v12_dc_member *m = cmember(s, target);
  if (!m || !m->connected || (!m->authenticated && kind!=V12_DC_REPORT) ||
      !coord_boot(s) || target == s->self_wire) {
    return refuse(s, V12_DC_REF_NON_DIRECT, ENOTCONN);
  }
  if (s->sequence == UINT64_MAX) { return refuse(s, V12_DC_REF_DECISION, EOVERFLOW); }
  unsigned at = (s->queue_head + s->queue_count) % V12_CONTROL_QUEUE_LIMIT;
  struct v12_dc_outgoing *o = &s->outgoing[at];
  memset(o, 0, sizeof(*o)); o->recipient = target; o->recipient_boot=m->boot;
  o->record = (struct v12_control_record){.kind=kind, .sender=s->self_wire,
    .target=target, .root=root, .boot=s->boot, .coordinator_boot=coord_boot(s),
    .sequence=++s->sequence, .decision=decision, .root_boot=root_boot,
    .created_us=s->now_us, .expires_us=s->now_us + V12_CONTROL_ORIGINAL_TTL_US,
    .evidence_us=s->now_us, .config_epoch=s->config_epoch, .flags=extra_flags};
  if (coarse && coarse_live(s, coarse)) {
    o->record.source_epoch=coarse->generation; o->record.source_second=coarse->utc_second;
    o->record.evidence_us=coarse->evidence_us;
    /* Epoch-only role handshakes carry no new time sample. Their own original
     * admission TTL must not inherit an older REPORT's delivery deadline.
     * Live epoch validation remains mandatory at enqueue, send and receipt. */
    if(kind==V12_DC_REPORT||kind==V12_DC_SERVICE||kind==V12_DC_SERVICE_ACK)
      o->record.expires_us=earlier(o->record.expires_us,coarse->valid_until_us);
    if (kind == V12_DC_REPORT) { o->record.flags |= V12_DC_FLAG_COARSE_VALID; }
    if((s->config_epoch==2||s->config_epoch==3) && coarse->relative_ready && (kind==V12_DC_REPORT||kind==V12_DC_SERVICE))o->record.flags|=V12_DC_FLAG_RELATIVE_RATE;
  }
  if(kind==V12_DC_SERVICE||kind==V12_DC_SERVICE_ACK){
    uint64_t role=s->self_wire==s->coordinator_wire||s->active_root==s->self_wire?s->active_until_us:s->view_until_us;
    o->record.expires_us=earlier(o->record.expires_us,role);
    if(o->record.expires_us<=s->now_us)return refuse(s,V12_DC_REF_TIME,ESTALE);
  }
  ++s->queue_count;
  return 0;
}
static uint8_t best(const struct v12_direct_control_state *s, uint8_t exclude)
{
  const struct v12_dc_member *chosen = NULL;
  for (unsigned i = 0; i < V12_CONTROL_MAX_GMS; ++i) {
    const struct v12_dc_member *m = &s->members[i];
    if (!live(s, m) || m->wire == exclude || !coarse_live(s,&m->coarse)) { continue; }
    if (!chosen || m->identity<chosen->identity ||
        (m->identity==chosen->identity && m->wire<chosen->wire)) { chosen=m; }
  }
  return chosen ? chosen->wire : V12_DC_NONE;
}
static void send_standby(struct v12_direct_control_state *s)
{
  const struct v12_dc_member *m = cmember(s, s->standby);
  if (!live(s, m) || s->standby == s->self_wire) {
    s->standby_ready_until_us = s->standby == s->self_wire ? s->now_us + V12_DC_LEASE_US : 0;
    return;
  }
  if (!queue(s, V12_DC_PREPARE, m->wire, m->wire, m->boot, s->standby_decision, &m->coarse, 0)) {
    count(&s->prepares);
  }
}
static int begin(struct v12_direct_control_state *s, uint8_t root)
{
  const struct v12_dc_member *m = cmember(s, root);
  if (!live(s, m) || s->decision == UINT64_MAX) {
    return refuse(s, V12_DC_REF_DECISION, EOVERFLOW);
  }
  uint64_t d = ++s->decision;
  s->pending_root=root; s->pending_root_boot=m->boot; s->pending_decision=d;
  s->pending_source_epoch=source_epoch(s, m);
  s->pending_until_us=s->now_us + V12_CONTROL_ORIGINAL_TTL_US;

  s->phase=V12_DC_PREPARING;
  uint8_t standby=best(s, root);
  if (s->standby != standby) { s->standby_ready_until_us=0; }
  s->standby=standby; s->standby_decision=d;
  if (root == s->self_wire) {
    count(&s->prepares); count(&s->ready_acks); count(&s->commits); count(&s->active_acks);
    if (s->active_root != root) { count(&s->elections); }
    s->active_root=root; s->active_root_boot=s->boot; s->active_decision=d;
    s->active_source_epoch=s->pending_source_epoch;
    s->active_until_us=s->fence_until_us=s->now_us + V12_DC_LEASE_US;
    if(coarse_live(s,&m->coarse))s->active_until_us=s->fence_until_us=earlier(s->active_until_us,m->coarse.valid_until_us);
    s->pending_root=V12_DC_NONE; s->pending_until_us=0; s->phase=V12_DC_RUNNING;
    s->next_renew_us=s->now_us + V12_DC_RENEW_US;
    s->next_report_us=s->now_us;s->next_service_us=s->now_us+V12_DC_REPORT_US/2;
    send_standby(s);
    return 0;
  }
  int rc=queue(s, V12_DC_PREPARE, root, root, m->boot, d, &m->coarse, 0);
  if (rc) { retire(s,d); s->pending_root=V12_DC_NONE; s->pending_until_us=0;
    s->phase=s->active_root == V12_DC_NONE ? V12_DC_IDLE : V12_DC_RUNNING; return rc; }
  count(&s->prepares); send_standby(s);
  return 0;
}
static void stop_active(struct v12_direct_control_state *s)
{
  uint8_t root=s->active_root;
  uint64_t d=s->active_decision;
  if (s->pending_root==root && s->pending_decision>d) { d=s->pending_decision; }
  if (root == V12_DC_NONE) { return; }
  retire(s,d); count(&s->stops);
  if (root == s->self_wire) {
    count(&s->stopped_acks); s->fence_until_us=0; s->phase=V12_DC_IDLE;
    s->pending_root=V12_DC_NONE; s->pending_until_us=0;
  } else {
    (void)queue(s,V12_DC_STOP,root,root,s->active_root_boot,d,NULL,0);
    s->phase=V12_DC_STOPPING; s->pending_root=root; s->pending_root_boot=s->active_root_boot;
    s->pending_decision=d; s->pending_until_us=s->fence_until_us;
  }
  s->active_root=V12_DC_NONE; s->active_until_us=0;
  s->service_received_until_us=s->service_acked_until_us=0;
}
int v12_dc_init(struct v12_direct_control_state *s, uint8_t self, uint64_t boot,
                uint8_t coordinator, uint32_t config)
{
  if (!s || self == V12_DC_NONE || coordinator == V12_DC_NONE || !boot || !config) { return -EINVAL; }
  memset(s,0,sizeof(*s)); s->self_wire=self; s->boot=boot;
  s->coordinator_wire=coordinator; s->config_epoch=config; s->running=true;
  s->active_root=s->pending_root=s->standby=V12_DC_NONE;
  s->members[0]=(struct v12_dc_member){.wire=self,.boot=boot,.identity=self,.occupied=true,
    .connected=true,.authenticated=true};
  return 0;
}
int v12_dc_set_member(struct v12_direct_control_state *s, uint8_t wire, uint64_t boot,
                      bool direct, bool connected, bool authenticated, uint64_t now)
{
  int rc=clock_now(s,now); if (rc) { return rc; }
  if (!s->running) { return -ESHUTDOWN; }
  if (wire == V12_DC_NONE || wire == s->self_wire) { return refuse(s,V12_DC_REF_SCOPE,EINVAL); }
  struct v12_dc_member *m=member(s,wire);
  bool admissible=direct && boot;
  /* Followers have one direct GM-control adjacency, their coordinator. */
  if (s->self_wire != s->coordinator_wire && wire != s->coordinator_wire) { admissible=false; }
  if (!admissible) {
    if (m) { memset(m,0,sizeof(*m)); }
    if (wire == s->coordinator_wire && s->self_wire != s->coordinator_wire) {
      retire(s,s->active_decision); retire(s,s->pending_decision); clear_role(s); s->queue_count=0;
    }
    if (connected || boot) { return refuse(s,V12_DC_REF_NON_DIRECT,EACCES); }
    return 0;
  }
  if (m && m->boot==boot && m->connected==connected && m->authenticated==authenticated) { return 0; }
  if (!m) {
    for (unsigned i=1;i<V12_CONTROL_MAX_GMS;++i) { if (!s->members[i].occupied) { m=&s->members[i]; break; } }
  }
  if (!m) { return refuse(s,V12_DC_REF_OVERFLOW,ENOSPC); }
  if (m->occupied && wire == s->coordinator_wire && s->self_wire != s->coordinator_wire) {
    retire(s,s->active_decision); retire(s,s->pending_decision); clear_role(s); s->queue_count=0;
  }
  uint64_t identity=m->occupied?m->identity:wire;
  *m=(struct v12_dc_member){.wire=wire,.boot=boot,.identity=identity,.occupied=true,.connected=connected,
    .authenticated=connected&&authenticated,.live_until_us=now + V12_DC_LEASE_US};
  if (!connected && wire==s->coordinator_wire && s->self_wire!=s->coordinator_wire) {
    retire(s,s->active_decision); retire(s,s->pending_decision); clear_role(s); s->queue_count=0;
  }
  return 0;
}
int v12_dc_identity(struct v12_direct_control_state *s, uint8_t wire, uint64_t identity)
{
  if (!s || !identity) { return -EINVAL; }
  struct v12_dc_member *m=member(s,wire);
  if (!m) { return refuse(s,V12_DC_REF_SCOPE,ENOENT); }
  m->identity=identity; return 0;
}
static void fence_local_source(struct v12_direct_control_state *s)
{
  /* A local source transition withdraws its serving/prepared role before any
   * further transport work. Returning input requires a fresh decision; an old
   * COMMIT/queued ACTIVE cannot resurrect the retired epoch. */
  uint32_t epoch=source_epoch(s,&s->members[0]);
  if (s->active_root==s->self_wire && s->active_source_epoch!=epoch) {
    retire(s,s->active_decision); count(&s->stops);
    s->active_root=V12_DC_NONE; s->active_until_us=0;
    if(s->self_wire==s->coordinator_wire)s->fence_until_us=0;
    s->phase=V12_DC_IDLE;
  }
  if (s->pending_root==s->self_wire && s->pending_source_epoch!=epoch) {
    retire(s,s->pending_decision);s->pending_root=V12_DC_NONE;s->pending_until_us=0;
    s->phase=s->active_root==V12_DC_NONE?V12_DC_IDLE:V12_DC_RUNNING;
  }
}
int v12_dc_source(struct v12_direct_control_state *s, const struct v12_dc_coarse *c, uint64_t now)
{
  int rc=clock_now(s,now); if (rc) { return rc; }
  if (!s->running || !c) { return -EINVAL; }
  if (c->qualified || (c->relative_ready && (s->config_epoch!=2&&s->config_epoch!=3))) { return refuse(s,V12_DC_REF_AUTHORITY,EACCES); }
  if (c->available && (!c->generation || c->evidence_us>now || c->valid_until_us<=now ||
      now-c->evidence_us>=V12_DC_COARSE_MAX_AGE_US)) { return refuse(s,V12_DC_REF_SOURCE,ERANGE); }
  s->local=*c;
  if (c->available) { s->local.valid_until_us=earlier(c->valid_until_us,c->evidence_us+V12_DC_COARSE_MAX_AGE_US); }
  else { memset(&s->local,0,sizeof(s->local)); }
  s->members[0].coarse=s->local;
  fence_local_source(s);
  return 0;
}
static int input_scope(struct v12_direct_control_state *s, const struct v12_control_record *r,
                        const struct v12_dc_times *t)
{
  const struct v12_dc_member *m=r ? cmember(s,r->sender) : NULL;
  if (!r || !t || r->target!=s->self_wire || r->sender==s->self_wire ||
      r->config_epoch!=s->config_epoch || !r->sequence || !r->challenge ||
      !m || r->boot!=m->boot || r->coordinator_boot!=coord_boot(s)) {
    return refuse(s,V12_DC_REF_SCOPE,EACCES);
  }
  if (!m->connected || !m->authenticated) { return refuse(s,V12_DC_REF_NON_DIRECT,EACCES); }
  if (r->kind<V12_DC_REPORT || r->kind>V12_DC_SERVICE_ACK || (r->flags&~7u) || ((r->flags&V12_DC_FLAG_RELATIVE_RATE) && ((s->config_epoch!=2&&s->config_epoch!=3) || !(r->flags&V12_DC_FLAG_COARSE_VALID) || (r->kind!=V12_DC_REPORT && r->kind!=V12_DC_SERVICE))) ||
      r->expires_us<=r->created_us || r->expires_us-r->created_us>V12_CONTROL_ORIGINAL_TTL_US ||
      r->evidence_us>r->created_us) { return refuse(s,V12_DC_REF_SCOPE,EINVAL); }
  if ((r->kind!=V12_DC_SERVICE_ACK&&r->permit_until_us) || t->permit_lower_us) { return refuse(s,V12_DC_REF_AUTHORITY,EACCES); }
  if (t->created_lower_us>t->created_upper_us || t->created_upper_us>s->now_us ||
      s->now_us-t->created_lower_us>=V12_CONTROL_ORIGINAL_TTL_US ||
      t->expires_lower_us<=s->now_us || t->evidence_lower_us>t->evidence_upper_us ||
      t->evidence_upper_us>t->created_upper_us) { return refuse(s,V12_DC_REF_TIME,ETIME); }
  bool coordinator=s->self_wire==s->coordinator_wire;
  if ((coordinator && r->sender==s->coordinator_wire) ||
      (!coordinator && r->sender!=s->coordinator_wire)) { return refuse(s,V12_DC_REF_NON_DIRECT,EACCES); }
  return 0;
}
static struct v12_dc_coarse reported(const struct v12_direct_control_state *s,
                                     const struct v12_control_record *r,
                                     const struct v12_dc_times *t)
{
  struct v12_dc_coarse c={0};
  if ((r->flags&V12_DC_FLAG_COARSE_VALID) && r->source_epoch &&
      s->now_us-t->evidence_lower_us<V12_DC_COARSE_MAX_AGE_US) {
    c=(struct v12_dc_coarse){.available=true,.relative_ready=!!(r->flags&V12_DC_FLAG_RELATIVE_RATE),.generation=r->source_epoch,
      .utc_second=r->source_second,.evidence_us=t->evidence_lower_us,
      .valid_until_us=earlier(t->expires_lower_us,t->evidence_lower_us+V12_DC_COARSE_MAX_AGE_US)};
  }
  return c;
}
/* A newer same-epoch receipt may have a shorter delivery window (SERVICE is
 * additionally capped by its role). Keep any still-live independently received
 * evidence to its own original expiry. A withdrawal, epoch or readiness change
 * always replaces it; arrival never extends either sample's lifetime. */
static void source_evidence(struct v12_direct_control_state *s,
                            struct v12_dc_member *m, struct v12_dc_coarse fresh)
{
  if(coarse_live(s,&m->coarse)&&fresh.available&&
     fresh.generation==m->coarse.generation&&fresh.relative_ready==m->coarse.relative_ready&&
     fresh.valid_until_us<m->coarse.valid_until_us)return;
  m->coarse=fresh;
}
int v12_dc_input(struct v12_direct_control_state *s, const struct v12_control_record *r,
                 const struct v12_dc_times *t, uint64_t now)
{
  int rc=clock_now(s,now); if (rc) { return rc; }
  if (!s->running) { return -ESHUTDOWN; }
  rc=input_scope(s,r,t); if (rc) { return rc; }
  bool coordinator=s->self_wire==s->coordinator_wire;
  struct v12_dc_member *m=member(s,r->sender);
  uint64_t until=t->created_lower_us+V12_DC_LEASE_US;
  if(r->kind==V12_DC_SERVICE||r->kind==V12_DC_SERVICE_ACK){
    /* Separate functional coarse service. The source is current and selected;
     * no precision permission, clock fit, or GM/PPS association is inferred. */
    uint8_t root=coordinator?s->active_root:s->active_root==s->self_wire?s->self_wire:s->active_root;
    uint64_t root_boot=coordinator?s->active_root_boot:root==s->self_wire?s->boot:s->view_root_boot;
    uint64_t decision=coordinator||root==s->self_wire?s->active_decision:s->view_decision;
    const struct v12_dc_coarse *source=root==s->self_wire?&s->local:
      coordinator&&cmember(s,root)?&cmember(s,root)->coarse:&s->view;
    uint64_t role_until=coordinator||root==s->self_wire?s->active_until_us:s->view_until_us;
    if(root==V12_DC_NONE||!decision||r->root!=root||r->root_boot!=root_boot||
       (r->kind==V12_DC_SERVICE?r->decision!=decision:
        (!r->decision||r->decision>decision||r->decision<=s->retired_decision))||
       !(r->kind==V12_DC_SERVICE ? source->available&&!source->qualified&&source->generation : coarse_live(s,source))||
       r->source_epoch!=source->generation||
       now>=role_until||(r->kind==V12_DC_SERVICE&&r->permit_until_us))return refuse(s,V12_DC_REF_SOURCE,ESTALE);
    uint64_t service_until=earlier(role_until,earlier(source->valid_until_us,t->expires_lower_us));
    if(r->kind==V12_DC_SERVICE){
      if((coordinator&&(r->sender!=root||(r->flags&~V12_DC_FLAG_RELATIVE_RATE)!=V12_DC_FLAG_COARSE_VALID))||
         (!coordinator&&(r->flags&~V12_DC_FLAG_RELATIVE_RATE)!=(V12_DC_FLAG_COARSE_VALID|V12_DC_FLAG_CONTROL_VIEW)))
        return refuse(s,V12_DC_REF_AUTHORITY,EACCES);
      struct v12_dc_coarse received=reported(s,r,t);
      if(!coarse_live(s,&received)||(received.relative_ready&&!source->relative_ready))return refuse(s,V12_DC_REF_SOURCE,ESTALE);
      if(coordinator){
        /* Same live role/boot/epoch only: a fresh authenticated SERVICE is also
         * current source evidence. Never replace a newer withdrawal/REPORT with
         * a delayed sample, nor renew membership or authority from its arrival. */
        if(r->sequence<=m->source_sequence)return refuse(s,V12_DC_REF_SOURCE,ESTALE);
        source_evidence(s,m,received);m->source_sequence=r->sequence;
      }
      s->service_relative=received.relative_ready;
      /* This authenticated SERVICE carries a new sample and its own original
       * expiry. The preceding view establishes scope/epoch eligibility above;
       * its delivery expiry must not shorten the new sample's lifetime. That
       * one-message lag also shortened the echoed ACK and caused periodic
       * service gaps even on a lossless link. Preserve the current role cap
       * and conservative translated expiry; never renew from arrival time. */
      service_until=earlier(role_until,received.valid_until_us);
      s->service_received_until_us=service_until;s->service_decision=decision;
      s->service_source_epoch=r->source_epoch;count(&s->service_receipts);
      /* The existing view owns the exact received service sample on either
       * recipient role. A later REPORT is control evidence, not this receipt. */
      s->view=received;s->view_root_boot=root_boot;s->view_decision=decision;
      /* Forward newly received root service at the next owner tick. A second
       * free-running timer can phase-lock behind renewal and repeatedly expire
       * the only usable sample before followers receive it. */
      if(coordinator)s->next_service_us=now;
      /* This ACK proves only this recipient's actual validated receipt. */
      rc=queue(s,V12_DC_SERVICE_ACK,r->sender,root,root_boot,decision,&received,0);
      if(rc)return rc;
      struct v12_control_record *ack=&s->outgoing[(s->queue_head+s->queue_count-1u)%V12_CONTROL_QUEUE_LIMIT].record;
      ack->challenge=r->sequence; /* Kind9 domain: original SERVICE sequence. */
      ack->permit_until_us=r->evidence_us; /* Kind9 domain: raw original evidence echo, no permission. */
      ack->source_second=r->source_second;ack->evidence_us=now;
      ack->expires_us=earlier(ack->expires_us,t->expires_lower_us);
    }else{
      if(r->flags||!live(s,m)||(!coordinator&&root!=s->self_wire)||
         (coordinator&&root!=s->self_wire&&(r->sender==root||now>=s->service_received_until_us)))
        return refuse(s,V12_DC_REF_AUTHORITY,EACCES);
      struct v12_dc_service_tuple *sent=NULL;
      for(unsigned n=0;n<4;n++){struct v12_dc_service_tuple *x=&m->service[n];
        if(x->issued&&r->challenge==x->sequence&&r->decision==x->decision&&
           r->root==x->root&&r->root_boot==x->root_boot&&r->source_epoch==x->epoch&&
           r->source_second==x->second&&r->permit_until_us==x->evidence_us&&
           now<x->expires_us&&t->expires_lower_us<=x->expires_us){sent=x;break;}}
      if(!sent)return refuse(s,V12_DC_REF_SOURCE,ESTALE);
      service_until=earlier(service_until,sent->expires_us);
      sent->issued=false; /* Exact receipt retires its outstanding tuple. */
      if(root==s->self_wire){s->service_acked_until_us=service_until;
        s->service_peer=r->sender;s->service_peer_boot=m->boot;
        s->service_decision=r->decision;s->service_source_epoch=r->source_epoch;}
      /* A routed follower ACK is counted as that follower's receipt; it never
       * turns this coordinator into the remote source's serving GM. */
      count(&s->service_acks);
    }
  }else if (r->kind==V12_DC_REPORT) {
    if (coordinator) {
      if ((r->flags&V12_DC_FLAG_CONTROL_VIEW) || r->root!=r->sender || r->root_boot!=m->boot) {
        return refuse(s,V12_DC_REF_SCOPE,EACCES);
      }
      if(r->sequence<=m->source_sequence)return refuse(s,V12_DC_REF_SOURCE,ESTALE);
      m->source_sequence=r->sequence;
      if(r->sender==s->active_root&&r->source_epoch!=s->service_source_epoch)
        s->service_received_until_us=0;
      source_evidence(s,m,reported(s,r,t)); m->last_report_us=now; m->live_until_us=until;
    } else {
      if (!(r->flags&V12_DC_FLAG_CONTROL_VIEW) || r->decision<s->view_decision ||
          (r->root!=V12_DC_NONE && (!r->root_boot || !r->decision)) ||
          (r->root==V12_DC_NONE && ((r->flags&V12_DC_FLAG_COARSE_VALID) || r->root_boot))) {
        return refuse(s,V12_DC_REF_DECISION,EACCES);
      }
      m->last_report_us=now; m->live_until_us=until;
      bool same_service=now<s->service_received_until_us&&r->root==s->active_root&&
        r->root_boot==s->view_root_boot&&r->decision>=s->service_decision&&(r->root!=s->self_wire||s->service_decision>s->retired_decision)&&
        r->source_epoch==s->service_source_epoch;
      struct v12_dc_coarse control_view=reported(s,r,t);
      if(!same_service){s->view=control_view;s->service_received_until_us=0;}
      s->view_root_boot=r->root_boot;
      /* REPORT admission uses its original 250ms delivery TTL; the scoped
       * control role uses the same creation-based 500ms lease as COMMIT.
       * Source/service freshness remains separately bounded by each accepted
       * sample. Capping this role by an older REPORT's delivery expiry made
       * a fresh SERVICE_ACK expire before the next lossless renewal. */
      s->view_until_us=until; s->view_decision=r->decision;
      /* A REPORT view cannot activate the receiving GM. */
      if (s->active_root!=s->self_wire) { s->active_root=r->root; }
    }
  } else if (!coordinator && r->kind==V12_DC_PREPARE) {
    if (r->flags || r->root!=s->self_wire || r->root_boot!=s->boot || !r->decision ||
        r->decision<=s->retired_decision || r->decision<s->pending_decision ||
        r->source_epoch!=(coarse_live(s,&s->local)?s->local.generation:0)) {
      return refuse(s,V12_DC_REF_DECISION,EACCES);
    }
    s->pending_root=s->self_wire; s->pending_root_boot=s->boot; s->pending_decision=r->decision;
    s->pending_source_epoch=r->source_epoch;
    /* Admission TTL is not the reservation lifetime. This uncommitted local
     * reservation uses creation + lease, capped by fresh local source; it
     * cannot grant a role, and the coordinator still owns its shorter timeout. */
    s->pending_until_us=r->source_epoch?earlier(until,s->local.valid_until_us):until;
    if (s->active_root!=s->self_wire) { s->phase=V12_DC_PREPARING; }
    count(&s->prepares);
    rc=queue(s,V12_DC_READY,s->coordinator_wire,s->self_wire,s->boot,r->decision,&s->local,0);
    if (rc) { count(&s->accepted); return rc; }
  } else if (coordinator && r->kind==V12_DC_READY) {
    if (r->flags || r->root!=r->sender || r->root_boot!=m->boot ||
        r->source_epoch!=source_epoch(s,m)) { return refuse(s,V12_DC_REF_DECISION,EACCES); }
    if (r->sender==s->standby && r->decision==s->standby_decision && r->decision>s->retired_decision) {
      s->standby_ready_until_us=until; count(&s->ready_acks);
    } else if (s->phase==V12_DC_PREPARING && r->sender==s->pending_root &&
        r->root_boot==s->pending_root_boot && r->decision==s->pending_decision &&
        r->source_epoch==s->pending_source_epoch && now<s->pending_until_us && live(s,m)) {
      rc=queue(s,V12_DC_COMMIT,m->wire,m->wire,m->boot,r->decision,&m->coarse,0);
      if (rc) { return rc; }
      s->phase=V12_DC_COMMITTING; s->pending_until_us=now+V12_CONTROL_ORIGINAL_TTL_US;
      s->fence_until_us=now+V12_DC_LEASE_US; count(&s->ready_acks); count(&s->commits);
    } else { return refuse(s,V12_DC_REF_DECISION,EACCES); }
  } else if (!coordinator && r->kind==V12_DC_COMMIT) {
    if (r->flags || r->root!=s->self_wire || r->root_boot!=s->boot ||
        r->decision<=s->retired_decision || r->decision!=s->pending_decision ||
        s->pending_root!=s->self_wire || now>=s->pending_until_us ||
        r->source_epoch!=s->pending_source_epoch ||
        r->source_epoch!=(coarse_live(s,&s->local)?s->local.generation:0)) {
      return refuse(s,V12_DC_REF_DECISION,EACCES);
    }
    s->active_root=s->self_wire; s->active_root_boot=s->boot; s->active_decision=r->decision;
    s->active_source_epoch=r->source_epoch;
    /* Record TTL gates admission above. The accepted role keeps its original
     * creation-based 500ms lease, capped by the independent live local source.
     * Reusing the REPORT/COMMIT delivery TTL as a role lease caused gaps before
     * the next renewal under real link latency. No deadline is based on arrival. */
    s->active_until_us=r->source_epoch?earlier(until,s->local.valid_until_us):until;
    s->pending_root=V12_DC_NONE; s->pending_until_us=0; s->phase=V12_DC_RUNNING;
    count(&s->commits);
    rc=queue(s,V12_DC_ACTIVE,s->coordinator_wire,s->self_wire,s->boot,r->decision,&s->local,0);
    if (rc) { count(&s->accepted); return rc; }
    /* FIFO ACTIVE precedes the first service under this exact new decision.
     * Publication never waits a full cadence after a renewed role. */
    if(coarse_live(s,&s->local))
      (void)queue(s,V12_DC_SERVICE,s->coordinator_wire,s->self_wire,s->boot,r->decision,&s->local,V12_DC_FLAG_COARSE_VALID);
    s->next_service_us=now+V12_DC_REPORT_US;
  } else if (coordinator && r->kind==V12_DC_ACTIVE) {
    if (r->flags || s->phase!=V12_DC_COMMITTING || r->sender!=s->pending_root ||
        r->root!=r->sender || r->root_boot!=s->pending_root_boot ||
        r->decision!=s->pending_decision || r->source_epoch!=s->pending_source_epoch ||
        now>=s->pending_until_us || now>=s->fence_until_us || !live(s,m)) {
      return refuse(s,V12_DC_REF_DECISION,EACCES);
    }
    if (s->active_root!=r->root) { count(&s->elections); }
    s->active_root=r->root; s->active_root_boot=r->root_boot; s->active_decision=r->decision;
    s->active_source_epoch=r->source_epoch; s->active_until_us=s->fence_until_us;
    s->pending_root=V12_DC_NONE; s->pending_until_us=0; s->phase=V12_DC_RUNNING;
    /* Renew relative to the original COMMIT creation, not its returning ACTIVE
     * receipt. Otherwise each round-trip consumes the safety margin of the
     * unchanged500ms lease and repeatedly tears down a healthy precision train. */
    s->next_renew_us=s->fence_until_us-V12_DC_LEASE_US+V12_DC_RENEW_US; count(&s->active_acks);
    s->next_report_us=now;s->next_service_us=now+V12_DC_REPORT_US/2;
  } else if (!coordinator && r->kind==V12_DC_STOP) {
    if (r->flags || r->root!=s->self_wire || r->root_boot!=s->boot ||
        r->decision<s->retired_decision || r->decision<s->active_decision ||
        r->decision<s->pending_decision) { return refuse(s,V12_DC_REF_DECISION,EACCES); }
    retire(s,r->decision); s->active_root=s->pending_root=V12_DC_NONE;
    s->active_until_us=s->pending_until_us=0; s->phase=V12_DC_IDLE;
    count(&s->stops);
    rc=queue(s,V12_DC_STOPPED,s->coordinator_wire,s->self_wire,s->boot,r->decision,NULL,0);
    if (rc) { count(&s->accepted); return rc; }
  } else if (coordinator && r->kind==V12_DC_STOPPED) {
    if (r->flags || s->phase!=V12_DC_STOPPING || r->root!=r->sender ||
        r->sender!=s->pending_root || r->root_boot!=s->pending_root_boot ||
        r->decision!=s->pending_decision) { return refuse(s,V12_DC_REF_DECISION,EACCES); }
    s->fence_until_us=s->pending_until_us=0; s->pending_root=V12_DC_NONE;
    s->phase=V12_DC_IDLE; s->uncertain=false; count(&s->stopped_acks);
  } else { return refuse(s,V12_DC_REF_SCOPE,EACCES); }
  count(&s->accepted); s->last_refusal=V12_DC_REF_NONE;
  return 0;
}
int v12_dc_tick(struct v12_direct_control_state *s, uint64_t now)
{
  int rc=clock_now(s,now); if (rc) { return rc; }
  if (!s->running) { return -ESHUTDOWN; }
  if (s->local.available && !coarse_live(s,&s->local)) {
    s->local.available=false; s->members[0].coarse.available=false;
  }
  fence_local_source(s);
  bool coordinator=s->self_wire==s->coordinator_wire;
  if (s->active_root!=V12_DC_NONE && s->active_until_us && now>=s->active_until_us) {
    if (!coordinator) { retire(s,s->active_decision); }
    s->active_root=V12_DC_NONE; s->active_until_us=0; count(&s->lease_expired);
  }
  if (s->pending_root!=V12_DC_NONE && now>=s->pending_until_us) {
    retire(s,s->pending_decision);
    if (coordinator && s->phase==V12_DC_COMMITTING && now<s->fence_until_us) {
      s->uncertain=true; s->phase=V12_DC_UNCERTAIN; s->pending_until_us=s->fence_until_us;
    } else {
      s->pending_root=V12_DC_NONE; s->pending_until_us=0;
      s->phase=s->active_root==V12_DC_NONE?V12_DC_IDLE:V12_DC_RUNNING;
    }
  }
  if (s->uncertain && now>=s->fence_until_us) { s->uncertain=false; }
  if (!coordinator) {
    const struct v12_dc_member *cm=cmember(s,s->coordinator_wire);
    if (!live(s,cm)) {
      retire(s,s->active_decision); retire(s,s->pending_decision); clear_role(s);
    } else if (now>=s->view_until_us && s->active_root!=s->self_wire) {
      s->active_root=V12_DC_NONE; s->view.available=false;
    }
  } else {
    uint8_t candidate=best(s,V12_DC_NONE);
    const struct v12_dc_member *active=cmember(s,s->active_root);
    /* Healthy incumbents survive a better-ranked source arriving later. */
    if(live(s,active)&&coarse_live(s,&active->coarse)&&active->boot==s->active_root_boot&&
       s->active_source_epoch==source_epoch(s,active))candidate=s->active_root;
    if (s->active_root!=V12_DC_NONE && (!live(s,active) || s->active_root_boot!=active->boot ||
        s->active_root!=candidate || s->active_source_epoch!=source_epoch(s,active))) {
      stop_active(s);
    }
    if (s->pending_root!=V12_DC_NONE && s->phase!=V12_DC_STOPPING && !s->uncertain) {
      const struct v12_dc_member *p=cmember(s,s->pending_root);
      if (!live(s,p) || p->boot!=s->pending_root_boot || s->pending_root!=candidate ||
          source_epoch(s,p)!=s->pending_source_epoch) {
        retire(s,s->pending_decision); s->pending_root=V12_DC_NONE; s->pending_until_us=0;
        s->phase=s->active_root==V12_DC_NONE?V12_DC_IDLE:V12_DC_RUNNING;
      }
    }
    if (!s->uncertain && s->pending_root==V12_DC_NONE && candidate!=V12_DC_NONE &&
        ((s->active_root==candidate && now>=s->next_renew_us) ||
         (s->active_root==V12_DC_NONE && now>=s->fence_until_us))) {
      (void)begin(s,candidate);
    }
  }
  if (now>=s->next_report_us) {
    s->next_report_us=now+V12_DC_REPORT_US;
    if (coordinator) {
      const struct v12_dc_member *root=cmember(s,s->active_root);
      for (unsigned i=1;i<V12_CONTROL_MAX_GMS;++i) {
        const struct v12_dc_member *m=&s->members[i];
        if (!m->occupied || !m->connected) { continue; }
        (void)queue(s,V12_DC_REPORT,m->wire,s->active_root,
          s->active_root==V12_DC_NONE?0:s->active_root_boot,
          s->active_root==V12_DC_NONE?s->decision:s->active_decision,
          root?&root->coarse:NULL,V12_DC_FLAG_CONTROL_VIEW);
      }
    } else {
      const struct v12_dc_member *cm=cmember(s,s->coordinator_wire);
      if (cm && cm->connected) {
        (void)queue(s,V12_DC_REPORT,s->coordinator_wire,s->self_wire,s->boot,0,&s->local,0);
      }
    }
  }
  if(now>=s->next_service_us){
    s->next_service_us=now+V12_DC_REPORT_US;
    const struct v12_dc_member *root=cmember(s,s->active_root);
    if(s->active_root==s->self_wire&&now<s->active_until_us&&coarse_live(s,&s->local)){
      if(coordinator){for(unsigned i=1;i<V12_CONTROL_MAX_GMS;i++){
        const struct v12_dc_member *p=&s->members[i];if(live(s,p))
          (void)queue(s,V12_DC_SERVICE,p->wire,s->self_wire,s->boot,s->active_decision,&s->local,
            V12_DC_FLAG_COARSE_VALID|V12_DC_FLAG_CONTROL_VIEW);
      }}else{const struct v12_dc_member *p=cmember(s,s->coordinator_wire);if(live(s,p))
        (void)queue(s,V12_DC_SERVICE,p->wire,s->self_wire,s->boot,s->active_decision,&s->local,V12_DC_FLAG_COARSE_VALID);}
    }else if(coordinator&&root&&live(s,root)&&coarse_live(s,&root->coarse)&&coarse_live(s,&s->view)&&
       now<s->active_until_us&&now<s->service_received_until_us&&s->service_decision==s->active_decision&&
       s->service_source_epoch==root->coarse.generation&&s->view_root_boot==root->boot&&
       s->view_decision==s->active_decision&&s->view.generation==s->service_source_epoch){
      for(unsigned i=1;i<V12_CONTROL_MAX_GMS;i++){const struct v12_dc_member *p=&s->members[i];
        if(live(s,p)&&p->wire!=root->wire&&!queue(s,V12_DC_SERVICE,p->wire,root->wire,root->boot,s->active_decision,&s->view,
            V12_DC_FLAG_COARSE_VALID|V12_DC_FLAG_CONTROL_VIEW)){
          struct v12_control_record *r=&s->outgoing[(s->queue_head+s->queue_count-1u)%V12_CONTROL_QUEUE_LIMIT].record;
          r->expires_us=earlier(r->expires_us,s->service_received_until_us);
        }
      }
    }
  }
  return 0;
}
static bool applicable(const struct v12_direct_control_state *s, const struct v12_control_record *r)
{
  if (r->kind==V12_DC_STOP || r->kind==V12_DC_STOPPED) { return true; }
  if (!s->running) { return false; }
  if(r->kind==V12_DC_SERVICE||r->kind==V12_DC_SERVICE_ACK){
    bool coordinator=s->self_wire==s->coordinator_wire;
    uint64_t decision=coordinator||s->active_root==s->self_wire?s->active_decision:s->view_decision;
    const struct v12_dc_coarse *c=s->active_root==s->self_wire?&s->local:
      coordinator&&cmember(s,s->active_root)?&cmember(s,s->active_root)->coarse:&s->view;
    return s->active_root!=V12_DC_NONE&&r->root==s->active_root&&(r->kind==V12_DC_SERVICE_ACK?
      (r->decision<=decision&&((!coordinator&&s->active_root!=s->self_wire)||r->decision>s->retired_decision)):r->decision==decision)&&
      coarse_live(s,c)&&r->source_epoch==c->generation;
  }
  if(r->source_epoch){
    const struct v12_dc_member *root=cmember(s,r->root);
    if(!root||!coarse_live(s,&root->coarse)||r->source_epoch!=root->coarse.generation)return false;
  }
  if (r->kind==V12_DC_REPORT) { return true; }
  if (r->decision<=s->retired_decision) { return false; }
  if (r->kind==V12_DC_PREPARE) { return
    (r->target==s->pending_root && r->decision==s->pending_decision) ||
    (r->target==s->standby && r->decision==s->standby_decision); }
  if (r->kind==V12_DC_COMMIT) { return s->phase==V12_DC_COMMITTING &&
    r->target==s->pending_root && r->decision==s->pending_decision; }
  if (r->kind==V12_DC_READY) { return s->pending_root==s->self_wire && r->decision==s->pending_decision; }
  if (r->kind==V12_DC_ACTIVE) { return s->active_root==s->self_wire && r->decision==s->active_decision; }
  return false;
}
static void discard_at(struct v12_direct_control_state *s,unsigned offset)
{
  for(unsigned i=offset;i+1<s->queue_count;i++)
    s->outgoing[(s->queue_head+i)%V12_CONTROL_QUEUE_LIMIT]=
      s->outgoing[(s->queue_head+i+1)%V12_CONTROL_QUEUE_LIMIT];
  --s->queue_count;
}
static bool select_outgoing(struct v12_direct_control_state *s,uint64_t now,
                            uint8_t mask,bool ready,bool pop,struct v12_dc_outgoing *out)
{
  if(!out||clock_now(s,now))return false;
  for(unsigned i=0;i<s->queue_count;){
    struct v12_dc_outgoing o=s->outgoing[(s->queue_head+i)%V12_CONTROL_QUEUE_LIMIT];
    const struct v12_dc_member *m=cmember(s,o.recipient);
    if(now>=o.record.expires_us||!m||!m->connected||m->boot!=o.recipient_boot||
       (!m->authenticated&&o.record.kind!=V12_DC_REPORT)||!applicable(s,&o.record)){
      discard_at(s,i);count(&s->queue_expired);continue;
    }
    if((!ready&&mask==255)||(o.recipient<8&&(mask&(1u<<o.recipient)))){
      if(ready&&o.record.kind==V12_DC_SERVICE&&!v12_dc_service_has_slot(s,&o.record,now)){
        mask&=~(1u<<o.recipient); /* preserve this recipient's sequence order */
      }else{*out=o;if(pop)discard_at(s,i);return true;}
    }
    ++i;
  }
  return false;
}
bool v12_dc_peek(struct v12_direct_control_state *s,uint64_t now,struct v12_dc_outgoing *out)
{return select_outgoing(s,now,255,false,false,out);}
bool v12_dc_pop(struct v12_direct_control_state *s,uint64_t now,struct v12_dc_outgoing *out)
{return select_outgoing(s,now,255,false,true,out);}
bool v12_dc_peek_ready(struct v12_direct_control_state *s,uint64_t now,uint8_t mask,struct v12_dc_outgoing *out)
{return select_outgoing(s,now,mask,true,false,out);}
bool v12_dc_pop_recipient(struct v12_direct_control_state *s,uint64_t now,uint8_t peer,struct v12_dc_outgoing *out)
{return peer<8&&select_outgoing(s,now,1u<<peer,false,true,out);}
bool v12_dc_outgoing_live(const struct v12_direct_control_state *s,
                          const struct v12_control_record *r,uint64_t now)
{
  if(!s||!r||now<s->now_us||now<r->created_us||now>=r->expires_us||!applicable(s,r))return false;
  const struct v12_dc_member *m=cmember(s,r->target);
  if(!m||!m->connected||m->boot==0||(!m->authenticated&&r->kind!=V12_DC_REPORT))return false;
  if(r->source_epoch){const struct v12_dc_member *root=cmember(s,r->root);
    /* A follower's coordinator REPORT/SERVICE populates its authenticated
     * view, not the coordinator member's source report. Use the same scoped
     * source as input/applicable; an empty member cannot veto a live ACK, and
     * a populated member cannot rescue an expired follower view. */
    const struct v12_dc_coarse *c=(r->kind==V12_DC_SERVICE||r->kind==V12_DC_SERVICE_ACK)?
      (s->active_root==s->self_wire?&s->local:
       s->self_wire==s->coordinator_wire&&root?&root->coarse:&s->view):
      (root?&root->coarse:&s->view);
    if(now>=c->valid_until_us)return false;}
  if(r->kind==V12_DC_SERVICE||r->kind==V12_DC_SERVICE_ACK){
    uint64_t role=s->self_wire==s->coordinator_wire||s->active_root==s->self_wire?s->active_until_us:s->view_until_us;
    if(now>=role)return false;
    if(r->kind==V12_DC_SERVICE&&s->self_wire==s->coordinator_wire&&s->active_root!=s->self_wire&&
      (now>=s->service_received_until_us||r->decision!=s->service_decision||
       r->root_boot!=s->view_root_boot||r->source_epoch!=s->service_source_epoch||
       r->expires_us>s->service_received_until_us))return false;
  }
  return true;
}
bool v12_dc_service_has_slot(const struct v12_direct_control_state *s,
  const struct v12_control_record *r,uint64_t now)
{
  if(!s||!r||r->kind!=V12_DC_SERVICE||!v12_dc_outgoing_live(s,r,now))return false;
  const struct v12_dc_member *m=cmember(s,r->target);if(!live(s,m))return false;
  for(unsigned n=0;n<4;n++)if(!m->service[n].issued||now>=m->service[n].expires_us||
    m->service[n].decision<=s->retired_decision)return true;
  return false;
}
int v12_dc_service_submitted(struct v12_direct_control_state *s,
  const struct v12_control_record *r,uint64_t now)
{
  if(!s||!r||r->kind!=V12_DC_SERVICE||!v12_dc_outgoing_live(s,r,now))return -ESTALE;
  struct v12_dc_member *m=member(s,r->target);if(!live(s,m)||now>=m->live_until_us)return -ENOTCONN;
  struct v12_dc_service_tuple *slot=NULL;
  for(unsigned n=0;n<4;n++)if(!m->service[n].issued||now>=m->service[n].expires_us||
    m->service[n].decision<=s->retired_decision){slot=&m->service[n];break;}
  if(!slot)return -ENOBUFS;
  *slot=(struct v12_dc_service_tuple){.sequence=r->sequence,.decision=r->decision,
    .second=r->source_second,.evidence_us=r->evidence_us,.expires_us=r->expires_us,
    .root_boot=r->root_boot,.epoch=r->source_epoch,.root=r->root,.issued=true};
  return 0;
}
void v12_dc_snapshot(const struct v12_direct_control_state *s, struct v12_dc_snapshot *o)
{
  if (!s || !o) { return; } memset(o,0,sizeof(*o));
  o->self_wire=s->self_wire; o->coordinator_wire=s->coordinator_wire;
  o->coordinator=s->self_wire==s->coordinator_wire; o->running=s->running; o->uncertain=s->uncertain;
  o->elected_root=V12_DC_NONE; o->hot_standby=V12_DC_NONE;
  o->last_now_us=s->now_us; o->config_epoch=s->config_epoch;
  o->phase=s->phase; o->last_refusal=s->last_refusal; o->queued=s->queue_count;
  o->retired_decision=s->retired_decision;
  const struct v12_dc_member *cm=cmember(s,s->coordinator_wire);
  o->connected=cm && cm->connected; o->authenticated=cm && cm->authenticated;
  for (unsigned i=0;i<V12_CONTROL_MAX_GMS;++i) { if (live(s,&s->members[i])) { ++o->members; } }
  if (s->running) {
    if (o->coordinator && s->active_root!=V12_DC_NONE && s->now_us<s->active_until_us) {
      o->elected_root=s->active_root; o->root_boot=s->active_root_boot;
      o->decision=s->active_decision; o->role_until_us=s->active_until_us;
      const struct v12_dc_member *m=cmember(s,s->active_root);
      if (m && coarse_live(s,&m->coarse)) { o->coarse=m->coarse; }
      if(s->active_root!=s->self_wire&&s->now_us<s->service_received_until_us&&
         s->service_decision==s->active_decision&&s->view_root_boot==s->active_root_boot&&
         m&&m->coarse.generation==s->service_source_epoch&&
         s->view.generation==s->service_source_epoch&&coarse_live(s,&s->view))o->coarse=s->view;
    } else if (!o->coordinator && live(s,cm)) {
      if (s->active_root==s->self_wire && s->now_us<s->active_until_us) {
        o->elected_root=s->self_wire; o->root_boot=s->boot; o->decision=s->active_decision;
        o->role_until_us=s->active_until_us;
        if (coarse_live(s,&s->local)) { o->coarse=s->local; }
      } else if (s->active_root!=V12_DC_NONE && s->active_root!=s->self_wire && s->now_us<s->view_until_us) {
        o->elected_root=s->active_root; o->root_boot=s->view_root_boot;
        o->decision=s->view_decision; o->role_until_us=s->view_until_us;
        if (coarse_live(s,&s->view)) { o->coarse=s->view; }
        o->coarse_control_view=true;
      }
      /* A queued local READY is PREPARED, not proof the coordinator received
       * it. Operational hot standby is exposed only by the coordinator's
       * authenticated READY receipt below. */
    }
    if (o->coordinator && s->standby!=V12_DC_NONE && s->now_us<s->standby_ready_until_us &&
        live(s,cmember(s,s->standby))) { o->hot_standby=s->standby; }
    if (o->elected_root==s->self_wire) { o->role=V12_DC_CONTROL_GM; }
    else if (o->hot_standby==s->self_wire) { o->role=V12_DC_STANDBY; }
    else if (o->elected_root!=V12_DC_NONE) { o->role=V12_DC_FOLLOWER; }
  }
  o->view_until_us=s->view_until_us;
  o->service_peer=s->service_peer;
  o->service_receipts=s->service_receipts;o->service_acks=s->service_acks;
  o->functional_service=o->elected_root!=V12_DC_NONE&&coarse_live(s,&o->coarse)&&
    s->service_decision<=o->decision&&(o->elected_root!=s->self_wire||s->service_decision>s->retired_decision)&&s->service_source_epoch==o->coarse.generation&&
    s->now_us<s->service_received_until_us;
  const struct v12_dc_member *recipient=cmember(s,s->service_peer);
  o->local_gm_ready=o->elected_root==s->self_wire&&coarse_live(s,&o->coarse)&&
    s->active_source_epoch==o->coarse.generation&&s->now_us<o->role_until_us;
  o->serving=o->elected_root==s->self_wire&&coarse_live(s,&o->coarse)&&
    s->service_decision<=o->decision&&s->service_decision>s->retired_decision&&s->service_source_epoch==o->coarse.generation&&
    s->now_us<s->service_acked_until_us&&live(s,recipient)&&recipient->boot==s->service_peer_boot;
  o->service_until_us=o->serving?s->service_acked_until_us:o->functional_service?s->service_received_until_us:0;
  o->relative_precision_permitted=(s->config_epoch==2||s->config_epoch==3)&&o->coarse.relative_ready&&
    (o->local_gm_ready||(o->functional_service&&s->service_relative));
#define COPY(f) o->f=s->f
  COPY(accepted); COPY(refused); COPY(queue_drops); COPY(queue_expired);
  COPY(prepares); COPY(ready_acks); COPY(commits); COPY(active_acks);
  COPY(stops); COPY(stopped_acks); COPY(elections); COPY(lease_expired);
#undef COPY
}
int v12_dc_stop(struct v12_direct_control_state *s, uint64_t now)
{
  int rc=clock_now(s,now); if (rc) { return rc; }
  bool coordinator=s->self_wire==s->coordinator_wire;
  uint8_t root=s->active_root; uint64_t d=s->active_decision;
  s->queue_count=0; s->queue_head=0;
  if (coordinator && root!=V12_DC_NONE && root!=s->self_wire) {
    (void)queue(s,V12_DC_STOP,root,root,s->active_root_boot,d,NULL,0); count(&s->stops);
  } else if (!coordinator && root==s->self_wire) {
    (void)queue(s,V12_DC_STOPPED,s->coordinator_wire,s->self_wire,s->boot,d,NULL,0);
  }
  retire(s,s->active_decision); retire(s,s->pending_decision);
  clear_role(s); s->running=false; s->local.available=false;
  return 0;
}

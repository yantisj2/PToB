/* SPDX-License-Identifier: Apache-2.0 */
#include "direct_control_state.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

/* Public host fixture key only; never linked into firmware. Real codec bodies
 * are compiled separately and every delivered control record crosses110B wire. */
static const uint8_t fixture_key[16]={1,3,5,7,9,11,13,15,2,4,6,8,10,12,14,16};
static unsigned checks, wire_deliveries;
#define CHECK(x) do { ++checks; assert(x); } while (0)
struct network { struct v12_direct_control_state nodes[3]; uint8_t coordinator;
  bool drop_active,drop_service,drop_service_ACK; uint64_t now;
  struct v12_control_record held_ACK; };
static const uint8_t ids[3]={1,2,4};
static int node(uint8_t id) { for (int i=0;i<3;++i) { if(ids[i]==id) return i; } return -1; }
static struct v12_dc_times mapped(const struct v12_control_record *r)
{
  return (struct v12_dc_times){.created_lower_us=r->created_us,.created_upper_us=r->created_us,
    .evidence_lower_us=r->evidence_us,.evidence_upper_us=r->evidence_us,
    .expires_lower_us=r->expires_us,.permit_lower_us=r->kind==V12_DC_SERVICE_ACK?0:r->permit_until_us};
}
static void setup(struct network *n,uint8_t coordinator,uint8_t source)
{
  memset(n,0,sizeof(*n)); n->coordinator=coordinator; n->now=10000;
  for(int i=0;i<3;++i) {
    CHECK(!v12_dc_init(&n->nodes[i],ids[i],1000+ids[i],coordinator,1));
    CHECK(!v12_dc_identity(&n->nodes[i],ids[i],ids[i]*100));
    if(ids[i]==source) {
      struct v12_dc_coarse c={.available=true,.qualified=false,.generation=9,
        .utc_second=1700000000,.evidence_us=n->now,.valid_until_us=n->now+2000000};
      CHECK(!v12_dc_source(&n->nodes[i],&c,n->now));
    }
  }
  for(int i=0;i<3;++i) { for(int j=0;j<3;++j) {
    if(i==j || (ids[i]!=coordinator && ids[j]!=coordinator)) continue;
    CHECK(!v12_dc_set_member(&n->nodes[i],ids[j],1000+ids[j],true,false,false,n->now));
    CHECK(!v12_dc_identity(&n->nodes[i],ids[j],ids[j]*100));
    CHECK(!v12_dc_set_member(&n->nodes[i],ids[j],1000+ids[j],true,true,false,n->now));
  }}
}
static void pump(struct network *n)
{
  unsigned rounds=0;
  for (;;) {
    unsigned emitted=0;
    for(int i=0;i<3;++i) {
      struct v12_dc_outgoing o;
      while(v12_dc_peek(&n->nodes[i],n->now,&o)) {
        if(o.record.kind==V12_DC_SERVICE&&!v12_dc_service_has_slot(&n->nodes[i],&o.record,n->now))break;
        CHECK(v12_dc_pop(&n->nodes[i],n->now,&o));
        ++emitted;
        CHECK(o.record.kind==V12_DC_SERVICE_ACK||o.record.permit_until_us==0);
        CHECK(o.record.expires_us-o.record.created_us<=V12_CONTROL_ORIGINAL_TTL_US);
        if(n->drop_active && o.record.kind==V12_DC_ACTIVE) continue;
        if(n->drop_service_ACK && o.record.kind==V12_DC_SERVICE_ACK){if(o.record.target==1)n->held_ACK=o.record;continue;}
        if(n->drop_service && o.record.kind==V12_DC_SERVICE) continue;
        int target=node(o.recipient); CHECK(target>=0);
        if(o.record.kind!=V12_DC_SERVICE_ACK)o.record.challenge=17;
        if(o.record.kind==V12_DC_SERVICE)CHECK(!v12_dc_service_submitted(&n->nodes[i],&o.record,n->now));
        uint8_t bytes[V12_CONTROL_BYTES]; struct v12_control_record decoded;
        CHECK(!v12_control_encode(bytes,&o.record,fixture_key));
        CHECK(!v12_control_decode(&decoded,bytes,sizeof(bytes),fixture_key));
        /* Native performs this only after complete MAC/boot/link/time checks. */
        CHECK(!v12_dc_set_member(&n->nodes[target],ids[i],1000+ids[i],true,true,true,n->now));
        struct v12_dc_times t=mapped(&decoded);
        int rc=v12_dc_input(&n->nodes[target],&decoded,&t,n->now);
        if(n->drop_active&&decoded.kind==V12_DC_SERVICE){
          CHECK(rc==-ESTALE);struct v12_dc_snapshot refused_view;v12_dc_snapshot(&n->nodes[target],&refused_view);
          CHECK(!refused_view.serving);continue;
        }
        if(rc)fprintf(stderr,"delivery refusal kind=%u sender=%u target=%u root=%u decision=%llu rc=%d now=%llu active=%u active_d=%llu view_d=%llu source=%u phase=%u\n",decoded.kind,decoded.sender,decoded.target,decoded.root,(unsigned long long)decoded.decision,rc,(unsigned long long)n->now,n->nodes[target].active_root,(unsigned long long)n->nodes[target].active_decision,(unsigned long long)n->nodes[target].view_decision,decoded.source_epoch,n->nodes[target].phase);
        CHECK(!rc); ++wire_deliveries;
      }
    }
    if(!emitted) break;
    CHECK(++rounds<32);
  }
}
static void advance(struct network *n,uint64_t now)
{
  n->now=now;
  for(int i=0;i<3;++i) CHECK(!v12_dc_tick(&n->nodes[i],now));
  pump(n);
}
static void role_rotation(void)
{
  for(int ci=0;ci<3;++ci) { for(int si=0;si<3;++si) {
    struct network n; setup(&n,ids[ci],ids[si]);
    for(uint64_t t=n.now;t<1000000;t+=10000) advance(&n,t);
    struct v12_dc_snapshot c; v12_dc_snapshot(&n.nodes[ci],&c);
    CHECK(c.elected_root==ids[si] && c.members==3);
    CHECK(c.active_acks && c.ready_acks && c.coarse.available && !c.coarse.qualified);
    CHECK(c.hot_standby==V12_DC_NONE); /* Only the selected source is eligible. */
    unsigned roots=0;
    for(int i=0;i<3;++i) {
      struct v12_dc_snapshot s; v12_dc_snapshot(&n.nodes[i],&s);
      CHECK(!s.precision_permitted);
      CHECK(s.elected_root==ids[si]);
      if(s.role==V12_DC_CONTROL_GM) ++roots;
      if(ids[i]==c.hot_standby) CHECK(s.role==V12_DC_STANDBY);
    }
    CHECK(roots==1);
  }}
}
static void bootstrap_and_cap(void)
{
  struct v12_direct_control_state s; CHECK(!v12_dc_init(&s,4,104,4,1));
  for(uint8_t i=1;i<=3;++i) CHECK(!v12_dc_set_member(&s,i,100+i,true,true,false,10000));
  CHECK(v12_dc_set_member(&s,5,105,true,true,true,10000)==-ENOSPC);
  CHECK(v12_dc_set_member(&s,6,106,false,true,true,10000)==-EACCES);
  CHECK(!v12_dc_tick(&s,10000));
  struct v12_dc_outgoing o; unsigned bootstrap=0;
  while(v12_dc_pop(&s,10000,&o)) { CHECK(o.record.kind==V12_DC_REPORT); ++bootstrap; }
  CHECK(bootstrap==3);
  struct v12_dc_snapshot a; v12_dc_snapshot(&s,&a); CHECK(a.members==1);
  CHECK(!v12_dc_set_member(&s,1,101,true,true,true,10000));
  CHECK(!v12_dc_identity(&s,1,1));
  CHECK(!v12_dc_tick(&s,20000));
  CHECK(!v12_dc_set_member(&s,1,101,true,true,true,520000));
  CHECK(!v12_dc_tick(&s,520000));
  v12_dc_snapshot(&s,&a); CHECK(a.members==1); /* notification is not heartbeat */
}
static void ack_loss_and_fence(void)
{
  struct network n; setup(&n,4,1); n.drop_active=true;
  for(uint64_t t=n.now;t<300000;t+=10000) advance(&n,t);
  struct v12_direct_control_state *c=&n.nodes[node(4)];
  CHECK(c->uncertain && c->fence_until_us>n.now);
  uint64_t decision=c->decision, fence=c->fence_until_us;
  for(uint64_t t=n.now+10000;t<fence;t+=10000) {
    advance(&n,t); CHECK(c->decision==decision); CHECK(c->uncertain);
  }
  n.drop_active=false;
  for(uint64_t t=fence;t<fence+200000;t+=10000) advance(&n,t);
  struct v12_dc_snapshot s; v12_dc_snapshot(c,&s);
  CHECK(s.elected_root==1 && !s.uncertain && s.decision>decision);
}
static struct v12_control_record peer_report(void)
{
  return (struct v12_control_record){.kind=V12_DC_REPORT,.sender=1,.target=4,.root=1,
    .boot=101,.coordinator_boot=104,.challenge=9,.sequence=1,.root_boot=101,
    .config_epoch=1,.created_us=10000,.evidence_us=10000,.expires_us=260000};
}
static void scope_and_original_time(void)
{
  struct v12_direct_control_state s; CHECK(!v12_dc_init(&s,4,104,4,1));
  CHECK(!v12_dc_set_member(&s,1,101,true,true,true,10000));
  struct v12_control_record r=peer_report(); struct v12_dc_times t=mapped(&r);
  CHECK(!v12_dc_input(&s,&r,&t,10000));
  r.boot=102; CHECK(v12_dc_input(&s,&r,&t,10000)==-EACCES); r.boot=101;
  r.config_epoch=2; CHECK(v12_dc_input(&s,&r,&t,10000)==-EACCES); r.config_epoch=1;
  r.permit_until_us=500000; CHECK(v12_dc_input(&s,&r,&t,10000)==-EACCES); r.permit_until_us=0;
  r.flags=V12_DC_FLAG_CONTROL_VIEW; CHECK(v12_dc_input(&s,&r,&t,10000)==-EACCES); r.flags=0;
  t.created_upper_us=10001; CHECK(v12_dc_input(&s,&r,&t,10000)==-ETIME); t=mapped(&r);
  CHECK(v12_dc_input(&s,&r,&t,260000)==-ETIME);
  struct v12_dc_coarse c={.available=true,.qualified=true,.generation=1,
    .utc_second=1,.evidence_us=260000,.valid_until_us=1000000};
  CHECK(v12_dc_source(&s,&c,260000)==-EACCES);
  CHECK(v12_dc_tick(&s,259999)==-ERANGE);
  struct v12_dc_snapshot snap; v12_dc_snapshot(&s,&snap); CHECK(!snap.running && snap.role==V12_DC_CLIENT);
}
static void stale_commit_and_withdrawal(void)
{
  struct network n; setup(&n,4,1);
  for(uint64_t t=n.now;t<150000;t+=10000) advance(&n,t);
  struct v12_direct_control_state *root=&n.nodes[node(1)];
  uint64_t d=root->active_decision; CHECK(d!=0);
  struct v12_control_record r={.kind=V12_DC_STOP,.sender=4,.target=1,.root=1,
    .boot=1004,.coordinator_boot=1004,.challenge=17,.sequence=999,.decision=d,
    .root_boot=1001,.config_epoch=1,.created_us=n.now,.evidence_us=n.now,.expires_us=n.now+250000};
  struct v12_dc_times t=mapped(&r); CHECK(!v12_dc_input(root,&r,&t,n.now));
  r.kind=V12_DC_COMMIT; r.source_epoch=9; CHECK(v12_dc_input(root,&r,&t,n.now)==-EACCES);
  struct v12_dc_snapshot s; v12_dc_snapshot(root,&s); CHECK(s.role!=V12_DC_CONTROL_GM);
  CHECK(!v12_dc_set_member(root,4,1004,true,false,false,n.now));
  v12_dc_snapshot(root,&s); CHECK(s.role==V12_DC_CLIENT && s.elected_root==V12_DC_NONE);
  r.kind=V12_DC_REPORT; r.flags=V12_DC_FLAG_CONTROL_VIEW;
  CHECK(v12_dc_input(root,&r,&t,n.now)==-EACCES);
}
static void queue_stop_and_overflow(void)
{
  struct v12_direct_control_state s; CHECK(!v12_dc_init(&s,1,101,4,1));
  CHECK(!v12_dc_set_member(&s,4,104,true,true,true,10000));
  for(uint64_t now=10000;now<310000;now+=10000) CHECK(!v12_dc_tick(&s,now));
  CHECK(s.queue_count<=8);
  struct v12_dc_outgoing out; unsigned popped=0;
  while(v12_dc_pop(&s,310000,&out)) { CHECK(out.record.expires_us>310000); ++popped; }
  CHECK(popped && s.queue_expired);
  CHECK(!v12_dc_stop(&s,310000));
  struct v12_dc_snapshot snap; v12_dc_snapshot(&s,&snap); CHECK(!snap.running && !snap.precision_permitted);
  CHECK(v12_dc_tick(&s,320000)==-ESHUTDOWN);
  CHECK(v12_dc_set_member(&s,4,104,true,true,true,320000)==-ESHUTDOWN);
  CHECK(!v12_dc_init(&s,1,101,4,1));
  CHECK(!v12_dc_set_member(&s,4,104,true,true,true,1));
  s.sequence=UINT64_MAX; CHECK(!v12_dc_tick(&s,1));
  CHECK(s.queue_count==0 && s.last_refusal==V12_DC_REF_DECISION);
  CHECK(v12_dc_tick(&s,UINT64_MAX)==-ERANGE);
}
static void source_handoff_and_coordinator_loss(void)
{
  struct network n; setup(&n,4,1);
  for(uint64_t t=n.now;t<150000;t+=10000) advance(&n,t);
  struct v12_dc_coarse none={0}; CHECK(!v12_dc_source(&n.nodes[node(1)],&none,n.now));
  struct v12_dc_coarse fresh={.available=true,.generation=12,.utc_second=1700000001,
    .evidence_us=n.now,.valid_until_us=n.now+2000000};
  CHECK(!v12_dc_source(&n.nodes[node(2)],&fresh,n.now));
  for(uint64_t t=n.now+10000;t<850000;t+=10000) {
    advance(&n,t); unsigned roots=0;
    for(int i=0;i<3;++i) { struct v12_dc_snapshot s; v12_dc_snapshot(&n.nodes[i],&s);
      if(s.role==V12_DC_CONTROL_GM) ++roots; CHECK(!s.precision_permitted); }
    CHECK(roots<=1);
  }
  struct v12_dc_snapshot c; v12_dc_snapshot(&n.nodes[node(4)],&c);
  CHECK(c.elected_root==2 && c.stopped_acks && c.coarse.generation==12);
  CHECK(!v12_dc_set_member(&n.nodes[node(2)],4,1004,true,false,false,n.now));
  v12_dc_snapshot(&n.nodes[node(2)],&c);
  CHECK(c.elected_root==V12_DC_NONE && c.role==V12_DC_CLIENT);
  /* Coordinator choice is a new bounded session, not an in-place mutation. */
  CHECK(!v12_dc_stop(&n.nodes[node(4)],n.now));
  setup(&n,2,2);
  for(uint64_t t=n.now;t<250000;t+=10000) advance(&n,t);
  v12_dc_snapshot(&n.nodes[node(2)],&c);
  CHECK(c.coordinator && c.elected_root==2 && c.members==3);
}
static void recipient_boot_and_stable_tie(void)
{
  struct v12_direct_control_state s; CHECK(!v12_dc_init(&s,4,104,4,1));
  CHECK(!v12_dc_set_member(&s,1,101,true,true,true,10000));
  CHECK(!v12_dc_identity(&s,4,1000)); CHECK(!v12_dc_identity(&s,1,2000));
  struct v12_dc_coarse local={.available=true,.generation=1,.utc_second=1700000000,.evidence_us=10000,.valid_until_us=1000000};
  CHECK(!v12_dc_source(&s,&local,10000));
  CHECK(!v12_dc_tick(&s,10000));
  struct v12_dc_snapshot a; v12_dc_snapshot(&s,&a); CHECK(a.elected_root==4);
  CHECK(!v12_dc_set_member(&s,1,102,true,true,true,10000));
  struct v12_dc_outgoing o; CHECK(!v12_dc_pop(&s,10000,&o)); CHECK(s.queue_expired);
  struct v12_control_record report=peer_report();report.boot=report.root_boot=102;report.flags=V12_DC_FLAG_COARSE_VALID;
  report.source_epoch=2;report.source_second=1700000000;report.created_us=report.evidence_us=20000;report.expires_us=270000;
  struct v12_dc_times times=mapped(&report);CHECK(!v12_dc_input(&s,&report,&times,20000));
  /* Lower trusted identity takes the next election, regardless of wire index. */
  CHECK(!v12_dc_identity(&s,1,50)); CHECK(!v12_dc_tick(&s,20000));
  v12_dc_snapshot(&s,&a);CHECK(a.elected_root==4); /* healthy incumbent sticks */
  local.available=false;CHECK(!v12_dc_source(&s,&local,20000));CHECK(!v12_dc_tick(&s,20000));
  CHECK(v12_dc_pop(&s,20000,&o)); CHECK(o.record.kind==V12_DC_PREPARE && o.recipient==1);
  CHECK(o.recipient_boot==102 && o.record.root_boot==102);
}
static void saturated_ack_queue(void)
{
  struct v12_direct_control_state s; CHECK(!v12_dc_init(&s,1,101,4,1));
  CHECK(!v12_dc_set_member(&s,4,104,true,true,true,10000));
  struct v12_control_record r={.kind=V12_DC_PREPARE,.sender=4,.target=1,.root=1,
    .boot=104,.coordinator_boot=104,.challenge=17,.sequence=1,.decision=1,
    .root_boot=101,.config_epoch=1,.created_us=10000,.evidence_us=10000,.expires_us=260000};
  struct v12_dc_times t=mapped(&r);
  for(unsigned i=1;i<=12;++i) { r.decision=r.sequence=i;
    CHECK(v12_dc_input(&s,&r,&t,10000)==(i<=8?0:-ENOBUFS)); }
  CHECK(s.queue_count==8 && s.queue_drops==4 && s.prepares==12);
  r.kind=V12_DC_COMMIT; r.sequence=13;
  CHECK(v12_dc_input(&s,&r,&t,10000)==-ENOBUFS);
  CHECK(s.commits==1 && s.active_decision==12 && s.active_until_us==510000);
  struct v12_dc_snapshot snap; v12_dc_snapshot(&s,&snap);
  CHECK(snap.role==V12_DC_CONTROL_GM && !snap.precision_permitted);
  r.kind=V12_DC_STOP; r.sequence=14;
  CHECK(v12_dc_input(&s,&r,&t,10000)==-ENOBUFS);
  v12_dc_snapshot(&s,&snap); CHECK(snap.role!=V12_DC_CONTROL_GM && s.stops==1);
  CHECK(!v12_dc_stop(&s,10000)); CHECK(s.queue_count==0 && !s.running);
}
static void source_epoch_queue_fence(void)
{
  struct v12_direct_control_state s;CHECK(!v12_dc_init(&s,1,101,4,1));
  CHECK(!v12_dc_set_member(&s,4,104,true,true,true,10000));
  struct v12_dc_coarse c={.available=true,.generation=9,.utc_second=1700000000,
    .evidence_us=10000,.valid_until_us=110000};CHECK(!v12_dc_source(&s,&c,10000));
  struct v12_control_record r={.kind=V12_DC_PREPARE,.sender=4,.target=1,.root=1,
    .boot=104,.coordinator_boot=104,.challenge=17,.sequence=1,.decision=1,
    .source_epoch=9,.root_boot=101,.config_epoch=1,.created_us=10000,
    .evidence_us=10000,.expires_us=260000};
  struct v12_dc_times t=mapped(&r);CHECK(!v12_dc_input(&s,&r,&t,10000));
  CHECK(s.pending_until_us<=c.valid_until_us);
  struct v12_dc_outgoing out;CHECK(v12_dc_pop(&s,10000,&out));
  CHECK(out.record.kind==V12_DC_READY&&out.record.expires_us==out.record.created_us+V12_CONTROL_ORIGINAL_TTL_US);
  CHECK(v12_dc_outgoing_live(&s,&out.record,10000));
  CHECK(!v12_dc_outgoing_live(&s,&out.record,110000));
  struct v12_control_record queued_ready=out.record;
  r.kind=V12_DC_COMMIT;CHECK(!v12_dc_input(&s,&r,&t,10000));
  CHECK(s.active_until_us==c.valid_until_us);
  struct v12_dc_snapshot view;v12_dc_snapshot(&s,&view);CHECK(view.role==V12_DC_CONTROL_GM);
  struct v12_dc_coarse none={0};CHECK(!v12_dc_source(&s,&none,20000));
  v12_dc_snapshot(&s,&view);CHECK(view.role!=V12_DC_CONTROL_GM&&s.retired_decision==1);
  CHECK(!v12_dc_outgoing_live(&s,&queued_ready,20000));
  CHECK(!v12_dc_pop(&s,20000,&out)); /* queued ACTIVE from withdrawn epoch */
  c.generation=11;c.evidence_us=30000;c.valid_until_us=500000;
  CHECK(!v12_dc_source(&s,&c,30000));
  CHECK(!v12_dc_outgoing_live(&s,&queued_ready,30000));
  r.created_us=r.evidence_us=30000;r.expires_us=280000;t=mapped(&r);
  CHECK(v12_dc_input(&s,&r,&t,30000)==-EACCES);
  r.kind=V12_DC_PREPARE;r.source_epoch=11;CHECK(v12_dc_input(&s,&r,&t,30000)==-EACCES);
  r.decision=2;CHECK(!v12_dc_input(&s,&r,&t,30000));
  r.kind=V12_DC_COMMIT;CHECK(!v12_dc_input(&s,&r,&t,30000));
  v12_dc_snapshot(&s,&view);CHECK(view.role==V12_DC_CONTROL_GM&&view.coarse.generation==11);
  /* Expiry with no fresh adapter submission still fences local serving state. */
  CHECK(!v12_dc_tick(&s,500000));v12_dc_snapshot(&s,&view);CHECK(view.role!=V12_DC_CONTROL_GM);
}
static void source_report_cannot_activate_self(void)
{
  struct v12_direct_control_state s;CHECK(!v12_dc_init(&s,1,101,4,1));
  CHECK(!v12_dc_set_member(&s,4,104,true,true,true,10000));
  struct v12_control_record r={.kind=V12_DC_REPORT,.sender=4,.target=1,.root=1,
    .boot=104,.coordinator_boot=104,.challenge=17,.sequence=1,.decision=1,
    .root_boot=101,.config_epoch=1,.created_us=10000,.evidence_us=10000,
    .expires_us=110000,.flags=V12_DC_FLAG_CONTROL_VIEW|V12_DC_FLAG_COARSE_VALID,
    .source_epoch=9,.source_second=1700000000};
  struct v12_dc_times t=mapped(&r);CHECK(!v12_dc_input(&s,&r,&t,10000));
  struct v12_dc_snapshot view;v12_dc_snapshot(&s,&view);
  CHECK(view.role==V12_DC_CLIENT&&view.elected_root==V12_DC_NONE); /* No COMMIT. */
  CHECK(s.view.valid_until_us==r.expires_us&&s.view_until_us==r.created_us+V12_DC_LEASE_US);
  CHECK(!v12_dc_tick(&s,r.expires_us));v12_dc_snapshot(&s,&view);
  CHECK(!view.functional_service&&!view.serving&&!view.coarse.available);
  CHECK(!v12_dc_init(&s,4,104,4,1));CHECK(!v12_dc_set_member(&s,1,101,true,true,true,10000));
  r.sender=1;r.boot=101;r.target=4;r.flags=V12_DC_FLAG_COARSE_VALID;r.decision=0;t=mapped(&r);
  CHECK(!v12_dc_input(&s,&r,&t,10000));CHECK(s.members[1].coarse.valid_until_us==r.expires_us);
  CHECK(!v12_dc_tick(&s,110000));CHECK(!s.members[1].coarse.available||s.members[1].coarse.valid_until_us<=s.now_us);
}
static void functional_service_receipts(void)
{
 struct v12_direct_control_state isolated;CHECK(!v12_dc_init(&isolated,4,1004,4,1));
 CHECK(!v12_dc_tick(&isolated,10000));struct v12_dc_snapshot view;v12_dc_snapshot(&isolated,&view);
 CHECK(view.elected_root==V12_DC_NONE&&!view.serving);
 struct v12_dc_coarse source={.available=true,.generation=9,.utc_second=1700000000,.evidence_us=10000,.valid_until_us=2000000};
 CHECK(!v12_dc_source(&isolated,&source,10000));CHECK(!v12_dc_tick(&isolated,20000));v12_dc_snapshot(&isolated,&view);
 CHECK(view.elected_root==4&&view.active_acks&&!view.serving&&!view.service_acks); /* self ACK != recipient */
 struct network n;setup(&n,4,1);n.drop_service_ACK=true;
 struct v12_dc_coarse backup=source;backup.generation=12;
 CHECK(!v12_dc_source(&n.nodes[node(2)],&backup,n.now));
 for(uint64_t t=n.now;t<400000;t+=10000)advance(&n,t);
 struct v12_dc_snapshot gm,coord,client;
 v12_dc_snapshot(&n.nodes[node(1)],&gm);v12_dc_snapshot(&n.nodes[node(4)],&coord);
 CHECK(gm.elected_root==1&&!gm.serving&&!gm.service_acks&&coord.service_receipts);
 CHECK(coord.hot_standby==2&&!coord.serving); /* actual authenticated READY from eligible backup */
 n.drop_service_ACK=false;for(uint64_t t=400000;t<650000;t+=10000)advance(&n,t);
 v12_dc_snapshot(&n.nodes[node(1)],&gm);v12_dc_snapshot(&n.nodes[node(4)],&coord);v12_dc_snapshot(&n.nodes[node(2)],&client);
 CHECK(gm.serving&&gm.service_acks&&gm.service_peer==4&&gm.service_until_us>n.now);
 CHECK(coord.functional_service&&coord.service_receipts&&!coord.serving&&coord.service_acks);
 CHECK(client.functional_service&&client.coarse.generation==9&&client.root_boot==1001&&!client.precision_permitted);
 CHECK(client.coarse.evidence_us==10000); /* routed origin age never refreshed */
 n.drop_service_ACK=true;for(uint64_t t=650000;t<820000;t+=10000)advance(&n,t);
 struct v12_direct_control_state *root=&n.nodes[node(1)];
 const struct v12_dc_member *recipient=NULL;
 for(unsigned i=0;i<4;i++)if(root->members[i].wire==4)recipient=&root->members[i];
 CHECK(recipient);const struct v12_dc_service_tuple *tuple=NULL;
 for(unsigned i=0;i<4;i++)if(recipient->service[i].issued&&recipient->service[i].expires_us>n.now&&
   recipient->service[i].decision>root->retired_decision)tuple=&recipient->service[i];
 CHECK(tuple);
 struct v12_control_record ack={.kind=V12_DC_SERVICE_ACK,.sender=4,.target=1,.boot=1004,.coordinator_boot=1004,
   .challenge=tuple->sequence,.sequence=900,.root=1,.root_boot=1001,.decision=tuple->decision,.source_epoch=9,
   .source_second=tuple->second,.permit_until_us=tuple->evidence_us,.created_us=n.now,.evidence_us=n.now,
   .expires_us=tuple->expires_us,.config_epoch=1};
 struct v12_dc_times map=mapped(&ack);CHECK(!v12_dc_input(root,&ack,&map,n.now));
 for(unsigned bad=0;bad<10;bad++){
   struct v12_control_record r=ack;if(bad==0)r.boot++;if(bad==1)r.root_boot++;if(bad==2)r.decision++;if(bad==3)r.source_epoch++;
   if(bad==4)r.permit_until_us=1;if(bad==5)r.expires_us=n.now;
   if(bad==6)r.source_second++;if(bad==7)r.challenge++;if(bad==8)r.expires_us++;
   if(bad==9){r.created_us=r.evidence_us=n.now;r.source_second=1;r.permit_until_us=1;}
   struct v12_dc_times times=mapped(&r);CHECK(v12_dc_input(root,&r,&times,n.now)<0);
 }
 CHECK(!v12_dc_set_member(&n.nodes[node(4)],2,1002,true,false,false,n.now));
 v12_dc_snapshot(&n.nodes[node(4)],&coord);CHECK(coord.elected_root==1&&coord.service_receipts);
 CHECK(!v12_dc_set_member(root,4,1004,true,false,false,n.now));v12_dc_snapshot(root,&gm);
 CHECK(!gm.serving&&gm.elected_root==V12_DC_NONE); /* synchronous coordinator withdrawal */
}
static void delayed_service_ACK_and_unsubmitted(void)
{
 struct network n;setup(&n,4,1);n.drop_service_ACK=true;
 for(uint64_t t=n.now;t<350000;t+=10000){advance(&n,t);
   if(n.held_ACK.kind==V12_DC_SERVICE_ACK&&n.held_ACK.created_us==n.now&&
     n.held_ACK.expires_us>=n.now+180000)break;
 }
 struct v12_direct_control_state *root=&n.nodes[node(1)];
 struct v12_dc_member *peer=NULL;for(unsigned i=0;i<4;i++)if(root->members[i].wire==4)peer=&root->members[i];
 CHECK(peer);struct v12_dc_service_tuple saved={0};
 for(unsigned i=0;i<4;i++)if(peer->service[i].issued&&peer->service[i].expires_us>saved.expires_us)saved=peer->service[i];
 CHECK(saved.issued&&saved.expires_us>=n.now+180000);
 struct v12_control_record ack=n.held_ACK;
 CHECK(ack.kind==V12_DC_SERVICE_ACK&&ack.challenge==saved.sequence&&ack.created_us==n.now);
 uint64_t received=n.now,delivery=received+180000;
 for(uint64_t t=n.now+10000;t<=delivery;t+=10000)advance(&n,t);
 bool newer=false,retained=false;for(unsigned i=0;i<4;i++){
   newer|=peer->service[i].issued&&peer->service[i].sequence>saved.sequence;
   retained|=peer->service[i].issued&&peer->service[i].sequence==saved.sequence;
 }
 CHECK(newer&&retained);struct v12_dc_snapshot s;v12_dc_snapshot(root,&s);CHECK(!s.serving);
 CHECK(ack.created_us==received&&ack.evidence_us==received);
 uint8_t bytes[V12_CONTROL_BYTES];CHECK(!v12_control_encode(bytes,&ack,fixture_key));
 CHECK(!v12_control_decode(&ack,bytes,sizeof(bytes),fixture_key));
 struct v12_dc_times m=mapped(&ack);CHECK(!v12_dc_input(root,&ack,&m,n.now));
 v12_dc_snapshot(root,&s);CHECK(s.serving&&s.service_until_us==saved.expires_us);
 /* A fresh ACK creation cannot refresh the original SERVICE deadline. */
 ack.created_us=ack.evidence_us=saved.expires_us;ack.expires_us=saved.expires_us+100000;
 m=mapped(&ack);CHECK(v12_dc_input(root,&ack,&m,saved.expires_us)<0);
 setup(&n,4,1);n.drop_service=true;
 for(uint64_t t=n.now;t<300000;t+=10000)advance(&n,t);
 root=&n.nodes[node(1)];v12_dc_snapshot(root,&s);CHECK(s.elected_root==1&&!s.serving);
 ack=(struct v12_control_record){.kind=V12_DC_SERVICE_ACK,.sender=4,.target=1,.boot=1004,.coordinator_boot=1004,
   .challenge=1,.sequence=900,.root=1,.root_boot=1001,.decision=root->active_decision,.source_epoch=9,
   .source_second=1700000000,.permit_until_us=10000,.created_us=n.now,.evidence_us=n.now,
   .expires_us=n.now+100000,.config_epoch=1};
 m=mapped(&ack);CHECK(v12_dc_input(root,&ack,&m,n.now)==-ESTALE);v12_dc_snapshot(root,&s);CHECK(!s.serving);
}
static void routed_service_original_tuple(void)
{
 struct v12_direct_control_state s;CHECK(!v12_dc_init(&s,4,1004,4,1));
 CHECK(!v12_dc_set_member(&s,1,1001,true,true,true,10000));
 CHECK(!v12_dc_set_member(&s,2,1002,true,true,true,10000));
 s.members[1].coarse=(struct v12_dc_coarse){.available=true,.generation=9,.utc_second=1700000000,.evidence_us=10000,.valid_until_us=2000000};
 s.active_root=1;s.active_root_boot=1001;s.decision=s.active_decision=1;s.active_source_epoch=9;s.active_until_us=2000000;s.phase=V12_DC_RUNNING;
 struct v12_control_record r={.kind=V12_DC_SERVICE,.sender=1,.target=4,.root=1,.boot=1001,.root_boot=1001,
   .coordinator_boot=1004,.challenge=17,.sequence=100,.decision=1,.source_epoch=9,.source_second=1700000000,
   .created_us=10000,.evidence_us=10000,.expires_us=260000,.config_epoch=1,.flags=V12_DC_FLAG_COARSE_VALID};
 struct v12_dc_times times=mapped(&r);CHECK(!v12_dc_input(&s,&r,&times,10000));
 struct v12_dc_outgoing out;CHECK(v12_dc_pop(&s,10000,&out)&&out.record.kind==V12_DC_SERVICE_ACK);
 r.kind=V12_DC_REPORT;r.sequence++;r.source_second++;r.created_us=r.evidence_us=100000;r.expires_us=350000;
 times=mapped(&r);CHECK(!v12_dc_input(&s,&r,&times,100000));
 CHECK(s.view.utc_second==1700000000&&s.view.evidence_us==10000&&s.service_received_until_us==260000);
 struct v12_dc_snapshot view;v12_dc_snapshot(&s,&view);CHECK(view.functional_service&&view.coarse.utc_second==1700000000);
 s.next_report_us=s.next_renew_us=UINT64_MAX;s.next_service_us=0;
 CHECK(!v12_dc_tick(&s,240000));CHECK(v12_dc_peek(&s,240000,&out)&&out.record.kind==V12_DC_SERVICE&&out.recipient==2);
 CHECK(out.record.source_second==1700000000&&out.record.evidence_us==10000&&out.record.expires_us==260000);
 struct v12_dc_outgoing held;CHECK(v12_dc_peek(&s,250000,&held)&&!memcmp(&out,&held,sizeof(out))); /* Busy FIFO delay never refreshes. */
 CHECK(v12_dc_outgoing_live(&s,&out.record,259999)&&!v12_dc_outgoing_live(&s,&out.record,260000));
 r.sequence++;r.source_epoch=10;r.created_us=r.evidence_us=250001;r.expires_us=500001;
 times=mapped(&r);CHECK(!v12_dc_input(&s,&r,&times,250001));
 CHECK(!s.service_received_until_us&&!v12_dc_outgoing_live(&s,&out.record,250001));
 v12_dc_snapshot(&s,&view);CHECK(!view.functional_service);
 /* A follower's newer control REPORT also cannot replace the sample for
  * which this recipient has an actual SERVICE receipt. */
 struct v12_direct_control_state f;CHECK(!v12_dc_init(&f,2,1002,4,1));
 CHECK(!v12_dc_set_member(&f,4,1004,true,true,true,10000));
 r=(struct v12_control_record){.kind=V12_DC_REPORT,.sender=4,.target=2,.root=1,.boot=1004,.root_boot=1001,
   .coordinator_boot=1004,.challenge=17,.sequence=200,.decision=1,.source_epoch=9,.source_second=1700000000,
   .created_us=10000,.evidence_us=10000,.expires_us=260000,.config_epoch=1,.flags=V12_DC_FLAG_COARSE_VALID|V12_DC_FLAG_CONTROL_VIEW};
 times=mapped(&r);CHECK(!v12_dc_input(&f,&r,&times,10000));r.kind=V12_DC_SERVICE;r.sequence++;
 CHECK(!v12_dc_input(&f,&r,&times,10000));r.kind=V12_DC_REPORT;r.sequence++;
 r.source_second++;r.created_us=r.evidence_us=100000;r.expires_us=350000;times=mapped(&r);
 CHECK(!v12_dc_input(&f,&r,&times,100000));v12_dc_snapshot(&f,&view);
 CHECK(view.functional_service&&view.coarse.utc_second==1700000000&&view.coarse.evidence_us==10000&&view.service_until_us==260000);
 r.sequence++;r.source_epoch=10;r.created_us=r.evidence_us=110000;r.expires_us=360000;times=mapped(&r);
 CHECK(!v12_dc_input(&f,&r,&times,110000));v12_dc_snapshot(&f,&view);CHECK(!view.functional_service&&!f.service_received_until_us);
}
static void follower_service_ACK_native_admission(void)
{
 /* Native dispatch calls outgoing_live after peek/job admission. Portable
  * delivery alone misses the coordinator-as-root member/view distinction. */
 struct v12_direct_control_state f;CHECK(!v12_dc_init(&f,2,1002,4,1));
 CHECK(!v12_dc_set_member(&f,4,1004,true,true,true,10000));
 struct v12_control_record r={.kind=V12_DC_REPORT,.sender=4,.target=2,.root=4,.boot=1004,.root_boot=1004,
   .coordinator_boot=1004,.challenge=17,.sequence=200,.decision=1,.source_epoch=9,.source_second=1700000000,
   .created_us=10000,.evidence_us=10000,.expires_us=260000,.config_epoch=1,
   .flags=V12_DC_FLAG_COARSE_VALID|V12_DC_FLAG_CONTROL_VIEW};
 struct v12_dc_times times=mapped(&r);CHECK(!v12_dc_input(&f,&r,&times,10000));
 r.kind=V12_DC_SERVICE;r.sequence++;CHECK(!v12_dc_input(&f,&r,&times,10000));
 CHECK(!f.members[1].coarse.available&&!f.members[1].coarse.valid_until_us&&f.view.available);
 struct v12_dc_outgoing ack;CHECK(v12_dc_peek(&f,20000,&ack)&&ack.record.kind==V12_DC_SERVICE_ACK);
 CHECK(ack.record.challenge==r.sequence&&ack.record.permit_until_us==r.evidence_us&&
   ack.record.source_second==r.source_second&&ack.record.expires_us==260000);
 CHECK(v12_dc_outgoing_live(&f,&ack.record,20000));
 CHECK(!v12_dc_outgoing_live(&f,&ack.record,260000));
 struct v12_direct_control_state stale=f;
 /* A different member source cannot rescue expired or changed receipt data. */
 stale.members[1].coarse=(struct v12_dc_coarse){.available=true,.generation=9,
   .utc_second=1700000000,.evidence_us=10000,.valid_until_us=2000000};
 stale.view.valid_until_us=20000;CHECK(!v12_dc_outgoing_live(&stale,&ack.record,20000));
 stale=f;stale.view.generation++;CHECK(!v12_dc_outgoing_live(&stale,&ack.record,20000));
 stale=f;stale.view_until_us=20000;CHECK(!v12_dc_outgoing_live(&stale,&ack.record,20000));
 stale=f;CHECK(!v12_dc_set_member(&stale,4,1004,true,false,false,20000));
 CHECK(!v12_dc_outgoing_live(&stale,&ack.record,20000));
 stale=f;CHECK(!v12_dc_set_member(&stale,4,1005,true,true,true,20000));
 CHECK(!v12_dc_outgoing_live(&stale,&ack.record,20000));
 stale=f;CHECK(!v12_dc_stop(&stale,20000));CHECK(!v12_dc_outgoing_live(&stale,&ack.record,20000));
 CHECK(v12_dc_pop(&f,20000,&ack)&&!f.queue_count); /* Original bytes/TTL survive admission. */
}
/* A role renewal is an epoch handshake, not a retransmission of an old
 * REPORT sample. Its deadline remains its own creation+250ms. */
static void handshake_original_deadline(void)
{
 struct v12_direct_control_state s;CHECK(!v12_dc_init(&s,4,104,4,1));
 CHECK(!v12_dc_set_member(&s,1,101,true,true,true,10000));
 struct v12_control_record r=peer_report();r.flags=V12_DC_FLAG_COARSE_VALID;
 r.source_epoch=9;r.source_second=1700000000;
 struct v12_dc_times t=mapped(&r);CHECK(!v12_dc_input(&s,&r,&t,10000));
 CHECK(!v12_dc_tick(&s,240000));CHECK(s.pending_until_us==490000);
 struct v12_dc_outgoing o;CHECK(v12_dc_pop(&s,240000,&o));
 CHECK(o.record.kind==V12_DC_PREPARE&&o.record.expires_us==490000);
 /* A genuinely fresh REPORT keeps the same epoch eligible beyond the first
  * sample's expiry. It does not modify the already-created PREPARE. */
 r.sequence=2;r.created_us=r.evidence_us=250000;r.expires_us=500000;
 t=mapped(&r);CHECK(!v12_dc_input(&s,&r,&t,270000));
 r.kind=V12_DC_READY;r.flags=0;r.sequence=3;r.decision=s.pending_decision;
 r.created_us=r.evidence_us=260000;r.expires_us=510000;
 t=mapped(&r);CHECK(!v12_dc_input(&s,&r,&t,280000));
 CHECK(s.phase==V12_DC_COMMITTING&&s.pending_until_us==530000);
 while(v12_dc_pop(&s,280000,&o))if(o.record.kind==V12_DC_COMMIT)
   CHECK(o.record.expires_us==530000);
}
static void advancing_service_source_evidence(void)
{
 struct network n;setup(&n,4,1);
 for(uint64_t now=n.now;now<=150000;now+=10000)advance(&n,now);
 struct v12_direct_control_state *c=&n.nodes[node(4)];
 struct v12_dc_member *m=NULL;for(unsigned i=0;i<4;i++)if(c->members[i].wire==1)m=&c->members[i];
 CHECK(m&&m->coarse.available&&c->active_root==1);
 uint64_t received=m->coarse.valid_until_us+1;
 CHECK(received<c->active_until_us&&received<m->live_until_us);
 struct v12_control_record r={.kind=V12_DC_SERVICE,.sender=1,.target=4,.root=1,
   .boot=1001,.root_boot=1001,.coordinator_boot=1004,.config_epoch=1,
   .challenge=17,.sequence=1000,.decision=c->active_decision,.source_epoch=9,
   .source_second=1700000000,.flags=V12_DC_FLAG_COARSE_VALID,
   .created_us=received-10000,.evidence_us=received-10000,.expires_us=received+240000};
 struct v12_dc_times t=mapped(&r);CHECK(!v12_dc_input(c,&r,&t,received));
 CHECK(m->coarse.valid_until_us==r.expires_us&&m->source_sequence==1000);
 CHECK(!v12_dc_tick(c,received));CHECK(c->active_root==1&&c->stops==0);
 /* A new SERVICE capped by an expiring role cannot shorten independently
  * retained same-epoch REPORT/source evidence. */
 uint64_t retained=m->coarse.valid_until_us;r.sequence=1001;r.expires_us=received+10000;
 t=mapped(&r);CHECK(!v12_dc_input(c,&r,&t,received));CHECK(m->coarse.valid_until_us==retained);
 r.expires_us=received+240000;t=mapped(&r);
 /* Delayed older REPORT cannot erase the newer sample. A new unavailable
  * REPORT immediately withdraws and no later SERVICE can resurrect it. */
 struct v12_direct_control_state copy=*c;
 r.kind=V12_DC_REPORT;r.decision=0;r.sequence=999;r.flags=0;r.source_epoch=0;
 CHECK(v12_dc_input(c,&r,&t,received)==-ESTALE);
 r.sequence=1002;CHECK(!v12_dc_input(c,&r,&t,received));
 CHECK(!m->coarse.available);CHECK(!v12_dc_tick(c,received));CHECK(c->active_root==V12_DC_NONE);
 r.kind=V12_DC_SERVICE;r.sequence=1003;r.decision=copy.active_decision;
 r.flags=V12_DC_FLAG_COARSE_VALID;r.source_epoch=9;
 CHECK(v12_dc_input(c,&r,&t,received)==-ESTALE);
 r.source_epoch=10;CHECK(v12_dc_input(&copy,&r,&t,received)==-ESTALE);
 r.source_epoch=9;r.created_us=r.evidence_us=copy.active_until_us;
 r.expires_us=r.created_us+250000;t=mapped(&r);
 CHECK(v12_dc_input(&copy,&r,&t,r.created_us)==-ESTALE);
}
static void blocked_recipient_keeps_other_peer_schedulable(void)
{
 struct v12_direct_control_state s;CHECK(!v12_dc_init(&s,4,104,4,1));
 CHECK(!v12_dc_set_member(&s,1,101,true,true,true,10000));
 CHECK(!v12_dc_set_member(&s,2,102,true,true,true,10000));
 CHECK(!v12_dc_tick(&s,10000));CHECK(!v12_dc_tick(&s,110000));
 CHECK(s.queue_count==4);
 struct v12_dc_outgoing o,original=s.outgoing[1];
 CHECK(v12_dc_peek_ready(&s,110000,1u<<2,&o));
 CHECK(o.recipient==2&&!memcmp(&o,&original,sizeof o));
 CHECK(v12_dc_pop_recipient(&s,110000,2,&o));
 CHECK(!memcmp(&o,&original,sizeof o));
 CHECK(v12_dc_peek_ready(&s,110000,1u<<2,&o));CHECK(o.record.sequence>original.record.sequence);
 CHECK(v12_dc_pop_recipient(&s,110000,2,&o));
 CHECK(v12_dc_pop(&s,110000,&o)&&o.recipient==1&&o.record.sequence<original.record.sequence);
 CHECK(v12_dc_pop(&s,110000,&o)&&o.recipient==1);
 CHECK(!s.queue_count);
}
int main(int argc,char **argv)
{
  if(argc==2){
    if(!strcmp(argv[1],"epoch"))source_epoch_queue_fence();
    else if(!strcmp(argv[1],"view"))source_report_cannot_activate_self();
    else return 2;
    printf("PASS targeted_source_case=%s checks=%u\n",argv[1],checks);return 0;
  }
  blocked_recipient_keeps_other_peer_schedulable();
  handshake_original_deadline();advancing_service_source_evidence();
  role_rotation(); bootstrap_and_cap(); ack_loss_and_fence(); scope_and_original_time();
  stale_commit_and_withdrawal(); queue_stop_and_overflow();
  functional_service_receipts();delayed_service_ACK_and_unsubmitted();routed_service_original_tuple();follower_service_ACK_native_admission();source_handoff_and_coordinator_loss(); recipient_boot_and_stable_tie();
  saturated_ack_queue();source_epoch_queue_fence();source_report_cannot_activate_self();
  printf("PASS checks=%u wire_deliveries=%u state_bytes=%zu members=4 queue=8 precision=REFUSED\n",
    checks,wire_deliveries,sizeof(struct v12_direct_control_state));
  return 0;
}

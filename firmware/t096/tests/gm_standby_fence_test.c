/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Targeted hostile-state/replay checks in the actual serialized G5 owner.
 * These are protocol negative controls, separate from physical-tuple simulation.
 */
#include "../protocol/network.c"
#include <assert.h>
#include <stdio.h>
#include "gm_diagnostics.h"
/* Both sides matter: delayed renewal must fund takeover, while deliberate
 * repair closure must not leave a grant valid through silence and takeover. */
_Static_assert(R2_GM5_GRANT_US>2000000+R2_GM5_COORDINATION_US+3000000+R2_GM5_RETIRE_GUARD_US,"grant must fund renewal age, silence and takeover");
_Static_assert(R2_GM5_GRANT_US+R2_GM5_RETIRE_GUARD_US<R2_GM5_REPAIR_US+R2_GM5_COORDINATION_US+3000000,"grant must expire before post-repair false takeover");
static struct r2_network base(bool active)
{
 struct r2_network n;assert(!r2_network_init(&n,0,2,100,10,22,40000,70000000));
 n.group=(struct r2_group_config){.id=1,.epoch=1,.psk={1}};n.capabilities=3;n.gm5.enabled=true;
 n.gm_mask=n.gnss_mask=3;n.admission.eligible=true;n.admission.observing=true;n.admission.source_epoch=1;
 n.candidate=(struct v12_dc_coarse){.available=true,.generation=1,.valid_until_us=n.now+1500000};
 n.peer[1]=(struct r2_net_peer){.identity=200,.boot=20,.connected=true,.authenticated=true,.map_valid=true,.alive=n.now,.seen=n.now,.candidate_until=n.now+1500000,.candidate_epoch=1};
 n.choice=active?0:1;n.root_boot=active?10:20;n.coordinator=active?1:0;n.coordinator_boot=active?20:10;
 n.view=n.candidate;n.view.available=true;n.publication=1;n.ready=true;n.local_gm=active;n.service_until=n.now+1500000;
 n.timing_valid=true;n.timing_boot=n.root_boot;n.timing_epoch=1;n.timing_train=1;n.timing_at=n.now;
 n.gm5.role=active?R2_GM5_ACTIVE:R2_GM5_ACQUIRING;n.gm5.active=active?100:200;n.gm5.active_boot=n.root_boot;n.gm5.active_until=n.now+120000000;
 n.gm5.selected=active?200:0;n.gm5.generation=7;n.management_generation=7;return n;
}
static struct r2_auth_record control(struct r2_network *n,unsigned kind,uint64_t created)
{
 struct r2_auth_record r={.kind=kind,.sender=1,.target=0,.root=1,.boot=20,.root_boot=20,.coordinator_boot=10,.challenge=31,.sequence=1,.config_epoch=R2_AUTH_CONFIG,.created_us=created,.evidence_us=created,.expires_us=created+250000,.permit_until_us=created+R2_GM5_GRANT_US,.source_epoch=1,.source_second=descriptor(n),.decision=8,.flags=1};return r;
}
static struct r2_network field_base(void)
{
 struct r2_network n=base(false);n.peer[1].own_nonce=31;n.peer[1].nonce=41;
 n.peer[1].offset_low=-10000;n.peer[1].offset_high=10000;n.peer[1].map_anchor=n.now-1000000;return n;
}
static int deliver(struct r2_network *n,struct r2_auth_record *r,uint64_t at)
{uint8_t wire[R2_GROUP_CONTROL_MAX];assert(!encode_control(n,wire,r,n->peer[1].rx_key));return r2_network_receive(n,1,wire,r2_control_wire_size(wire),at,at);}
static void clock_field_boundary_tests(void)
{
 const uint64_t created=70000000,grant=created+15000000-10000-32100;
 const uint64_t expiry=created+250000-10000-2600;
 for(int delta=-1;delta<=1;delta++){
  struct r2_network n=field_base();struct r2_auth_record r=control(&n,R2_AUTH_COMMIT,created);
  int rc=deliver(&n,&r,expiry+delta);
  if(delta<0){assert(!rc&&n.gm5.grant_until==grant&&n.management_committed);assert(n.peer[1].response.expires_us==expiry);}
  else assert(rc==-ESTALE&&!n.gm5.grant_until&&!n.management_committed);
 }
 struct r2_network n=field_base();struct r2_auth_record r=control(&n,R2_AUTH_COMMIT,created);
 assert(!deliver(&n,&r,created+200000)&&n.gm5.grant_until==grant);
 assert(deliver(&n,&r,created+200001)==-ESTALE); /* Same sequence cannot replay. */
 r.sequence++;r.created_us+=1000;r.evidence_us+=1000;r.expires_us+=1000;r.permit_until_us+=1000;
 assert(!deliver(&n,&r,created+201000)&&n.gm5.grant_until==grant); /* Same generation cannot extend grant. */
 gm5_grant_clear(&n,R2_GM5_GRANT_EXPIRED);r.sequence++;assert(deliver(&n,&r,created+201001)==-ESTALE&&!n.gm5.grant_until);
 for(unsigned why=0;why<5;why++){
  n=field_base();r=control(&n,R2_AUTH_COMMIT,created);
  if(why==0)r.permit_until_us++; /* Original15s is a hard maximum. */
  if(why==1)r.source_epoch++;
  if(why==2)r.root_boot++;
  if(why==3)r.coordinator_boot++;
  if(why==4)r.source_second++;
  assert(deliver(&n,&r,created+100000)&&!n.gm5.grant_until&&!n.hot_ready);
 }
 /* A short direct-source permit is not replaced by the record TTL. */
 const uint64_t permit=created+100000-10000-2300;
 for(int delta=-1;delta<=1;delta++){
  n=field_base();r=control(&n,R2_AUTH_SERVICE,created);r.coordinator_boot=20;r.decision=n.publication;r.source_second=1791417600;r.permit_until_us=created+100000;
  int rc=deliver(&n,&r,permit+delta);if(delta<0)assert(!rc&&n.peer[1].candidate_until==permit);else assert(rc&&!n.management_committed);
 }
 printf("PASS actual codec/receive original record expiry -1/at/+1=%llu, full15sgrant lowerdeadline=%llu, sourcepermit -1/at/+1=%llu, replay/generation/retired/source/boot/descriptor fences\n",(unsigned long long)expiry,(unsigned long long)grant,(unsigned long long)permit);
 }

static void clock_rate_corner_tests(void)
{
 const uint64_t anchors[]={20000000,70000000,INT64_MAX/4-20000000};
 const int64_t deltas[]={-10000000,-1000000,-250000,-501,-500,-499,-1,0,1,499,500,501,250000,1000000,10000000};
 const uint64_t ages[]={0,1,249999,1000000,9999999};const int64_t offsets[]={-40000,0,40000};const int rates[]={-2000,0,2000};unsigned cases=0;
 for(unsigned a=0;a<3;a++)for(unsigned d=0;d<15;d++)for(unsigned g=0;g<5;g++)for(unsigned o=0;o<3;o++)for(unsigned edge=0;edge<2;edge++)for(unsigned r=0;r<3;r++){
  struct r2_net_peer m={.seen=anchors[a]+1000,.map_anchor=anchors[a],.offset_low=offsets[o],.offset_high=offsets[o]+80000};
  uint64_t t=anchors[a]+deltas[d],now=m.seen+ages[g],guard=map_drift(&m,t,now);
  int64_t at_anchor=edge?m.offset_high:m.offset_low;
  __int128 exact=(__int128)(t+at_anchor)*1000000+(__int128)rates[r]*deltas[d];
  uint64_t lower=mapped(t,m.offset_low-(int64_t)guard),upper=mapped(t,m.offset_high+(int64_t)guard);
  assert((__int128)lower*1000000<=exact&&(__int128)upper*1000000>=exact);cases++;
 }
 /* A 10s grant keeps its complete 11s anchor distance; a 250ms TTL uses its
  * own1.25s distance. Arrival-age floor can only make either more cautious. */
 struct r2_net_peer m={.seen=70000000,.map_anchor=69000000};
 assert(map_drift(&m,80000000,70200000)==22100);
 assert(map_drift(&m,70250000,70200000)==2600);
 assert(map_drift(&m,70000000,79999999)==20099);
 printf("PASS %u rational corner checks: +/-2000ppm, both offset endpoints, past/future timestamps, rounding boundaries, near INT64_MAX/4; per-field lower never exceeds earliest allowed time; upper never precedes latest allowed time; full10sgrant guard unchanged\n",cases);
}

/* Actual serialized owner and wire at100ms accepted opportunities. Initial
 * admission/clock context and ideal captured PT are controlled fixture inputs;
 * this finite delivery burst is not a controller/RF qualification. */
static void delay_burst_tests(void)
{
 struct r2_network pair[2]={base(true),base(false)};struct r2_network *a=&pair[0],*b=&pair[1];
 b->identity=200;b->boot=20;b->peer[1].identity=100;b->peer[1].boot=10;
 b->root_boot=b->gm5.active_boot=b->timing_boot=10;b->gm5.active=100;b->coordinator_boot=20;
 a->peer[1].own_nonce=11;a->peer[1].nonce=22;b->peer[1].own_nonce=22;b->peer[1].nonce=11;
 strcpy(a->name,"Active");strcpy(b->name,"Successor");uint64_t start=a->now;
 for(unsigned k=0;k<2;k++){
  struct r2_network *n=&pair[k];n->admission.boot_at=n->admission.stable_at=0;n->admission.last_expiry=start+1500000;
  n->candidate.utc_second=n->view.utc_second=INT64_C(1791417600);n->candidate.evidence_us=n->view.evidence_us=start;
  n->peer[1].map_anchor=start;n->gm5.attempt_started=start;n->gm5.attempt_until=start+R2_GM5_ACQUIRE_US;
 }
 /* Delay is always below the100ms offer period: one in-flight record per
  * direction suffices, without a second transport or ideal timer algorithm. */
 struct {uint64_t due;size_t size;uint8_t wire[R2_GROUP_CONTROL_MAX];} flight[2]={0};
 uint64_t first_hot=0,withdraw=0,first_ready_rejection=0,max_width=0;unsigned reason=0,post_hot_rejections=0;
 for(uint64_t elapsed=0;elapsed<=45000000;elapsed+=1000){uint64_t now=start+elapsed;
  a->now=b->now=now;
  for(unsigned k=0;k<2;k++)if(flight[k].due&&now>=flight[k].due){
   struct r2_network *n=&pair[1-k];struct r2_auth_record r;char name[25];uint8_t caps;
   assert(!decode_control(n,&r,flight[k].wire,flight[k].size,1,n->peer[1].rx_key,name,&caps,NULL));
   int rc=r2_network_receive(n,1,flight[k].wire,flight[k].size,now,now);
   if(first_hot&&rc){post_hot_rejections++;if(r.kind==R2_AUTH_READY&&!first_ready_rejection)first_ready_rejection=elapsed;}
   uint64_t width=n->peer[1].offset_high-n->peer[1].offset_low;if(width>max_width)max_width=width;
   flight[k].due=0;
  }
  for(unsigned k=0;k<2;k++){
   struct r2_network *n=&pair[k];struct r2_clock source={0};source.view.source_available=true;source.view.source_epoch=1;
   uint64_t second=elapsed/1000000*1000000;source.last_pair_edge=source.last_rmc=start+second;source.last_pair_utc=INT64_C(1791417600000000)+second;
   r2_network_timing(n,n->root_boot,n->view.generation,n->publication,n->local_gm,true,now);
   bool was_hot=n->hot_ready;r2_network_tick(n,&source,now);
   if(was_hot&&!n->hot_ready&&!withdraw){withdraw=elapsed;reason=n->hot_withdrawal_reason;}
  }
  if(!first_hot&&a->hot_ready&&b->hot_ready)first_hot=elapsed;
  for(unsigned k=0;k<2;k++)if(elapsed>=k*50000&&(elapsed-k*50000)%100000==0){
   uint8_t peer,wire[R2_GROUP_CONTROL_MAX];bool hello;
   if(!r2_network_next_ready(&pair[k],2,&peer,wire,&hello))continue;
   assert(peer==1&&!flight[k].due);r2_network_sent(&pair[k],hello,peer,wire);
   flight[k].due=now+(elapsed>=4000000&&elapsed<8000000?54000:25000);
   flight[k].size=r2_control_wire_size(wire);memcpy(flight[k].wire,wire,flight[k].size);
  }
 }
 printf("G5 fixed25/54ms burst4..8s: first_hot=%llu first_ready_rejection=%llu post_hot_rejections=%u withdraw=%llu reason=%u max_map_width=%llu final_hot=%u/%u\n",(unsigned long long)first_hot,(unsigned long long)first_ready_rejection,post_hot_rejections,(unsigned long long)withdraw,reason,(unsigned long long)max_width,a->hot_ready,b->hot_ready);fflush(stdout);
 assert(first_hot&&first_hot<8000000&&!first_ready_rejection&&!post_hot_rejections&&!withdraw);
 assert(max_width<80000&&!a->hello_map_refusals&&!b->hello_map_refusals&&a->hot_ready&&b->hot_ready);
 puts("PASS actual fixed delay burst retains HOT through45s within original source/TTL/map/READY/management/grant bounds");
}

/* New source permission must be offered at the current CONTROL opportunity,
 * before the sparse HELLO cadence. These are actual producer/encoded-wire
 * boundary checks, not a claim of complete RF/PA recovery. */
static void hello_source_refresh_tests(void)
{
 for(unsigned owner=75000;owner<=100000;owner+=25000)for(unsigned phase=0;phase<owner;phase+=25000){
  struct r2_network n=base(true);n.admission.boot_at=0;n.now+=phase;n.peer[1].seen=n.peer[1].alive=n.now;n.peer[1].own_nonce=31;n.peer[1].nonce=41;strcpy(n.name,"Sender");
  n.candidate.valid_until_us=n.now+500000;n.admission.last_expiry=n.candidate.valid_until_us;
  n.peer[1].candidate_until=n.now+1500000;n.gm5.ready_at=n.now;
  n.peer[1].next_hello=n.now+owner;n.peer[1].next_service=n.peer[1].next_management=n.now+1000000;
  struct r2_clock source={0};source.view.source_available=true;source.view.source_epoch=1;
  source.last_pair_edge=source.last_rmc=n.now;source.last_pair_utc=INT64_C(1791417600000000);
  uint64_t at=n.now,service_due=n.peer[1].next_service;r2_network_tick(&n,&source,at);
  assert(n.peer[1].next_service==service_due); /* New PPS cannot overwrite live issued history. */
  uint8_t peer,wire[R2_GROUP_CONTROL_MAX];bool hello=false;
  assert(r2_network_next_ready(&n,2,&peer,wire,&hello)&&peer==1&&hello);
  struct r2_auth_record record;char name[25];uint8_t caps;
  assert(!decode_control(&n,&record,wire,r2_control_wire_size(wire),n.self,n.peer[1].tx_key,name,&caps,NULL));
  assert(record.kind==R2_AUTH_HELLO&&record.flags==1&&record.root==n.self&&record.source_epoch==1&&record.permit_until_us==at+1500000);
  r2_network_sent(&n,hello,peer,wire);uint64_t due=n.peer[1].next_hello;
  r2_network_tick(&n,&source,at+1);assert(n.peer[1].next_hello==due);
  assert(!r2_network_next_ready(&n,2,&peer,wire,&hello)); /* Unchanged evidence is not another send. */
 }
 puts("PASS G5 new original source permit offers authenticated HELLO in the current75/100ms phased opportunity; unchanged evidence preserves sparse cadence");
 for(unsigned fault=0;fault<4;fault++){
  struct r2_network n=base(true);n.admission.boot_at=0;n.admission.last_expiry=n.candidate.valid_until_us;
  n.peer[1].next_hello=n.now+300000;n.peer[1].next_service=n.peer[1].next_management=n.now+1000000;
  n.gm5.ready_at=n.now;n.hot_ready=true;n.management_committed=true;n.management_until=n.now+2000000;
  struct r2_clock source={0};source.view.source_available=true;source.view.source_epoch=1;
  source.last_pair_edge=source.last_rmc=n.now;source.last_pair_utc=INT64_C(1791417600000000);
  if(fault==0)source.last_pair_edge=source.last_rmc=n.now-1500000; /* Actual original permit expired. */
  if(fault==1)source.view.source_epoch=2; /* New epoch must requalify for60s. */
  if(fault==2)n.timing_valid=false; /* Source HELLO cannot fabricate PA/map readiness. */
  if(fault==3)n.gm5.ready_at=n.now-R2_GM5_COORDINATION_US; /* Nor can it renew remote READY. */
  r2_network_tick(&n,&source,n.now);assert(!n.hot_ready&&!n.standby_authorized);
  if(fault<2)assert(!n.admission.eligible&&!n.local_gm&&!n.ready&&n.peer[1].next_hello==n.now+300000);
  else assert(n.local_gm&&n.gm5.attempt_until==n.now+R2_GM5_REPAIR_US);
 }
 for(unsigned inhibited=0;inhibited<2;inhibited++){
  struct r2_network n=base(true);n.admission.boot_at=0;n.admission.last_expiry=n.candidate.valid_until_us;
  n.gm5.role=R2_GM5_COLD;n.gm5.active=200;n.gm5.active_boot=20;n.gm5.selected=0;
  struct r2_clock source={0};source.view.source_available=true;source.view.source_epoch=1;
  source.last_pair_edge=source.last_rmc=n.now;source.last_pair_utc=INT64_C(1791417600000000);
  struct r2_gm_coordination c={.root=200,.boot=20,.epoch=1,.until=n.now+1000000};
  r2_network_tick_coordinated(&n,&source,n.now,inhibited?&c:NULL);
  uint8_t peer,wire[R2_GROUP_CONTROL_MAX];bool hello;
  assert(n.admission.eligible&&!n.ready&&!n.hot_ready&&!n.standby_authorized&&!r2_network_next_ready(&n,2,&peer,wire,&hello));
 }
 puts("PASS true permit expiry/source epoch requalification, independent PT/READY loss, COLD quiet and foreign-root inhibition remain negative gates");
}
/* Controlled PT feedback boundary: established authenticated maps and fresh
 * sources are fixture inputs. Actual PREPARE/READY/COMMIT/acknowledgement wire
 * records drive the controller; this does not prove native PA convergence. */
static void late_map_wire(struct r2_network *from,struct r2_network *to,unsigned counts[16])
{
 uint8_t peer,wire[R2_GROUP_CONTROL_MAX];bool hello;
 if(!r2_network_next_ready(from,2,&peer,wire,&hello))return;
 assert(peer==1&&!hello&&wire[1]<16);counts[wire[1]]++;
 r2_network_sent(from,hello,peer,wire);
 assert(!r2_network_receive(to,1,wire,r2_control_wire_size(wire),to->now,to->now));
}
static void late_map_acquire_tests(void)
{
 for(unsigned mode=0;mode<3;mode++){
  struct r2_network a=base(true),b=base(false);uint64_t start=a.now;
  b.identity=200;b.boot=20;b.peer[1].identity=100;b.peer[1].boot=10;
  b.root_boot=b.gm5.active_boot=b.timing_boot=10;b.gm5.active=100;b.coordinator_boot=20;
  a.peer[1].own_nonce=11;a.peer[1].nonce=22;b.peer[1].own_nonce=22;b.peer[1].nonce=11;
  strcpy(a.name,"Active");strcpy(b.name,"Successor");
  a.gm5.attempt_started=b.gm5.attempt_started=start;
  a.gm5.attempt_until=b.gm5.attempt_until=start+R2_GM5_ACQUIRE_US;
  uint64_t deadline=a.gm5.attempt_until,first_hot=0,issued_at=0;unsigned sent[16]={0};
  for(uint64_t elapsed=0;elapsed<=R2_GM5_ACQUIRE_US;elapsed+=1000){
   uint64_t now=start+elapsed;a.now=b.now=now;
   struct r2_network *pair[2]={&a,&b};
   for(unsigned k=0;k<2;k++){
    struct r2_network *n=pair[k];
    n->candidate.valid_until_us=n->view.valid_until_us=n->service_until=now+1500000;
    n->peer[1].candidate_until=now+1500000;n->peer[1].seen=n->peer[1].alive=now;
    n->peer[1].map_anchor=now;n->peer[1].next_hello=n->peer[1].next_service=UINT64_MAX;
    n->timing_at=now;n->timing_valid=!k||(mode!=1&&elapsed>=25643000);
   }
   uint32_t generation=a.gm5.generation;gm5_tick(&a,true,now,NULL);gm5_tick(&b,true,now,NULL);
   if(a.gm5.generation!=generation){issued_at=now;assert(a.gm5.issued_until-now==R2_GM5_GRANT_US);}
   if(elapsed%10000==0&&a.gm5.selected){late_map_wire(&a,&b,sent);late_map_wire(&b,&a,sent);}
   if(a.gm5.attempt_until)assert(a.gm5.attempt_until==deadline);
   if(a.hot_ready&&b.hot_ready){first_hot=elapsed;break;}
   if(mode!=1&&elapsed>=32000000)break;
  }
  if(mode==1){assert(!first_hot&&!a.gm5.selected&&a.gm5.failures==1);assert(!b.hot_ready&&!b.standby_authorized);continue;}
  assert(first_hot>=25643000+R2_GM5_STABLE_US&&first_hot<32000000);
  assert(sent[R2_AUTH_PREPARE]&&sent[R2_AUTH_READY]&&sent[R2_AUTH_COMMIT]);
  assert(a.management_committed&&!a.gm5.grant_pending&&b.standby_authorized);
  assert(issued_at&&a.gm5.issued_until-issued_at==R2_GM5_GRANT_US);
  assert(b.gm5.grant_until<=a.gm5.issued_until&&b.gm5.grant_until>b.now);
  if(mode==2){a.now++;gm5_tick(&a,false,a.now,NULL);assert(!a.hot_ready&&!a.local_gm&&!a.ready&&!a.gm5.selected);}
 }
 puts("PASS controlled PT map first at25.643s then original5s stability: encoded READY/COMMIT/ACK confirms HOT; unproductive fixed ACQUIRE expires; real source withdrawal and15s original grants");
}
static struct r2_network contact_base(void)
{
 struct r2_network n=base(true);n.gm5.selected=0;
 n.gm5.candidate[0]=(struct r2_gm5_candidate){.identity=200,.boot=20,.eligible=true,.epoch=1,.at=n.now,.until=n.now+120000000,.quality={1,248,254,128,65535,200}};
 return n;
}
static struct r2_join_exchange contact_exchange(const struct r2_network *n,uint64_t peer,uint64_t boot,bool server)
{return(struct r2_join_exchange){.self=n->identity,.peer=peer,.boot=n->boot,.peer_boot=boot,.nonce=31,.remote_nonce=41,.until=n->now+5000000,.accepted=true,.server=server};}
static void contact_tests(void)
{
 struct r2_network n=contact_base();gm5_select(&n);uint64_t start=n.gm5.attempt_started,until=n.gm5.attempt_until;
 assert(n.gm5.contacting&&until==start+R2_GM5_CONTACT_US&&!n.hot_ready&&!n.standby_authorized&&!n.gm5.issued_until);
 assert(!r2_gm5_wants_control(&n,200)&&r2_gm5_control_peer(&n)==200);
 struct r2_auth_record ready={.kind=R2_AUTH_READY,.root=0,.root_boot=10,.coordinator_boot=20,.source_epoch=1,.source_second=descriptor(&n),.decision=7,.flags=5};
 assert(gm5_management_receive(&n,1,&ready,0,n.now+250000));
 n.peer[1].next_hello=n.peer[1].next_service=n.now+1000000;uint8_t peer,wire[R2_GROUP_CONTROL_MAX];bool hello;
 assert(!gm5_next(&n,2,&peer,wire,&hello)); /* CONTACT cannot emit PREPARE/COMMIT. */
 struct r2_join_exchange e=contact_exchange(&n,200,20,true);uint8_t status[R2_GM5_STATUS_BYTES];
 assert(!r2_gm5_status_encode(&n,&e,0,status)&&!r2_group_get(status+92,8)&&n.gm5.contacting);
 struct r2_join_exchange wrong=contact_exchange(&n,300,30,true);assert(!r2_gm5_status_encode(&n,&wrong,1,status)&&n.gm5.contacting);
 wrong=e;wrong.peer_boot++;assert(!r2_gm5_status_encode(&n,&wrong,1,status)&&n.gm5.contacting);
 wrong=e;wrong.until=n.now;assert(r2_gm5_status_encode(&n,&wrong,1,status)&&n.gm5.contacting);
 assert(!r2_gm5_status_encode(&n,&e,2,status)&&n.gm5.contacting);
 n.now=until-1;n.candidate.valid_until_us=n.now+1500000;e=contact_exchange(&n,200,20,true);
 assert(!r2_gm5_status_encode(&n,&e,1,status)&&!n.gm5.contacting&&r2_group_get(status+92,8)==200);
 assert(n.gm5.attempt_started==until-1&&n.gm5.attempt_until==until-1+R2_GM5_ACQUIRE_US);
 uint64_t acquire=n.gm5.attempt_until;for(unsigned i=0;i<3;i++){n.now++;assert(!r2_gm5_status_encode(&n,&e,1,status));assert(n.gm5.attempt_until==acquire);}
 n.now=acquire;gm5_tick(&n,true,n.now,NULL);assert(!n.gm5.selected&&!n.gm5.contacting&&n.gm5.failures==1&&!n.hot_ready);
 puts("PASS fixed CONTACT, no management/STATUS0 appointment, wrong peer/boot/phase/expired exchange fences; late matching reply arms once, lost/repeated phases cannot renew ACQUIRE");
 n=contact_base();n.gm5.candidate[1]=n.gm5.candidate[0];n.gm5.candidate[1].identity=n.gm5.candidate[1].quality.identity=300;n.gm5.candidate[1].boot=30;n.gm5.candidate[1].quality.priority1=2;
 gm5_select(&n);until=n.gm5.attempt_until;assert(n.gm5.selected==200);
 for(unsigned i=0;i<4;i++){n.now+=1000000;wrong=contact_exchange(&n,300,30,true);assert(!r2_gm5_status_encode(&n,&wrong,1,status)&&n.gm5.contacting&&n.gm5.attempt_until==until);}
 n.now=until;gm5_tick(&n,true,n.now,NULL);assert(!n.gm5.selected&&n.gm5.candidate[0].failures==1&&n.gm5.candidate[0].cooldown==until+30000000);
 n.now+=4999999;gm5_select(&n);assert(!n.gm5.selected);n.now++;gm5_select(&n);assert(n.gm5.selected==300&&n.gm5.contacting&&n.gm5.attempt_until==n.now+R2_GM5_CONTACT_US);
 n.candidate.valid_until_us=n.now+1500000;wrong=contact_exchange(&n,300,30,false);assert(!r2_gm5_status_encode(&n,&wrong,1,status)&&n.gm5.contacting);
 assert(!r2_gm5_status_encode(&n,&wrong,2,status)&&!n.gm5.contacting&&n.gm5.attempts==2);
 puts("PASS unreachable higher-ranked nominee fails original30s contact into full-identity cooldown; second healthy candidate starts after original5s spacing and client STATUS2 arms acquisition");
 for(unsigned phase=0;phase<2;phase++)for(unsigned source=0;source<2;source++){
  n=contact_base();gm5_select(&n);e=contact_exchange(&n,200,20,true);
  if(phase)assert(!r2_gm5_status_encode(&n,&e,1,status)&&!n.gm5.contacting);
  if(source){n.peer[1].source_withdrawn=true;n.peer[1].candidate_until=0;n.gm5.candidate[0].eligible=false;}
  gm5_tick(&n,source,n.now,NULL);assert(!n.gm5.selected&&!n.gm5.contacting&&!n.hot_ready&&!n.standby_authorized&&n.gm5.failures==1);
 }
 n=contact_base();n.gm5.retire_until=n.now+R2_GM5_GRANT_US+R2_GM5_RETIRE_GUARD_US;gm5_select(&n);assert(!n.gm5.selected);n.now=n.gm5.retire_until;gm5_select(&n);assert(n.gm5.contacting);
 puts("PASS local/remote source withdrawal in CONTACT and ACQUIRE; original retirement prevents nomination, with no grant or takeover supplied by status");
 n=contact_base();memset(n.gm5.candidate,0,sizeof n.gm5.candidate);gm5_select(&n);assert(!n.gm5.next_attempt&&n.gm5.last_reason==R2_GM5_NO_CANDIDATE);
 n.gm5.next_attempt=n.now+5000000;gm5_select(&n);assert(n.gm5.next_attempt==n.now+5000000); /* Real failed-attempt floor is never discarded. */
 puts("PASS idle NO_CANDIDATE does not invent a retry deadline; actual failed-attempt/takeover spacing remains original");
}
static void phase_evidence(struct r2_network *n,uint64_t at)
{
 n->now=at;n->candidate.valid_until_us=n->view.valid_until_us=n->service_until=at+1500000;
 n->peer[1].candidate_until=at+1500000;n->peer[1].seen=n->peer[1].alive=at;n->timing_at=at;
}
static void lifecycle_boundary_tests(void)
{
 /* READY freshness shares the management lease; source/PT evidence remains
  * independently current. Test strict original boundary without rearming. */
 for(int edge=-1;edge<=1;edge++){
  struct r2_network q=base(true);uint64_t ready=q.now;
  q.gm5.ready_at=ready;q.gm5.stable_since=ready-R2_GM5_STABLE_US;
  q.hot_ready=true;q.gm5.successes=1;q.gm5.issued_until=ready+R2_GM5_GRANT_US;
  phase_evidence(&q,ready+R2_GM5_COORDINATION_US+edge);
  gm5_tick(&q,true,q.now,NULL);
  assert(q.gm5.ready_at==ready);
  assert(q.hot_ready==(edge<0));
  if(edge>=0)assert(q.gm5.attempt_until==q.now+R2_GM5_REPAIR_US);
 }

 struct r2_network n=base(true);n.gm5.attempt_until=n.now+R2_GM5_ACQUIRE_US;
 n.gm5.stable_since=n.now-R2_GM5_STABLE_US+1;n.gm5.ready_at=n.now-R2_GM5_COORDINATION_US;
 gm5_tick(&n,true,n.now,NULL);assert(!n.gm5.stable_since&&!n.gm5.issued_until);
 uint64_t restart=n.now+1;
 for(unsigned i=0;i<=10;i++){
  phase_evidence(&n,restart+i*500000);n.gm5.ready_at=n.now;gm5_tick(&n,true,n.now,NULL);
  if(i<10)assert(!n.gm5.issued_until);else assert(n.gm5.issued_until==n.now+R2_GM5_GRANT_US);
 }
 for(int boundary=-1;boundary<=1;boundary++){
  n=base(true);n.hot_ready=true;n.gm5.successes=1;n.timing_valid=false;
  gm5_tick(&n,true,n.now,NULL);uint64_t until=n.gm5.attempt_until;
  assert(until==n.now+R2_GM5_REPAIR_US&&!n.hot_ready);
  phase_evidence(&n,until-2);n.timing_valid=true;n.gm5.ready_at=n.now;
  gm5_tick(&n,true,n.now,NULL);assert(n.gm5.attempt_until==until);
  phase_evidence(&n,until+boundary);n.gm5.ready_at=n.now;
  gm5_tick(&n,true,n.now,NULL);
  assert((n.gm5.selected!=0)==(boundary<0));assert(!n.hot_ready);
  if(boundary>=0)assert(n.gm5.failures==1&&!n.gm5.attempt_until);
 }
 puts("PASS READY freshness glitch before5s requires full new stable dwell; partial repair cannot renew original10s and expires at -1/at/+1 boundaries");
}
static void coordination_boundary_tests(void)
{
 struct r2_network legacy=base(true);legacy.gm5.enabled=false;
 assert(coordination_lease(&legacy)==2000000);
 for(int edge=-1;edge<=1;edge++){
  struct r2_network n=field_base();struct r2_auth_record r=control(&n,R2_AUTH_COMMIT,n.now);
  assert(!deliver(&n,&r,n.now+25000));assert(n.hot_ready);
  uint64_t accepted=n.now,until=n.management_until,grant=n.gm5.grant_until;
  assert(until==accepted+5000000&&grant<r.permit_until_us);
  phase_evidence(&n,until+edge);gm5_tick(&n,true,n.now,NULL);
  assert(n.hot_ready==(edge<0));assert(n.management_until==until&&n.gm5.grant_until==grant);
  /* A valid original grant does not excuse a stale timing map. */
  n=field_base();r=control(&n,R2_AUTH_COMMIT,n.now);assert(!deliver(&n,&r,n.now+25000));
  n.timing_valid=false;gm5_tick(&n,true,n.now,NULL);assert(!n.hot_ready);
  /* Peer liveness is identity only: no fresh source permission is invented. */
  n=base(true);uint64_t alive=n.peer[1].alive;n.now=alive+5000000+edge;
  assert(live(&n,1)==(edge<0));assert(n.now>=n.peer[1].candidate_until);
 }
 puts("PASS actual wire COMMIT gives5s coordination; original -1/at/+1 lease and peer liveness, stale PT refusal, unchanged legacy2s and finite grant");
}
static void dispatcher_tests(void)
{
 for(unsigned flood=0;flood<3;flood++)for(unsigned ack=0;ack<3;ack++){
  struct r2_network n=base(true);n.peer[1].nonce=41;n.peer[1].own_nonce=31;strcpy(n.name,"Sender");
  uint64_t start=n.now,authority=n.service_until,grant=start+10000000;
  n.hot_ready=n.management_committed=true;n.management_until=start+2000000;
  n.gm5.issued_until=grant;n.peer[1].next_management=start;
  unsigned counts[4]={0};uint64_t last[4]={start,start,start,start},max_gap[4]={0};
  for(unsigned i=0;i<=60;i++){
   n.now=start+i*100000;
   if(i%4==0)n.peer[1].seen=n.peer[1].alive=n.now;
   if(i%10==0){n.peer[1].candidate_until=n.now+1500000;
    n.view.valid_until_us=n.service_until=n.now+1500000;n.peer[1].next_hello=0;}
   if(ack==1&&i%10==6){n.peer[1].acked_publication=n.publication;n.peer[1].acked_permit=n.service_until;}
   if(ack==2&&i%10==8){n.peer[1].acked_publication=0;n.peer[1].acked_permit=0;}
   if(flood&&(i%(flood==1?4:1)==0)){
    struct r2_auth_record request={.decision=1,.source_epoch=1,.sequence=i+1,.coordinator_boot=n.coordinator_boot};
    response(&n,1,&request,R2_AUTH_ACK,n.now+250000);
   }
   uint8_t p,w[R2_GROUP_CONTROL_MAX];bool h;
   if(!r2_network_next_ready(&n,2,&p,w,&h))continue;
   struct r2_auth_record r;char name[25];uint8_t caps;
   assert(!decode_control(&n,&r,w,r2_control_wire_size(w),n.self,n.peer[p].tx_key,name,&caps,NULL));
   unsigned k=h?1:r.kind==R2_AUTH_SERVICE?2:(r.kind==R2_AUTH_COMMIT||r.kind==R2_AUTH_PREPARE)?3:0;
   assert(r.expires_us>n.now);if(k==3&&r.kind==R2_AUTH_COMMIT)assert(r.permit_until_us==grant);
   counts[k]++;uint64_t gap=n.now-last[k];if(gap>max_gap[k])max_gap[k]=gap;last[k]=n.now;
   r2_network_sent(&n,h,p,w);
   assert(n.management_until==start+2000000&&n.gm5.issued_until==grant);
  }
  assert(counts[1]&&counts[2]&&counts[3]);
  assert(max_gap[1]<=900000&&max_gap[3]<=1100000);
  if(flood)assert(counts[0]);
  printf("PASS offered100ms cadence response_mode=%u ack_mode=%u response=%u hello=%u source=%u management=%u max_hello_gap=%llu max_management_gap=%llu original_grant_retained\n",flood,ack,counts[0],counts[1],counts[2],counts[3],(unsigned long long)max_gap[1],(unsigned long long)max_gap[3]);
  (void)authority;
 }
 for(int boundary=-1;boundary<=1;boundary++){
  struct r2_network n=base(true);n.peer[1].own_nonce=31;n.peer[1].nonce=41;strcpy(n.name,"Sender");
  struct r2_auth_record request={.decision=1,.source_epoch=1,.sequence=1,.coordinator_boot=n.coordinator_boot};
  response(&n,1,&request,R2_AUTH_ACK,n.now+100000);
  uint64_t expiry=n.peer[1].response.expires_us;assert(expiry==n.now+100000);
  n.next_peer=0;n.now=expiry+boundary;n.peer[1].seen=n.peer[1].alive=n.now;
  uint8_t p,w[R2_GROUP_CONTROL_MAX];bool h;
  assert(r2_network_next_ready(&n,2,&p,w,&h));
  struct r2_auth_record r;char name[25];uint8_t caps;
  assert(!decode_control(&n,&r,w,r2_control_wire_size(w),n.self,n.peer[1].tx_key,name,&caps,NULL));
  assert((r.kind==R2_AUTH_ACK)==(boundary<0));
  if(boundary<0){r2_network_sent(&n,h,p,w);assert(n.next_peer==1&&!n.peer[1].response_pending);}
  assert(n.peer[1].response.expires_us==expiry);
 }
 /* Selection without accepted capacity cannot advance fairness or leases. */
 struct r2_network n=base(false);n.gm5.role=R2_GM5_HOT;n.hot_ready=true;
 n.management_until=n.now+2000000;n.gm5.grant_until=n.now+10000000;
 uint64_t lease=n.management_until;n.next_peer=2;
 struct r2_auth_record request={.decision=1,.source_epoch=1,.sequence=1,.coordinator_boot=n.coordinator_boot};
 response(&n,1,&request,R2_AUTH_ACK,n.now+250000);
 uint8_t p,w[R2_GROUP_CONTROL_MAX];bool h;
 for(unsigned i=0;i<20;i++){n.now=lease-2000000+i*100000;r2_network_next_ready(&n,2,&p,w,&h);assert(n.next_peer==2&&n.management_until==lease);}
 n.now=lease;n.peer[1].seen=n.peer[1].alive=n.now;n.peer[1].candidate_until=n.now+1500000;
 n.candidate.valid_until_us=n.view.valid_until_us=n.service_until=n.now+1500000;
 gm5_tick(&n,true,n.now,NULL);assert(!n.hot_ready&&n.gm5.role==R2_GM5_ACQUIRING&&n.gm5.attempt_until==n.now+R2_GM5_REPAIR_US);
 puts("PASS original response expiry -1/at/+1, fair response selection preserves the next class; stalled submissions cannot advance cursor/leases and original management expiry withdraws HOT");
}
static void recovery_join(struct r2_network *a,struct r2_network *b,uint64_t at,
                        struct r2_join_exchange *c,struct r2_join_exchange *s)
{
 static uint64_t nonce=100;
 assert(!r2_group_join_begin(c,false,a->identity,b->identity,a->boot,++nonce,at));
 assert(!r2_group_join_begin(s,true,b->identity,a->identity,b->boot,++nonce,at));
 for(unsigned k=R2_JOIN_REQUEST;k<=R2_JOIN_ACCEPT;k++){
  bool client=k==R2_JOIN_REQUEST||k==R2_JOIN_PROOF;
  struct r2_network *from=client?a:b,*to=client?b:a;
  if(k==R2_JOIN_REQUEST)c->request_at=at;
  if(k==R2_JOIN_CHALLENGE)s->challenge_at=at;
  if(k==R2_JOIN_PROOF)c->proof_at=at;
  struct r2_join_record r={.kind=k,.capabilities=3,.sender=from->identity,.receiver=to->identity,
   .sender_boot=from->boot,.receiver_boot=k==R2_JOIN_REQUEST?0:to->boot,
   .client_nonce=c->nonce,.server_nonce=k==R2_JOIN_REQUEST?0:s->nonce,.sequence=k,.created_us=at,
   .echo_us=k==R2_JOIN_CHALLENGE?c->request_at:k==R2_JOIN_PROOF?s->challenge_at:k==R2_JOIN_ACCEPT?c->proof_at:0,.name="Synthetic"};
  uint8_t wire[R2_GROUP_JOIN_BYTES];assert(!r2_group_join_encode(wire,&r,&from->group));
  assert(!r2_group_join_decode(&r,wire,sizeof wire,&to->group));
  assert(!r2_group_join_receive(client?s:c,&r,at));at++;
 }
 s->accepted=true;a->now=b->now=at;
}
static int recovery_status(struct r2_network *a,struct r2_network *b,uint64_t at)
{
 struct r2_join_exchange c,s;recovery_join(a,b,at,&c,&s);
 uint8_t wire[R2_GM5_STATUS_BYTES];assert(!r2_gm5_status_encode(b,&s,0,wire));
 return r2_gm5_status_receive(a,&c,0,wire,sizeof wire,a->now);
}
static void same_epoch_requalification_tests(void)
{
 struct r2_network a=base(true),b;
 assert(!r2_network_init(&b,0,2,200,20,44,40000,0));b.group=a.group;b.gm5.enabled=true;b.capabilities=3;
 b.gm5.role=R2_GM5_COLD;b.gm5.active=100;b.gm5.active_boot=10;
 a.gm5.selected=0;a.gm5.next_attempt=UINT64_MAX;a.hot_ready=a.standby_authorized=false;
 const uint64_t fault=80000000,recovered=150000000;
 r2_admission_init(&b.admission,0);
 for(uint64_t t=0;t<=fault-1000000;t+=1000000)r2_admission_update(&b.admission,true,true,true,19,t+1500000,t);
 b.candidate.generation=19;b.candidate.available=true;b.candidate.valid_until_us=fault+1500000;
 assert(b.admission.eligible&&!recovery_status(&a,&b,fault-1000000));
 struct r2_gm5_candidate *c=gm5_candidate(&a,200,false);assert(c&&c->eligible);
 b.candidate.generation=20;b.candidate.available=false;
 assert(!r2_admission_update(&b.admission,true,true,false,20,0,fault));
 assert(!recovery_status(&a,&b,fault));assert(!c->eligible&&c->withdraw_epoch==20);
 uint64_t floor=c->withdraw_created;
 for(uint64_t t=fault+1000000;t<=recovered;t+=1000000)
  r2_admission_update(&b.admission,true,true,true,20,t+1500000,t);
 assert(r2_admission_update(&b.admission,true,true,true,20,recovered+1500000,recovered));
 b.candidate.available=true;b.candidate.valid_until_us=recovered+1500000;
 assert(b.admission.source_epoch==20&&b.candidate.generation==20&&b.admission.eligible);
 a.gm5.next_attempt=0;
 int rc=recovery_status(&a,&b,recovered);
 assert(!rc&&c->eligible&&a.gm5.selected==200&&a.gm5.contacting);
 assert(c->withdraw_created==floor&&c->withdraw_epoch==20&&!a.hot_ready&&!a.standby_authorized&&!a.gm5.grant_until);
 /* Eligibility can return; positive ACTIVE authority remains fenced even after
  * that descriptive update. Older/equal positives cannot undo the withdrawal. */
 uint64_t retired=a.gm5.retire_until;uint32_t generation=a.gm5.generation;
 b.gm5.role=R2_GM5_ACTIVE;b.gm5.active=b.identity;b.gm5.active_boot=b.boot;
 assert(recovery_status(&a,&b,recovered+100)==-ESTALE);
 b.gm5.role=R2_GM5_COLD;b.gm5.active=a.identity;b.gm5.active_boot=a.boot;
 for(unsigned old=0;old<2;old++){
  struct r2_join_exchange local,remote;recovery_join(&a,&b,recovered+200+old*100,&local,&remote);
  uint8_t wire[R2_GM5_STATUS_BYTES];b.now=floor-old;
  assert(!r2_gm5_status_encode(&b,&remote,0,wire));
  assert(r2_gm5_status_receive(&a,&local,0,wire,sizeof wire,a.now)==-ESTALE);
 }
 assert(c->eligible&&c->withdraw_created==floor&&c->withdraw_epoch==20&&a.gm5.retire_until==retired&&a.gm5.generation==generation);
 /* Real causal STATUS appointment then actual joined owners still carry no
  * offset/source map. A fresh positive HELLO is the separate fence-clear path. */
 struct r2_join_exchange client,server;recovery_join(&b,&a,recovered+1000,&client,&server);
 a.candidate.valid_until_us=a.now+1500000;
 uint8_t wire[R2_GROUP_CONTROL_MAX];
 assert(!r2_gm5_status_encode(&b,&client,0,wire));assert(!r2_gm5_status_receive(&a,&server,0,wire,R2_GM5_STATUS_BYTES,a.now));
 assert(!r2_gm5_status_encode(&a,&server,1,wire));assert(!r2_gm5_status_receive(&b,&client,1,wire,R2_GM5_STATUS_BYTES,b.now));
 assert(!r2_gm5_status_encode(&b,&client,2,wire));assert(!r2_gm5_status_receive(&a,&server,2,wire,R2_GM5_STATUS_BYTES,a.now));
 assert(r2_gm5_wants_control(&a,b.identity)&&r2_gm5_wants_control(&b,a.identity));
 assert(c->withdraw_created==floor&&c->withdraw_epoch==20&&!a.hot_ready&&!b.hot_ready);
 client.control=server.control=true;r2_network_joined(&a,1,&server,a.now);r2_network_joined(&b,1,&client,b.now);
 assert(!a.peer[1].map_valid&&!b.peer[1].map_valid&&!a.peer[1].candidate_until&&!b.peer[1].candidate_until);
 strcpy(a.name,"Active");strcpy(b.name,"Recovered");
 for(unsigned i=0;i<3;i++){
  struct r2_network *from=i==1?&b:&a,*to=i==1?&a:&b;
  a.now=b.now=recovered+2000+i*1000;
  from->peer[1].next_hello=0;from->peer[1].next_service=from->peer[1].next_management=UINT64_MAX;
  uint8_t peer;bool hello;
  assert(r2_network_next_ready(from,2,&peer,wire,&hello)&&hello&&peer==1);
  r2_network_sent(from,hello,peer,wire);
  assert(!r2_network_receive(to,1,wire,r2_control_wire_size(wire),to->now,to->now));
  if(!i)assert(c->withdraw_epoch==20&&!b.peer[1].map_valid);
 }
 assert(c->withdraw_created==floor&&!c->withdraw_epoch&&a.peer[1].map_valid&&b.peer[1].map_valid);
 assert(!a.hot_ready&&!b.hot_ready&&!a.standby_authorized&&!b.standby_authorized&&!a.gm5.grant_until&&!b.gm5.grant_until);
 puts("PASS sameepoch60s requalification: newer COLD inventory permits nomination; old/equal/ACTIVE claims remain fenced, STATUS retains withdrawal, causal JOIN/HELLO gates map, no HOT/grant fabricated");
}
static void record_budget_tests(void)
{
 /* MAC-correct overlong records must fail in their own envelope, not merely
  * because a G5 frame was presented to a legacy decoder. */
 for(unsigned gm5=0;gm5<2;gm5++)for(int edge=-1;edge<=1;edge++){
  struct r2_network n=base(false);struct r2_auth_record r=control(&n,R2_AUTH_COMMIT,n.now),decoded;
  uint64_t ttl=gm5?R2_GM5_RECORD_US:R2_AUTH_ORIGINAL_TTL_US;
  uint64_t ids[4]={100,200,0,0};uint8_t wire[R2_GROUP_CONTROL_MAX],caps;char name[25];
  r.expires_us=r.created_us+ttl+edge;
  int encoded=gm5?r2_group_control_encode5(wire,&r,&n.group,ids,"Sender",3):r2_group_control_encode(wire,&r,&n.group,ids,"Sender",3);
  assert((encoded==0)==(edge<=0));
  r.expires_us=r.created_us+ttl;assert(!(gm5?r2_group_control_encode5(wire,&r,&n.group,ids,"Sender",3):r2_group_control_encode(wire,&r,&n.group,ids,"Sender",3)));
  r2_group_put(wire+81,r.created_us+ttl+edge,8);size_t len=r2_control_wire_size(wire);
  assert(!r2_group_mac(wire+len-16,&n.group,gm5?"PTOBG5C5":"PTOBG1C1",wire,len-16));
  int rc=gm5?r2_group_control_decode5(&decoded,wire,len,&n.group,ids,200,name,&caps,NULL):r2_group_control_decode(&decoded,wire,len,&n.group,ids,200,name,&caps);
  assert((rc==0)==(edge<=0));
  if(gm5)assert(r2_group_control_decode(&decoded,wire,len,&n.group,ids,200,name,&caps));
  else assert(r2_group_control_decode5(&decoded,wire,len,&n.group,ids,200,name,&caps,NULL));
  uint8_t key[16]={1};r.expires_us=r.created_us+250000+edge;
  assert((r2_auth_encode(wire,&r,key)==0)==(edge<=0));
 }
 /* Mapped original1s record boundary and response clipping remain immutable. */
 for(int edge=-1;edge<=1;edge++){
  struct r2_network n=field_base();struct r2_auth_record r=control(&n,R2_AUTH_COMMIT,n.now);
  r.expires_us=r.created_us+R2_GM5_RECORD_US;
  uint64_t expiry=r.expires_us-10000-map_drift(&n.peer[1],r.expires_us,r.created_us);
  int rc=deliver(&n,&r,expiry+edge);
  if(edge<0){assert(!rc&&n.peer[1].response.expires_us==expiry);assert(deliver(&n,&r,expiry-1)==-ESTALE);}
  else assert(rc==-ESTALE&&!n.gm5.grant_until);
 }
 puts("PASS MAC-correct legacy250ms/G5 1s codec -1/at/+1 and cross-domain refusal; mapped1s expiry/response clip/replay boundaries");
}
static void issued_history_tests(void)
{
 _Static_assert(R2_ISSUED_RECORDS==4,"exercise actual native history capacity");
 for(int edge=-1;edge<=1;edge++){
  struct r2_network n=base(true);n.peer[1].own_nonce=31;n.peer[1].nonce=41;
  uint64_t start=n.now,expiry=start+R2_GM5_RECORD_US;
  for(unsigned i=0;i<2;i++){
   n.now=start+i*R2_GM5_PROBE_US;struct r2_auth_record r=record(&n,1,R2_AUTH_SERVICE);
   r.sequence=i+1;r.decision=n.publication;r.permit_until_us=n.service_until;
   uint8_t w[R2_GROUP_CONTROL_MAX];assert(!encode_control(&n,w,&r,n.peer[1].tx_key));r2_network_sent(&n,false,1,w);
   assert(n.peer[1].next_service==n.now+R2_GM5_PROBE_US);
  }
  n.peer[1].offset_low=n.peer[1].offset_high=0;n.peer[1].map_anchor=start;
  struct r2_auth_record ack=control(&n,R2_AUTH_ACK,expiry+edge-1000);
  ack.root=0;ack.root_boot=ack.coordinator_boot=10;ack.decision=n.publication;ack.source_second=1;
  int rc=deliver(&n,&ack,expiry+edge);
  assert((rc==0)==(edge<0));if(edge<0)assert(n.peer[1].acked_permit==start+1500000);
 }
 struct r2_network n=base(true);n.peer[1].nonce=41;uint64_t start=n.now;
 for(unsigned i=0;i<6;i++){
  n.now=start+i*R2_GM5_PROBE_US;struct r2_auth_record r=record(&n,1,R2_AUTH_SERVICE);r.sequence=i+1;r.decision=1;
  uint8_t w[R2_GROUP_CONTROL_MAX];assert(!encode_control(&n,w,&r,n.peer[1].tx_key));
  unsigned slot=n.peer[1].issued_head%R2_ISSUED_RECORDS;
  assert(!n.peer[1].issued[slot].sequence||n.peer[1].issued[slot].expiry<=n.now);
  r2_network_sent(&n,false,1,w);
 }
 puts("PASS four actual issued slots never evict unexpired500ms SERVICE records; ACK accepted only before original1s expiry and retains original source permit");
}

int main(void)
{
 record_budget_tests();issued_history_tests();clock_field_boundary_tests();clock_rate_corner_tests();delay_burst_tests();
 same_epoch_requalification_tests();lifecycle_boundary_tests();dispatcher_tests();
 late_map_acquire_tests();coordination_boundary_tests();
 hello_source_refresh_tests();
 contact_tests();
 const struct {unsigned role;uint64_t active,selected;bool connected;uint64_t delay;} cadence[]={
  {R2_GM5_ACTIVE,100,0,false,5000000},
  {R2_GM5_ACTIVE,100,200,false,5000000},
  {R2_GM5_ACTIVE,100,200,true,30000000},
  {R2_GM5_COLD,0,0,false,5000000},
  {R2_GM5_COLD,200,0,false,30000000},
  {R2_GM5_ACQUIRING,200,100,false,5000000},
  {R2_GM5_HOT,200,100,true,30000000}
 };
 for(unsigned i=0;i<sizeof cadence/sizeof cadence[0];i++){
  struct r2_network poll=base(true);poll.gm5.role=cadence[i].role;poll.gm5.active=cadence[i].active;
  poll.gm5.selected=cadence[i].selected;poll.peer[1].connected=cadence[i].connected;
  poll.gm5.next_poll=poll.now-1000000;poll.gm5.round_until=poll.gm5.next_attempt=poll.now+60000000;
  poll.gm5.candidate[0]=(struct r2_gm5_candidate){.identity=200,.boot=20,.eligible=true,.epoch=1,.until=poll.now+120000000};
  struct r2_network completed=poll;r2_gm5_poll_completed(&completed,completed.now);
  gm5_tick(&poll,true,poll.now,NULL);
  assert(poll.gm5.next_poll==poll.now+cadence[i].delay&&poll.gm5.next_poll==completed.gm5.next_poll);
 }
 puts("PASS completed/missed poll cadence agrees across ACTIVE unpaired/repair/bound and COLD/acquiring/HOT roles");
 struct r2_network n=base(false);struct r2_auth_record commit={.kind=R2_AUTH_COMMIT,.root=1,.root_boot=20,.coordinator_boot=10,.source_epoch=1,.source_second=descriptor(&n),.decision=7,.created_us=70000000,.permit_until_us=80000000,.flags=1};
 assert(!gm5_management_receive(&n,1,&commit,79900000,70200000));assert(n.hot_ready&&n.standby_authorized&&n.gm5.grant_until==79900000);
 assert(!gm5_management_receive(&n,1,&commit,79910000,70200000));assert(n.gm5.grant_until==79900000); /* Offset refinement cannot extend original permission. */
 assert(!gm5_management_receive(&n,1,&commit,79800000,70200000));assert(n.gm5.grant_until==79800000);
 for(unsigned why=R2_GM5_SOURCE;why<=R2_GM5_GRANT_EXPIRED;why++){
  struct r2_network copy=n;gm5_grant_clear(&copy,why);
  assert(gm5_management_receive(&copy,1,&commit,79900000,70200000));assert(!copy.standby_authorized&&!copy.hot_ready&&!copy.gm5.grant_until);
  assert(copy.hot_withdrawal_reason==(512u|r2_network_hot_refusal(&copy)));
 }
 struct r2_network changed=n;changed.root_boot++;assert(gm5_management_receive(&changed,1,&commit,79900000,70200000));
 changed=n;changed.view.generation++;assert(gm5_management_receive(&changed,1,&commit,79900000,70200000));
 changed=n;changed.coordinator_boot++;assert(gm5_management_receive(&changed,1,&commit,79900000,70200000));
 struct r2_network expiry=n;expiry.admission.boot_at=0;expiry.admission.last_expiry=expiry.now+1500000;expiry.gm5.role=R2_GM5_HOT;expiry.gm5.grant_until=expiry.now+100000;expiry.management_until=expiry.now+2000000;
 struct r2_clock own={0};own.view.source_available=true;own.view.source_epoch=1;own.last_pair_edge=own.last_rmc=expiry.now+100000;own.last_pair_utc=INT64_C(1791417600100000);
 r2_network_tick(&expiry,&own,expiry.now+100000);assert(!expiry.hot_ready&&!expiry.standby_authorized&&!expiry.gm5.grant_until&&expiry.gm5.role==R2_GM5_ACQUIRING);
 assert(gm5_management_receive(&expiry,1,&commit,expiry.now+9000000,expiry.now+250000));assert(!expiry.hot_ready&&!expiry.standby_authorized);
 puts("PASS retired-generation replay, narrower mapping, root/HOT incarnations/source epochs; G5 reason enum never enters legacy refusal bitmask");
 n=base(true);n.gm5.grant_pending=true;n.gm5.issued_until=n.now+10000000;n.gm5.attempt_until=n.now+30000000;
 struct r2_auth_record ack={.kind=R2_AUTH_READY,.root=0,.root_boot=10,.coordinator_boot=20,.source_epoch=1,.source_second=descriptor(&n),.decision=7,.flags=1};
 assert(!gm5_management_receive(&n,1,&ack,0,n.now+250000));assert(!n.hot_ready&&n.gm5.grant_pending); /* PREPARE READY is not COMMIT confirmation. */
 struct r2_network lost=n;gm5_failed(&lost,R2_GM5_TIMEOUT);assert(lost.gm5.retire_until>=n.gm5.issued_until+R2_GM5_RETIRE_GUARD_US);assert(!lost.hot_ready);
 ack.flags=5;ack.decision++;assert(gm5_management_receive(&n,1,&ack,0,n.now+250000));assert(!n.hot_ready);
 ack.decision--;assert(!gm5_management_receive(&n,1,&ack,0,n.now+250000));assert(n.hot_ready&&!n.gm5.grant_pending&&!n.gm5.attempt_until);
 puts("PASS lost/late COMMIT acknowledgements, uncertain issuance retirement and confirmed global HOT");
 n=base(true);n.gm5.selected=0;n.gm5.candidate[0]=(struct r2_gm5_candidate){.identity=200,.boot=20,.eligible=true,.epoch=1,.until=n.now+120000000,.quality=n.quality};n.gm5.candidate[0].quality.identity=200;
 n.gm5.candidate[1]=n.gm5.candidate[0];n.gm5.candidate[1].identity=n.gm5.candidate[1].quality.identity=300;n.gm5.candidate[1].successes=2;
 gm5_select(&n);assert(n.gm5.selected==300);
 n.gm5.selected=0;n.gm5.candidate[0].quality.priority1=1;gm5_select(&n);assert(n.gm5.selected==200);
 n.gm5.selected=0;n.gm5.candidate[0].inhibited=true;n.gm5.candidate[0].negative_root=100;gm5_select(&n);assert(n.gm5.selected==200); /* Explicit same-incumbent acquisition can seek its missing direct map. */
 n.gm5.selected=0;n.gm5.candidate[0].negative_root=999;gm5_select(&n);assert(n.gm5.selected==300);
 puts("PASS source tier then delivery history then identity; same-root inhibited acquisition and foreign-root exclusion");
 assert(!r2_network_init(&n,0,2,100,10,22,40000,0));n.gm5.enabled=true;n.capabilities=3;n.gm5.active=200;n.gm5.active_boot=20;n.gm5.active_until=200000000;r2_network_capabilities(&n,1,1);
 struct r2_gm_coordination constraint={.root=200,.boot=0,.epoch=1,.until=200000000};
 for(uint64_t t=0;t<=65000000;t+=1000000){struct r2_clock c={0};c.view.source_available=true;c.view.source_epoch=1;c.last_pair_edge=c.last_rmc=t;c.last_pair_utc=INT64_C(1791417600000000)+t;r2_network_tick_coordinated(&n,&c,t,&constraint);assert(!n.local_gm&&!n.hot_ready&&!n.standby_authorized);}
 assert(n.admission.eligible&&n.gm5.inhibited);uint64_t qualified=n.admission.stable_at;
 struct r2_clock c={0};c.view.source_available=true;c.view.source_epoch=1;c.last_pair_edge=c.last_rmc=66000000;c.last_pair_utc=INT64_C(1791417666000000);r2_network_tick(&n,&c,66000000);assert(n.admission.eligible&&n.admission.stable_at==qualified);
 c.view.source_epoch=2;r2_network_tick(&n,&c,66000001);assert(!n.admission.eligible&&n.admission.stable_at==66000001);
 puts("PASS continuous physical COLD qualification under unresolved foreign inhibition; epoch change resets the original dwell");
 /* Received generations belong to the appointed root scope. A node's own
  * issuer counter survives role changes without becoming a foreign floor. */
 n=base(false);n.gm5.generation=100;n.management_generation=0;
 struct r2_auth_record prepare=commit;prepare.kind=R2_AUTH_PREPARE;prepare.decision=1;prepare.flags=0;prepare.source_second=descriptor(&n);
 assert(!gm5_management_receive(&n,1,&prepare,0,n.now+250000));assert(n.management_generation==1&&n.gm5.generation==100);
 prepare.kind=R2_AUTH_COMMIT;prepare.flags=1;assert(!gm5_management_receive(&n,1,&prepare,n.now+9000000,n.now+250000));assert(n.gm5.grant_generation==1&&n.gm5.generation==100);
 puts("PASS foreign received-generation namespace independent of retained local issuer generation100");
 for(unsigned direct=0;direct<2;direct++){
  n=base(true);n.admission.boot_at=0;n.admission.last_expiry=n.now+1500000;
  struct r2_gm_coordination co={.root=200,.boot=20,.epoch=1,.until=n.now+12000000,.direct=direct};
  struct r2_clock source={0};source.view.source_available=true;source.view.source_epoch=1;source.last_pair_edge=source.last_rmc=n.now;source.last_pair_utc=INT64_C(1791417600000000);
  r2_network_tick_coordinated(&n,&source,n.now,&co);assert(n.admission.eligible&&!n.local_gm&&!n.ready&&n.choice==255&&n.gm5.role==R2_GM5_COLD);
  n.gm5.round_until=n.now;source.last_pair_edge=source.last_rmc=n.now+1;r2_network_tick_coordinated(&n,&source,n.now+1,&co);assert(!n.local_gm&&!n.ready); /* Lower self identity cannot bypass the root constraint. */
 }
 puts("PASS G5 live foreign root restricts already-ACTIVE and lower-ranked local election with/without direct proof");
 n=base(false);n.admission.boot_at=0;n.admission.last_expiry=n.now+1500000;n.gm5.grant_generation=7;n.gm5.grant_until=n.now+10000000;n.standby_authorized=true;n.standby_root_boot=20;n.standby_source_epoch=1;
 n.pair=(struct r2_pair){.root=1,.standby=0,.root_boot=20,.standby_boot=10,.epoch=1,.until=n.gm5.grant_until};n.peer[1].source_withdrawn=true;n.peer[1].candidate_epoch=0;n.root_health_at=n.now;
 struct r2_gm_coordination co={.root=200,.boot=20,.epoch=1,.until=n.now+12000000,.direct=true};uint64_t origin=n.now,original_until=co.until;
 for(unsigned ms=0;ms<=3000;ms+=10){uint64_t at=origin+ms*1000;struct r2_clock source={0};source.view.source_available=true;source.view.source_epoch=1;source.last_pair_edge=source.last_rmc=at;source.last_pair_utc=INT64_C(1791417600000000)+ms*1000;
  if(ms){co.direct=false;n.peer[1].connected=false;}r2_network_tick_coordinated(&n,&source,at,&co);
  if(ms<3000)assert(!n.local_gm);else assert(n.local_gm);
 }
 assert(n.gm5.retired_until==original_until&&n.gm5.retired_root==200&&n.gm5.retired_boot==20&&n.gm5.retired_epoch==1);
 co.epoch=2;struct r2_clock source={0};source.view.source_available=true;source.view.source_epoch=1;source.last_pair_edge=source.last_rmc=n.now+1;source.last_pair_utc=INT64_C(1791417603000001);r2_network_tick_coordinated(&n,&source,n.now+1,&co);assert(!n.local_gm&&!n.ready);
 puts("PASS exact direct withdrawal, original3s fence through ACL drain, finite nonrolling root retirement, changed-epoch refusal");
 n=base(true);n.gm5.selected=0;n.gm5.candidate[0]=(struct r2_gm5_candidate){.identity=200,.boot=20,.cooldown=n.now+60000000,.failures=3,.successes=2,.last_reason=R2_GM5_TIMEOUT};
 uint64_t cooldown=n.gm5.candidate[0].cooldown;
 for(unsigned boot=21;boot<27;boot++){
  struct r2_network remote;assert(!r2_network_init(&remote,0,2,200,boot,44,40000,n.now));remote.group=n.group;remote.gm5.enabled=true;remote.admission.eligible=true;remote.candidate.generation=boot;
  struct r2_join_exchange local={.self=100,.peer=200,.boot=10,.peer_boot=boot,.nonce=3,.remote_nonce=4,.until=n.now+5000000,.accepted=true};
  struct r2_join_exchange other={.self=200,.peer=100,.boot=boot,.peer_boot=10,.nonce=4,.remote_nonce=3,.until=n.now+5000000,.accepted=true,.server=true};
  uint8_t status[R2_GM5_STATUS_BYTES];assert(!r2_gm5_status_encode(&remote,&other,0,status));assert(!r2_gm5_status_receive(&n,&local,0,status,sizeof status,n.now));
  assert(n.gm5.candidate[0].boot==boot&&n.gm5.candidate[0].cooldown==cooldown&&n.gm5.candidate[0].failures==3&&n.gm5.candidate[0].successes==2&&!n.gm5.selected);
 }
 puts("PASS six candidate incarnations/source epochs retain exact per-identity failures, success history and original cooldown");
 /* RX policy uses original direct contact evidence, never local election intent. */
 n=base(true);n.local_gm=n.ready=true;n.gm5.selected=200;n.gm5.active=200;
 n.gm5.attempt_until=n.now+30000000;assert(!r2_network_mesh_contact(&n,n.now));
 n.peer[1].member=true;n.peer[1].member_at=n.now;uint64_t contact=n.now;
 assert(!r2_network_mesh_contact(&n,contact-1));
 assert(r2_network_mesh_contact(&n,contact+R2_V14_MEMBERSHIP_US-1));
 assert(!r2_network_mesh_contact(&n,contact+R2_V14_MEMBERSHIP_US));
 assert(!r2_network_mesh_contact(&n,contact+R2_V14_MEMBERSHIP_US+1));n.peer[1].member=false;
 struct r2_network sender;assert(!r2_network_init(&sender,0,2,200,20,44,40000,n.now));sender.group=n.group;sender.gm5.enabled=true;
 struct r2_join_exchange receiver_join={.self=100,.peer=200,.boot=10,.peer_boot=20,.nonce=3,.remote_nonce=4,.until=n.now+5000000,.accepted=true};
 struct r2_join_exchange sender_join={.self=200,.peer=100,.boot=20,.peer_boot=10,.nonce=4,.remote_nonce=3,.until=n.now+5000000,.accepted=true,.server=true};
 uint8_t contact_wire[R2_GM5_STATUS_BYTES];assert(!r2_gm5_status_encode(&sender,&sender_join,0,contact_wire));
 receiver_join.accepted=false;assert(r2_gm5_status_receive(&n,&receiver_join,0,contact_wire,sizeof contact_wire,n.now));
 assert(!r2_network_mesh_contact(&n,n.now));receiver_join.accepted=true;
 assert(!r2_gm5_status_receive(&n,&receiver_join,0,contact_wire,sizeof contact_wire,n.now));
 n.ready=false;n.peer[1].map_valid=false; /* PT/source loss does not revoke authenticated contact. */
 assert(!r2_network_mesh_contact(&n,contact-1));
 assert(r2_network_mesh_contact(&n,contact+R2_GM5_STATUS_US-1));
 assert(!r2_network_mesh_contact(&n,contact+R2_GM5_STATUS_US));
 assert(!r2_network_mesh_contact(&n,contact+R2_GM5_STATUS_US+1));
 puts("PASS RX policy solo/attempt-only/unauthenticated false, real STATUS contact, original membership/STATUS boundaries, PT-independent");
 /* Worst-width counters/identities/deadlines, normalized boolean objects. */
 struct r2_gm5_diagnostic d;memset(&d,255,sizeof d);
 d.physical_eligible=d.local_hot=d.global_hot=d.global_hot_known=d.map_valid=d.locked=d.serving=false;
 d.state.enabled=true;d.state.inhibited=d.state.bridge=d.state.repair_used=d.state.grant_pending=d.state.contacting=false;d.state.role=R2_GM5_ACQUIRING;
 for(unsigned c=0;c<R2_GM5_CANDIDATES;c++){d.state.candidate[c].role=R2_GM5_ACQUIRING;d.state.candidate[c].eligible=d.state.candidate[c].inhibited=false;}
 char json[2200];for(unsigned page=0;page<5;page++){int length=r2_gm_diagnostic_format(json,sizeof json,&d,page,"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",UINT64_MAX);assert(length>0&&length<2200);printf("PASS worst-width GM page%u payload_bytes=%d console_budget3072\n",page,length);}
 return 0;
}

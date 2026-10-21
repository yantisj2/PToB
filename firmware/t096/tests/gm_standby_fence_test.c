/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Targeted hostile-state/replay checks in the actual serialized G5 owner.
 * These are protocol negative controls, separate from physical-tuple simulation.
 */
#include "../protocol/network.c"
#include <assert.h>
#include <stdio.h>
#include "gm_diagnostics.h"
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
  uint64_t at=n.now;r2_network_tick(&n,&source,at);
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
  if(fault==3)n.gm5.ready_at=n.now-1000000; /* Nor can it renew remote READY. */
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
   if(mode!=1&&elapsed>=31000000)break;
  }
  if(mode==1){assert(!first_hot&&!a.gm5.selected&&a.gm5.failures==1);assert(!b.hot_ready&&!b.standby_authorized);continue;}
  assert(first_hot>=25643000+R2_GM5_STABLE_US&&first_hot<32000000);
  assert(sent[R2_AUTH_PREPARE]&&sent[R2_AUTH_READY]&&sent[R2_AUTH_COMMIT]);
  assert(a.management_committed&&!a.gm5.grant_pending&&b.standby_authorized);
  assert(issued_at&&a.gm5.issued_until-issued_at==R2_GM5_GRANT_US);
  assert(b.gm5.grant_until<=a.gm5.issued_until&&b.gm5.grant_until>b.now);
  if(mode==2){a.now++;gm5_tick(&a,false,a.now,NULL);assert(!a.hot_ready&&!a.local_gm&&!a.ready&&!a.gm5.selected);}
 }
 puts("PASS controlled PT map first at25.643s then original5s stability: encoded READY/COMMIT/ACK confirms HOT; unproductive fixed ACQUIRE expires; real source withdrawal and10s grants unchanged");
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
 n=contact_base();n.gm5.retire_until=n.now+10000000;gm5_select(&n);assert(!n.gm5.selected);n.now=n.gm5.retire_until;gm5_select(&n);assert(n.gm5.contacting);
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
 struct r2_network n=base(true);n.gm5.attempt_until=n.now+R2_GM5_ACQUIRE_US;
 n.gm5.stable_since=n.now-R2_GM5_STABLE_US+1;n.gm5.ready_at=n.now-1000000;
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
  n.next_peer=1;n.now=expiry+boundary;n.peer[1].seen=n.peer[1].alive=n.now;
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
 puts("PASS original response expiry -1/at/+1, urgency preserves ordinary turn; stalled submissions cannot advance cursor/leases and original management expiry withdraws HOT");
}
int main(void)
{
 lifecycle_boundary_tests();dispatcher_tests();
 late_map_acquire_tests();
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
 /* Worst-width counters/identities/deadlines, normalized boolean objects. */
 struct r2_gm5_diagnostic d;memset(&d,255,sizeof d);
 d.physical_eligible=d.local_hot=d.global_hot=d.global_hot_known=d.map_valid=d.locked=d.serving=false;
 d.state.enabled=true;d.state.inhibited=d.state.bridge=d.state.repair_used=d.state.grant_pending=d.state.contacting=false;d.state.role=R2_GM5_ACQUIRING;
 for(unsigned c=0;c<R2_GM5_CANDIDATES;c++){d.state.candidate[c].role=R2_GM5_ACQUIRING;d.state.candidate[c].eligible=d.state.candidate[c].inhibited=false;}
 char json[2200];for(unsigned page=0;page<5;page++){int length=r2_gm_diagnostic_format(json,sizeof json,&d,page,"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",UINT64_MAX);assert(length>0&&length<2200);printf("PASS worst-width GM page%u payload_bytes=%d console_budget3072\n",page,length);}
 return 0;
}

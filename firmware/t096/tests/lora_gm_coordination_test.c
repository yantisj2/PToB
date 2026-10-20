/* Actual election owner with explicit synthetic direct sessions/source input.
 * Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#define main existing_pair_fixture_main
#include "gm_pair_hint_test.c"
#undef main
#include "runtime_profile.h"
static void coordinated(struct r2_network *n,struct r2_gm_coordination *c,uint64_t at){
 struct r2_clock clock={0};clock.view.source_available=true;clock.view.source_epoch=1;
 clock.last_pair_edge=clock.last_rmc=at;clock.last_pair_utc=1791504000000000LL;
 r2_network_tick_coordinated(n,&clock,at,c);
}
static struct r2_gm_coordination scope(void){
 return (struct r2_gm_coordination){.root=20,.boot=100,.epoch=1,.until=now+12000000};
}
static void direct(struct r2_network *n){
 setup(n,true);n->peer[1].serving_epoch=1;n->peer[1].serving_until=now+1000000;
}
int main(void){
 struct r2_network n;struct r2_gm_coordination c=scope();
 /* An active, lower-ID local GM cannot beat the LoRa root by quality, old
  * reservation, test selection, local PPS or prior HOT takeover. */
 for(unsigned mode=0;mode<6;mode++){
  setup(&n,true);c=scope();n.peer[1].connected=false;
  if(mode==0)c.boot=0; /* timing-only cold acquisition */
  if(mode==1)n.pair=(struct r2_pair){200,100,now+10000000,1,0,1};
  if(mode==2){n.standby_authorized=n.takeover_pending=true;n.standby_source_epoch=1;n.standby_root_boot=200;}
  if(mode==3)n.profile_root=0,n.profile_client=1;
  if(mode==4)c.root=30; /* announced GM unknown to the BLE roster */
  if(mode==5)n.peer[1].identity=30; /* immediate relay is not the root */
  coordinated(&n,&c,now);assert(!n.local_gm&&!n.ready&&!n.hot_ready&&!n.admission.eligible);
  assert(n.choice==255&&n.coordinator==255&&!n.standby_authorized&&!n.takeover_pending);
  assert(!n.pair.until&&!n.management_until&&!n.service_until);
 }
 /* Exact current direct root restores ordinary admission and chooses it,
  * without fabricating timing, a service map or a HOT handshake. */
 direct(&n);c=scope();assert(r2_network_coordination_direct(&n,&c,now));
 coordinated(&n,&c,now);assert(n.choice==1&&!n.local_gm&&!n.hot_ready&&n.admission.eligible);
 assert(!n.ready&&!n.view.available&&!n.management_until);
 /* Each independent direct-contact/admission fence is necessary. */
 for(unsigned fail=0;fail<12;fail++){
  direct(&n);c=scope();struct r2_net_peer *p=&n.peer[1];
  switch(fail){
   case 0:c.boot=0;break;case 1:p->connected=false;break;case 2:p->authenticated=false;break;
   case 3:p->member=false;break;case 4:p->map_valid=false;break;case 5:p->capabilities=4;break;
   case 6:p->identity=30;break;case 7:p->boot++;break;case 8:p->candidate_epoch++;break;
   case 9:p->alive=now-R2_MANAGEMENT_LEASE_US;break;case 10:p->seen=now-10000000;break;
   case 11:p->member_at=now-R2_V14_MEMBERSHIP_US;break;
  }
  assert(!r2_network_coordination_direct(&n,&c,now));coordinated(&n,&c,now);
  assert(!n.local_gm&&!n.hot_ready&&!n.admission.eligible&&n.choice==255);
 }
 /* Positive source expiry alone preserves direct contact/admission, but
  * cannot authorize local takeover while the LoRa root still claims service. */
 direct(&n);c=scope();n.peer[1].candidate_until=n.peer[1].serving_until=now;
 assert(r2_network_coordination_direct(&n,&c,now));coordinated(&n,&c,now);
 assert(n.admission.eligible&&!n.local_gm&&n.choice==255);
 /* BLE-owned current direct JOIN+PA proof allows cold source readmission,
  * but cannot invent a control root from an absent/unmapped peer. */
 setup(&n,false);c=scope();c.direct=true;n.peer[1].connected=false;
 coordinated(&n,&c,now);assert(n.admission.eligible&&n.choice==255&&!n.ready&&!n.hot_ready);
 /* No contact proof can resolve an unknown companion boot. */
 setup(&n,true);c.boot=0;coordinated(&n,&c,now);assert(!n.admission.eligible&&n.choice==255);
 /* Same-root direct contact preserves actual ordinary HOT negotiation. It
  * still needs the existing service map, management ACKs and synced timing. */
 direct(&n);c=scope();n.choice=1;n.root_boot=100;n.local_gm=false;n.coordinator=0;n.coordinator_boot=200;
 n.view.available=true;n.view.generation=1;n.view.valid_until_us=now+1000000;n.publication=1;
 coordinated(&n,&c,now);assert(n.ready&&!n.hot_ready&&n.management_hash);
 n.management_ready=3;n.management_committed=true;n.timing_valid=true;
 n.timing_boot=100;n.timing_epoch=n.timing_train=1;n.timing_at=now+1;
 coordinated(&n,&c,now+1);assert(n.hot_ready&&n.standby_authorized&&n.pair.root==1&&n.pair.standby==0);
 /* Root inhibition must not re-rank the HOT peer of that same root. A third
  * or fourth independently eligible candidate joins cold with respect to
  * this established pair, regardless of its better nominal dataset. */
 struct r2_network paired=n;
 for(unsigned count=3;count<=4;count++){
  n=paired;n.count=count;r2_network_capabilities(&n,(1u<<count)-1,(1u<<count)-1);
  for(unsigned p=2;p<count;p++){
   n.peer[p]=n.peer[1];n.peer[p].identity=n.peer[p].quality.identity=5-p;
   n.peer[p].boot=300+p;n.peer[p].serving_until=0;n.peer[p].serving_epoch=0;
  }
  coordinated(&n,&c,now+2);
  assert(n.choice==1&&n.coordinator==0&&n.hot_ready&&n.standby_authorized);
  assert(n.pair.root==1&&n.pair.standby==0&&n.management_generation==paired.management_generation);
 }
 n=paired;
 /* Current authenticated root withdrawal keeps direct eligibility and the
  * actual prior HOT takeover/output fence while the LoRa lease is live. */
 n.peer[1].source_withdrawn=true;n.peer[1].candidate_epoch=0;n.peer[1].candidate_until=0;
 n.peer[1].serving_until=now+500000;n.peer[1].alive=now+2;
 coordinated(&n,&c,now+2);assert(n.takeover_pending&&n.choice==n.self&&!n.local_gm);
 uint64_t promote=n.takeover_not_before;assert(promote==now+3000002&&promote<c.until);
 for(uint64_t at=now+250000;at<promote;at+=250000){
  n.peer[1].seen=n.peer[1].alive=n.peer[1].member_at=at;coordinated(&n,&c,at);assert(!n.local_gm);
 }
 n.peer[1].seen=n.peer[1].alive=n.peer[1].member_at=promote;coordinated(&n,&c,promote);
 assert(n.local_gm&&n.choice==n.self&&n.admission.eligible); /* positive ACTIVE under live constraint */
 /* The separately tested receiver retires the old foreign scope only when
  * TC01 forwards this CURRENT actual origin in a newer full context. After
  * that retirement, old-root loss cannot demote this established origin. */
 struct r2_network origin=n;struct r2_gm_coordination retired={0};
 origin.peer[1].connected=false;coordinated(&origin,&retired,promote+1);
 assert(origin.local_gm&&origin.choice==origin.self);
 /* Genuine direct contact loss revokes even that completed replacement
  * while the old foreign announcement remains within its bounded hold. */
 n.peer[1].connected=false;coordinated(&n,&c,promote+1);
 assert(!n.hot_ready&&!n.standby_authorized&&!n.takeover_pending&&!n.pair.until&&!n.local_gm);
 /* Own forwarded root cannot demote its origin. Stale self boot is not a
  * foreign authority or a replacement for an existing foreign constraint. */
 setup(&n,true);c=scope();c.root=10;c.boot=200;coordinated(&n,&c,now);assert(n.local_gm);
 setup(&n,true);c.boot=199;coordinated(&n,&c,now);assert(n.local_gm);
 /* Expiry releases the negative constraint through ordinary source admission
  * and topology fences; it never creates instantaneous replacement output. */
 setup(&n,true);c=scope();coordinated(&n,&c,now);uint64_t end=c.until;
 coordinated(&n,&c,end);assert(!n.local_gm&&!n.hot_ready&&!n.admission.eligible);
 /* The actual normal-profile restart calls this same network_init with a
  * fresh profile incarnation and CURRENT time, even with continuous GNSS.
  * Exercise repeated RC->GM entries: a cleared RX sample during drain cannot
  * bypass the existing60s startup/source admission. Cold LoRa reacquisition
  * at4s inhibits before admission; no negative lease is renewed by the toggle. */
 for(unsigned entry=0;entry<3;entry++){
  uint64_t at=now+(uint64_t)entry*180000000,boot=r2_profile_incarnation(200,entry*2+1);
  assert(boot&&boot!=200&&!r2_network_init(&n,0,4,10,boot,boot^123,40000,at));
  n.group=(struct r2_group_config){.psk={1},.id=7,.epoch=9};n.capabilities=3;n.relative_source=true;
  r2_network_capabilities(&n,1,1);struct r2_gm_coordination reacquired={0};
  for(uint64_t elapsed=0;elapsed<=70000000;elapsed+=250000){
   if(elapsed>=4000000)reacquired=(struct r2_gm_coordination){.root=20,.boot=100,.epoch=1,.until=at+elapsed/4000000*4000000+12000000};
   coordinated(&n,&reacquired,at+elapsed);
   assert(!n.local_gm&&!n.admission.eligible&&!n.hot_ready);
   assert(n.admission.boot_at==at);
  }
 }
 puts("PASS LoRa GM coordination: timing-only inhibition, forced root, exact direct admission,12 contact fences, positive HOT-to-ACTIVE withdrawal handover, HOT loss, self echo and original expiry; synthetic sessions only");
 return 0;
}

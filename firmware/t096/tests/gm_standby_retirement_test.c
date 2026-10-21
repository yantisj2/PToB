/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
/* Controlled actual-owner/wire counterexample. No native hardware or PA model.
 * Fresh independent source, established identity/map and five-second prior
 * READY/PT stability are fixture inputs. Final COMMIT confirmation is lost. */
#include "network.c"
#include <assert.h>
#include <stdio.h>
static struct r2_network initial(bool active)
{
 struct r2_network n;assert(!r2_network_init(&n,0,2,active?100:200,active?10:20,22,40000,70000000));
 n.group=(struct r2_group_config){.id=1,.epoch=1,.psk={1}};n.capabilities=3;n.gm5.enabled=true;n.gm_mask=n.gnss_mask=3;
 n.admission.eligible=n.admission.observing=true;n.admission.source_epoch=1;
 n.candidate=(struct v12_dc_coarse){.available=true,.generation=1,.valid_until_us=n.now+1500000};
 n.peer[1]=(struct r2_net_peer){.identity=active?200:100,.boot=active?20:10,.connected=true,.authenticated=true,.map_valid=true,.alive=n.now,.seen=n.now,.candidate_until=n.now+1500000,.candidate_epoch=1,.own_nonce=active?11:22,.nonce=active?22:11,.map_anchor=n.now};
 n.choice=active?0:1;n.root_boot=10;n.coordinator=active?1:0;n.coordinator_boot=20;
 n.view=n.candidate;n.publication=1;n.ready=true;n.local_gm=active;n.service_until=n.now+1500000;
 n.timing_valid=true;n.timing_boot=10;n.timing_epoch=1;n.timing_train=1;n.timing_at=n.now;
 n.gm5.role=active?R2_GM5_ACTIVE:R2_GM5_ACQUIRING;n.gm5.active=100;n.gm5.active_boot=10;n.gm5.active_until=n.now+R2_GM5_STATUS_US;
 n.gm5.selected=active?200:200;n.gm5.generation=n.management_generation=7;
 return n;
}
static void evidence(struct r2_network *n,uint64_t at,bool peer)
{
 n->now=at;n->candidate.valid_until_us=n->view.valid_until_us=n->service_until=at+1500000;n->timing_at=at;
 if(peer){n->peer[1].candidate_until=at+1500000;n->peer[1].seen=n->peer[1].alive=at;n->peer[1].map_anchor=at;}
}
static int deliver_record(struct r2_network *from,struct r2_network *to,struct r2_auth_record *r)
{
 struct r2_auth_record sent=*r;sent.sequence=++from->tx_sequence; /* same fresh-response stamping as network_next */
 uint8_t wire[R2_GROUP_CONTROL_MAX];assert(!encode_control(from,wire,&sent,from->peer[1].tx_key));
 return r2_network_receive(to,1,wire,r2_control_wire_size(wire),to->now,to->now);
}
struct late_pair {struct r2_network a,b;uint64_t deadline,late,issued,accepted;uint32_t generation;};
static struct late_pair late_pair(void)
{
 struct late_pair f={.a=initial(true),.b=initial(false)};struct r2_network *a=&f.a,*b=&f.b;
 f.deadline=a->now+R2_GM5_ACQUIRE_US;f.late=f.deadline-1000;
 a->gm5.attempt_started=b->gm5.attempt_started=a->now;a->gm5.attempt_until=b->gm5.attempt_until=f.deadline;
 evidence(a,f.late,true);evidence(b,f.late,true);a->gm5.stable_since=f.late-R2_GM5_STABLE_US;
 a->management_hash=descriptor(a); /* prior matching READY evidence, not a confirmed grant */
 struct r2_auth_record r=record(b,1,R2_AUTH_READY);r.sequence=++b->tx_sequence;r.decision=a->gm5.generation;
 r.coordinator_boot=20;r.source_epoch=1;r.source_second=descriptor(b);r.flags=1;assert(!deliver_record(b,a,&r));
 gm5_tick(a,true,f.late,NULL);assert(a->gm5.grant_pending&&a->gm5.issued_until==f.late+R2_GM5_GRANT_US);
 f.issued=a->gm5.issued_until;f.generation=a->gm5.generation;
 a->next_peer=3;a->peer[1].next_hello=a->peer[1].next_service=UINT64_MAX;a->peer[1].next_management=0;
 uint8_t peer,wire[R2_GROUP_CONTROL_MAX];bool hello;assert(r2_network_next_ready(a,2,&peer,wire,&hello)&&!hello&&peer==1);
 r2_network_sent(a,hello,peer,wire);assert(!r2_network_receive(b,1,wire,r2_control_wire_size(wire),f.late,f.late));
 assert(b->standby_authorized&&b->peer[1].response_pending&&b->peer[1].response.flags==5);f.accepted=b->gm5.grant_until;
 assert(f.accepted>f.deadline);return f;
}
static void held(const struct late_pair *f)
{
 const struct r2_network *a=&f->a;
 assert(a->gm5.selected==200&&r2_gm5_control_peer(a)==200&&a->local_gm&&a->gm5.grant_pending);
 assert(a->gm5.generation==f->generation&&a->gm5.issued_until==f->issued&&a->gm5.attempt_until==f->deadline);
}
static void ready_now(struct r2_network *a,struct r2_network *b,uint64_t at)
{
 evidence(a,at,true);evidence(b,at,true);
 struct r2_auth_record r=record(b,1,R2_AUTH_READY);r.decision=a->gm5.generation;
 r.coordinator_boot=b->boot;r.source_epoch=a->view.generation;r.source_second=descriptor(b);r.flags=1;
 assert(!deliver_record(b,a,&r));
}
static void confirmed_renewal_tests(void)
{
 for(unsigned change=0;change<4;change++){
  struct r2_network a=initial(true),b=initial(false);uint64_t start=a.now;
  a.hot_ready=a.management_committed=true;a.management_hash=descriptor(&a);a.management_until=start+R2_GM5_COORDINATION_US;
  a.gm5.issued_until=start+R2_GM5_GRANT_US-2000000;a.gm5.ready_at=start;
  uint64_t oldgrant=a.gm5.issued_until;uint32_t generation=a.gm5.generation;
  a.timing_valid=false;gm5_tick(&a,true,start,NULL);
  assert(!a.hot_ready&&a.gm5.attempt_until==start+R2_GM5_REPAIR_US&&!a.gm5.grant_pending);
  assert(a.gm5.issued_until==oldgrant&&a.gm5.generation==generation);
  if(change==1){a.publication++;b.publication++;a.timing_train=a.publication;}
  if(change==2){a.peer[1].boot++;b.boot++;a.coordinator_boot=b.boot;b.coordinator_boot=b.boot;}
  if(change==3){a.peer[1].candidate_epoch++;b.candidate.generation++;}
  a.timing_valid=true;uint64_t restored=start+1;
  ready_now(&a,&b,restored);gm5_tick(&a,true,restored,NULL);
  if(!change){
   assert(a.gm5.generation==generation+1&&a.gm5.grant_pending&&a.gm5.issued_until==restored+R2_GM5_GRANT_US);
   printf("PASS confirmed same descriptor renews1us after PT recovery without another5s dwell; old grant remains original until new generation\n");
  }else{
   assert(a.gm5.generation==generation&&!a.gm5.grant_pending&&a.gm5.issued_until==oldgrant&&!a.management_committed);
   ready_now(&a,&b,restored+R2_GM5_STABLE_US-1);gm5_tick(&a,true,a.now,NULL);
   assert(a.gm5.generation==generation&&!a.gm5.grant_pending&&a.gm5.issued_until==oldgrant);
   ready_now(&a,&b,restored+R2_GM5_STABLE_US);gm5_tick(&a,true,a.now,NULL);
   assert(a.gm5.generation==generation+1&&a.gm5.grant_pending&&a.gm5.issued_until==a.now+R2_GM5_GRANT_US);
   printf("PASS changed descriptor case%u requires full5s initial dwell after actual matching READY (publication/peerboot/peersourceepoch)\n",change);
  }
 }
}

static void partial_restart_tests(void) {
 for(unsigned phase=0;phase<3;phase++) {
  struct late_pair f=late_pair();
  struct r2_auth_record oldack=f.b.peer[1].response;
  oldack.sequence=++f.b.tx_sequence;
  uint8_t oldwire[R2_GROUP_CONTROL_MAX];
  assert(!encode_control(&f.b,oldwire,&oldack,f.b.peer[1].tx_key));
  struct r2_network issuer=f.a; /* Deliberately preserve every remote cache. */
  uint64_t at=f.late+1000;
  struct r2_network restarted;
  assert(!r2_network_init(&restarted,0,2,200,21,222,40000,at));
  restarted.group=f.a.group;restarted.gm5.enabled=true;restarted.capabilities=3;
  assert(!restarted.standby_authorized&&!restarted.hot_ready&&!restarted.gm5.grant_until);
  assert(!memcmp(&issuer,&f.a,sizeof issuer));
  /* A half-connected remote resets without delivering the local disconnect.
   * Prior caches cannot extend the finite original issuer obligation. */
  evidence(&f.a,at,false);gm5_tick(&f.a,true,at,NULL);held(&f);
  if(phase) {
   r2_network_link(&f.a,1,false,++at);
   if(phase==2)r2_network_link(&f.a,1,false,++at); /* idempotent late duplicate */
   assert(!f.a.peer[1].authenticated&&!f.a.peer[1].map_valid);
   assert(f.a.gm5.issued_until==f.issued&&f.a.gm5.generation==f.generation);
  }
  /* A proved replacement JOIN uses the existing one-peer cache replacement.
   * Do not clear the issuer's still-outstanding finite grant. */
  struct r2_join_exchange join={.self=100,.peer=200,.boot=10,.peer_boot=21,
   .nonce=333,.remote_nonce=444,.accepted=true,.control=true,.peer_capabilities=3};
  r2_network_joined(&f.a,1,&join,++at);f.a.now=at;
  assert(f.a.peer[1].boot==21&&!f.a.peer[1].map_valid&&!f.a.peer[1].candidate_until);
  assert(f.a.gm5.issued_until==f.issued&&f.a.gm5.generation==f.generation);
  for(unsigned duplicate=0;duplicate<2;duplicate++) {
   assert(r2_network_receive(&f.a,1,oldwire,r2_control_wire_size(oldwire),at,at));
   assert(f.a.gm5.grant_pending&&!f.a.hot_ready&&!f.a.peer[1].map_valid);
   assert(f.a.gm5.issued_until==f.issued&&f.a.gm5.generation==f.generation);
  }
  evidence(&f.a,f.issued+1,false);gm5_tick(&f.a,true,f.a.now,NULL);
  assert(!f.a.gm5.selected&&!f.a.hot_ready&&!r2_gm5_control_peer(&f.a));
  assert(!restarted.standby_authorized&&!restarted.hot_ready);
  printf("PASS successor-only reboot phase%u retains issuer caches/original grant; existing JOIN replaces one peer; duplicate old ACK rejected; expiry releases selection\n",phase);
 }
}

int main(void)
{
 partial_restart_tests();
 confirmed_renewal_tests();
 for(int edge=-1;edge<=1;edge++){
  struct late_pair f=late_pair();evidence(&f.a,f.deadline+edge,true);gm5_tick(&f.a,true,f.a.now,NULL);held(&f);
 }
 puts("PASS actual late-COMMIT/final-READY-loss retains original control owner at phase -1/at/+1; no attempt/grant/generation rolling");
 struct late_pair f=late_pair();evidence(&f.a,f.deadline+100000,true);gm5_tick(&f.a,true,f.a.now,NULL);held(&f);
 f.b.now=f.a.now;assert(!deliver_record(&f.b,&f.a,&f.b.peer[1].response));
 assert(f.a.hot_ready&&!f.a.gm5.grant_pending&&!f.a.gm5.attempt_until&&f.a.gm5.successes==1&&f.a.management_committed);
 assert(f.a.gm5.issued_until==f.issued&&f.a.gm5.generation==f.generation);
 puts("PASS actual final READY after original phase completes one acquisition and preserves original issued grant");
 f=late_pair();evidence(&f.a,f.deadline+100000,true);f.a.timing_valid=false;gm5_tick(&f.a,true,f.a.now,NULL);held(&f);
 struct r2_auth_record zero=f.b.peer[1].response;zero.flags=0;f.b.now=f.a.now;assert(!deliver_record(&f.b,&f.a,&zero));
 assert(f.a.gm5.grant_pending&&!f.a.gm5.ready_at&&!f.a.hot_ready);f.a.timing_valid=true;f.a.now++;f.a.timing_at=f.a.now;
 struct r2_auth_record final=f.b.peer[1].response;final.sequence++;f.b.now=f.a.now;assert(!deliver_record(&f.b,&f.a,&final));
 assert(f.a.hot_ready&&!f.a.gm5.grant_pending&&f.a.gm5.successes==1&&f.a.gm5.issued_until==f.issued);
 puts("PASS PT gap and READY.flags0 preserve original pending obligation; valid actual final proof recovers without reminting");
 f=late_pair();evidence(&f.a,f.deadline,true);gm5_tick(&f.a,true,f.a.now,NULL);held(&f);
 evidence(&f.a,f.issued-1,true);gm5_tick(&f.a,true,f.a.now,NULL);held(&f);
 evidence(&f.a,f.issued,true);gm5_tick(&f.a,true,f.a.now,NULL);
 assert(!f.a.gm5.selected&&!r2_gm5_control_peer(&f.a)&&f.a.gm5.failures==1&&f.a.gm5.retire_until==f.issued+R2_GM5_RETIRE_GUARD_US);
 assert(f.a.gm5.issued_until==f.issued&&f.a.gm5.generation==f.generation);
 evidence(&f.a,f.issued+1,true);gm5_tick(&f.a,true,f.a.now,NULL);assert(!f.a.gm5.selected&&f.a.gm5.failures==1);
 puts("PASS original pending issued expiry -1/at/+1 releases once, retains existing50ms retirement, and never rolls generation");
 f=late_pair();evidence(&f.a,f.deadline-500,true);f.a.peer[1].candidate_until=0;gm5_tick(&f.a,true,f.a.now,NULL);held(&f);
 puts("PASS uncertain missing peer source proof retains original outstanding owner before phase expiry");
 f=late_pair();evidence(&f.a,f.deadline-500,true);gm5_tick(&f.a,false,f.a.now,NULL);
 assert(!f.a.local_gm&&!f.a.ready&&!f.a.gm5.selected&&f.a.gm5.role==R2_GM5_COLD);
 puts("PASS actual own-source withdrawal immediately stops root service despite outstanding grant");
 f=late_pair();evidence(&f.a,f.deadline+100000,true);f.a.peer[1].candidate_epoch=2;gm5_tick(&f.a,true,f.a.now,NULL);held(&f);
 assert(!f.a.management_committed&&!f.a.gm5.stable_since);f.b.now=f.a.now;
 assert(deliver_record(&f.b,&f.a,&f.b.peer[1].response)==-ESTALE);
 struct r2_auth_record changed=f.b.peer[1].response;changed.sequence++;changed.source_second=descriptor(&f.a);
 assert(!deliver_record(&f.b,&f.a,&changed));assert(!f.a.hot_ready&&f.a.gm5.grant_pending&&f.a.gm5.issued_until==f.issued);
 f.a.next_peer=3;f.a.peer[1].next_management=0;uint8_t peer,wire[R2_GROUP_CONTROL_MAX];bool hello;
 assert(r2_network_next_ready(&f.a,2,&peer,wire,&hello)&&!hello);
 struct r2_auth_record out;char name[25];uint8_t caps;
 assert(!decode_control(&f.b,&out,wire,r2_control_wire_size(wire),1,f.b.peer[1].rx_key,name,&caps,NULL));assert(out.kind==R2_AUTH_PREPARE);
 puts("PASS changed descriptor rejects old final ACK and cannot confirm or retransmit old COMMIT under a new tuple");
 return 0;
}

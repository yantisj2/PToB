/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Compose real native admission/JOIN, STATUS and management wire bodies.
 * Public controller callbacks and initial source/map/PT are fixture inputs; actual source-loss ACTIVE exit is executed. */
#define R2_TIMING_EVENTS_TEST 1
#include "network.c"
#define main original_transport_main
#include "gm_standby_transport_test.c"
#undef main

static struct r2_network new_active(void)
{
 struct r2_network a;assert(!r2_network_init(&a,0,2,111,11,55,40000,n.now));
 a.group=n.group;a.gm5.enabled=true;a.capabilities=3;strcpy(a.name,"NewRoot");
 a.admission.eligible=a.admission.observing=true;a.admission.source_epoch=1;
 a.candidate=(struct v12_dc_coarse){.available=true,.generation=1,.valid_until_us=a.now+1500000};
 a.view=a.candidate;a.gm5.role=R2_GM5_ACTIVE;a.gm5.active=111;a.gm5.active_boot=11;
 a.gm5.selected=333;a.gm5.generation=1;a.choice=0;a.coordinator=1;a.root_boot=11;a.coordinator_boot=33;
 a.ready=a.local_gm=true;a.publication=1;a.service_until=a.now+1500000;
 a.peer[1]=(struct r2_net_peer){.identity=333,.boot=33,.connected=true,.authenticated=true,.map_valid=true,.seen=a.now,.alive=a.now,.candidate_until=a.now+1500000,.candidate_epoch=1,.own_nonce=22,.nonce=11};
 return a;
}
static int management(struct r2_network *issuer,struct r2_network *receiver,unsigned kind)
{
 issuer->now=receiver->now;
 struct r2_auth_record r=record(issuer,1,kind);
 r.sequence=++issuer->tx_sequence;r.decision=1;r.source_epoch=1;r.source_second=descriptor(receiver);
 r.coordinator_boot=33;r.flags=kind==R2_AUTH_COMMIT?1:0;
 if(r.flags)r.permit_until_us=issuer->now+R2_GM5_GRANT_US;
 uint8_t wire[R2_GROUP_CONTROL_MAX];assert(!encode_control(issuer,wire,&r,issuer->peer[1].tx_key));
 return r2_network_receive(receiver,1,wire,r2_control_wire_size(wire),receiver->now,receiver->now);
}
int main(void)
{
 reset();n.gm5.role=R2_GM5_ACTIVE;n.gm5.active=n.identity;n.gm5.active_boot=n.boot;
 n.choice=0;n.root_boot=n.boot;n.local_gm=n.ready=true;
 n.management_generation=3886;n.gm5.generation=3886;
 n.gm5.issued_until=n.now+90000000;uint64_t outstanding=n.gm5.issued_until;
 n.gm5.grant_generation=37;
 gm5_tick(&n,false,n.now,NULL);
 assert(n.gm5.role==R2_GM5_COLD&&!n.local_gm);
 assert(n.gm5.generation==3886&&n.gm5.issued_until==outstanding);
 assert(!n.management_generation&&!n.gm5.grant_generation);
 struct r2_join_exchange other=authenticate_client(true);
 assert(n.gm5.active==111&&n.gm5.active_boot==11);
 struct r2_network issuer=new_active();
 uint8_t status[R2_GM5_STATUS_BYTES];assert(!r2_gm5_status_encode(&issuer,&other,1,status));
 assert(!r2_gm5_status_receive(&n,&exchange,1,status,sizeof status,n.now));
 assert(n.gm5.role==R2_GM5_ACQUIRING);
 printf("ACTUAL_JOIN_STATUS active=%llu boot=%llu management_floor=%u newissuer_generation=%u own_outstanding_unchanged=%u\n",(unsigned long long)n.gm5.active,(unsigned long long)n.gm5.active_boot,n.management_generation,issuer.gm5.generation,n.gm5.issued_until==outstanding);
 /* Coherent current PT/source/session after the admitted connection. The
  * management exchange, not the synthetic timing input, must grant HOT. */
 n.choice=1;n.root_boot=11;n.coordinator=0;n.coordinator_boot=33;n.publication=1;
 n.view=(struct v12_dc_coarse){.available=true,.generation=1,.valid_until_us=n.now+1500000};
 n.ready=true;n.local_gm=false;n.service_until=n.now+1500000;
 n.timing_valid=true;n.timing_boot=11;n.timing_epoch=1;n.timing_train=1;n.timing_at=n.now;
 n.peer[1]=(struct r2_net_peer){.identity=111,.boot=11,.connected=true,.authenticated=true,.map_valid=true,.seen=n.now,.alive=n.now,.map_anchor=n.now,.candidate_until=n.now+1500000,.candidate_epoch=1,.own_nonce=11,.nonce=22};
 int prepare=management(&issuer,&n,R2_AUTH_PREPARE);
 int commit=management(&issuer,&n,R2_AUTH_COMMIT);
 assert(!prepare&&!commit&&n.hot_ready&&n.standby_authorized);
 assert(n.gm5.grant_generation==1&&n.management_generation==1);
 assert(n.gm5.generation==3886&&n.gm5.issued_until==outstanding);
 puts("PASS actual ACTIVE source exit -> native JOIN -> STATUS -> signed generation1 PREPARE/COMMIT reaches HOT after old issuer3886; own finite grant preserved; controller/PT fixtures, no hardware");
 return 0;
}

/* Actual admission owner: no controller operations, bootstrap scheduling only. */
#include "../src/group_admission.c"
#include <assert.h>
#include <stdio.h>
int r2_ble_admission_nonce(uint64_t *out){static uint64_t fixture_nonce=900;*out=++fixture_nonce;return 0;}
static bool scan_opportunity;
bool r2_ble_admission_scan_opportunity(void){bool v=scan_opportunity;scan_opportunity=false;return v;}
static bool needed,mode,urgent,transport_ready,server;static unsigned sends,promotions;static uint64_t transport_peer;static uint8_t sent_wire[R2_GROUP_JOIN_BYTES];
void r2_ble_client_bootstrap(bool value,bool priority){needed=value;urgent=priority;}
void r2_ble_group_client(bool value){mode=value;}
bool r2_ble_ready(unsigned p){return transport_ready;}
uint64_t r2_ble_peer_identity(unsigned p,uint8_t address[6]){return transport_peer;}
unsigned r2_ble_admission_peer(void){return 4;}
int r2_ble_admission_promote(void){++promotions;return -EAGAIN;}
void r2_ble_admission_release(void){transport_ready=false;}
bool r2_ble_admission_connected(void){return transport_ready;}
bool r2_ble_admission_server(void){return server;}
bool r2_ble_control_available(void){return true;}
bool r2_ble_control_member(void){return false;}
void r2_ble_admission_completed(uint64_t identity,uint64_t now){}
int r2_ble_admission_bind(uint64_t identity,const uint8_t address[6]){transport_peer=identity;return 0;}
int r2_ble_send(unsigned p,const uint8_t *data){++sends;memcpy(sent_wire,data,R2_GROUP_JOIN_BYTES);return 0;}
static void combined_capabilities(void){
 struct r2_network n={.group={.id=1,.epoch=1,.psk={1}},.identity=42,.boot=8,.capabilities=7,.self=0,.count=1,.profile_client=255};
 active=waiting=promote=control_denied=false;transport_ready=false;client=(struct r2_group_client){0};
 demand=(struct r2_local_demand){0};queued_demand=(struct r2_local_demand){0};
 r2_group_admission_tick(&n,1000000);assert(client.passive&&n.profile_client==0&&!control_available(&n));
 exchange=(struct r2_join_exchange){.peer_capabilities=7};poor_direct=true;
 assert(message(&n,R2_JOIN_PROOF,1000001).demand==1); /* cap7 may be leaf */
 n.admission.eligible=true;r2_group_admission_tick(&n,1000002);
 assert(!client.passive&&n.profile_client==255&&control_available(&n));
 /* A currently admitted one-hop leaf cannot grant a second intermediary. */
 demand=(struct r2_local_demand){.until=2000000,.received=false};received_request=true;
 assert(message(&n,R2_JOIN_ACCEPT,1000003).demand==3);
 n.local_gm=n.ready=true;n.choice=0;n.root_boot=n.boot;n.publication=1;
 n.service_until=2000000;n.view.available=n.view.relative_ready=true;
 n.view.generation=1;n.view.evidence_us=1000000;n.view.utc_second=1791504000;
 struct r2_join_record direct=message(&n,R2_JOIN_ACCEPT,1000004);
 assert(direct.demand==3&&direct.source_flags&&direct.root==n.identity);
 exchange.server=exchange.control=false;struct r2_join_record cold={.capabilities=7};
 accepted_peer(&n,&cold,1000004,1000005);
 assert(!client.passive&&!control_denied&&n.profile_client==255);

 demand=(struct r2_local_demand){0};received_request=false;poor_direct=false;
 puts("PASS cap7 cold passive to source-qualified control; relay capability may request; depth2 refusal");
}
static void population(void){
 struct r2_network n={.group={.id=1,.epoch=1,.psk={1,2,3}},.identity=20,.boot=200,.self=0,.count=4,.capabilities=1,.map_bound_us=40000,.members_mask=15,.ready=true,.choice=0,.root_boot=200,.publication=1};
 n.admission.eligible=true;strcpy(n.name,"server");n.view.available=n.view.relative_ready=true;n.view.generation=1;n.view.utc_second=1791500000;
 client=(struct r2_group_client){0};active=waiting=promote=false;retry_at=close_at=0;server=true;
 for(unsigned i=0;i<10;i++){
  uint64_t now=200000000+i*1000000;transport_ready=true;transport_peer=1000+(i%9);
  n.service_until=now+1200000;n.view.evidence_us=now;
  struct r2_join_record r={.kind=R2_JOIN_REQUEST,.capabilities=7,.sender=transport_peer,.receiver=20,.sender_boot=100+i,.client_nonce=100+i,.sequence=1,.created_us=now};
  strcpy(r.name,i==9?"renamed":"same label");
  struct r2_ble_event e={.peer=4,.length=R2_GROUP_JOIN_BYTES,.at=now};assert(!r2_group_join_encode(e.data,&r,&n.group));
  r2_group_admission_receive(&n,&e,now);r2_group_admission_tick(&n,now+1000);
  struct r2_join_record reply;assert(!r2_group_join_decode(&reply,sent_wire,R2_GROUP_JOIN_BYTES,&n.group));assert(reply.kind==R2_JOIN_CHALLENGE&&!reply.control);
  r.kind=R2_JOIN_PROOF;r.receiver_boot=200;r.server_nonce=reply.server_nonce;r.echo_us=reply.created_us;r.created_us=now+2000;r.sequence=2;e.at=r.created_us;
  assert(!r2_group_join_encode(e.data,&r,&n.group));r2_group_admission_receive(&n,&e,e.at);r2_group_admission_tick(&n,now+3000);
  assert(!r2_group_join_decode(&reply,sent_wire,R2_GROUP_JOIN_BYTES,&n.group));assert(reply.kind==R2_JOIN_ACCEPT&&!reply.control&&reply.source_flags);
  struct r2_join_time anchor;assert(r2_group_join_time(&anchor,&reply,now+2000,now+4000,40000));
  assert(n.members_mask==15&&!promote&&!promotions&&!client.passive);
  r2_group_admission_tick(&n,now+104000);assert(!active&&!transport_ready);
 }
 unsigned names=0;bool renamed=false;for(unsigned i=0;i<8;i++){names+=recent[i].identity!=0;renamed|=recent[i].identity==1000&&!strcmp(recent[i].name,"renamed");}
 assert(names==8&&renamed&&n.members_mask==15);
 puts("PASS actual admission owner: nine distinct passive clients plus rename, valid signed causal anchors, full four-GM mask unchanged, bounded eight-name cache evicts without admission loss");
}
static void local_demand_test(void){
 struct r2_network n={.group={.id=1,.epoch=1,.psk={1,2,3}},.identity=99999,.boot=200,.self=0,.count=4,.capabilities=4,.map_bound_us=40000,.members_mask=0};
 strcpy(n.name,"relay");client=(struct r2_group_client){0};active=waiting=promote=false;retry_at=close_at=0;server=true;
 uint64_t now=300000000;transport_ready=true;transport_peer=77777;
 timing_context=(struct r2_group_client){.source=true,.root=111,.boot=9,.source_epoch=4,.publication=99,.at=now,.until=now+1000000,.map_until=now+50000,.utc_us=1791500000000000ULL};
 struct r2_join_record r={.kind=R2_JOIN_REQUEST,.sender=77777,.receiver=99999,.sender_boot=11,.client_nonce=8,.sequence=1,.created_us=now};strcpy(r.name,"client");
 struct r2_ble_event e={.peer=4,.length=R2_GROUP_JOIN_BYTES,.at=now};assert(!r2_group_join_encode(e.data,&r,&n.group));
 r2_group_admission_receive(&n,&e,now);r2_group_admission_tick(&n,now+1000);
 struct r2_join_record reply;assert(!r2_group_join_decode(&reply,sent_wire,R2_GROUP_JOIN_BYTES,&n.group));
 r.kind=R2_JOIN_PROOF;r.receiver_boot=200;r.server_nonce=reply.server_nonce;r.echo_us=reply.created_us;r.created_us=now+2000;r.sequence=2;
 r.demand=1;r.root=111;r.root_boot=9;r.source_epoch=4;r.publication=5; /* compatible older publication */
 e.at=r.created_us;assert(!r2_group_join_encode(e.data,&r,&n.group));r2_group_admission_receive(&n,&e,e.at);r2_group_admission_tick(&n,now+3000);
 assert(demand.received&&demand.peer==77777&&demand.until==now+30003000&&!n.members_mask&&!n.ready);
 uint64_t until=demand.until;r.sequence++;e.at++;assert(!r2_group_join_encode(e.data,&r,&n.group));r2_group_admission_receive(&n,&e,e.at);assert(demand.until==until);
 /* Quiet snapshots never manufacture missed listening opportunities. */
 demand=(struct r2_local_demand){0};n.capabilities=0;client=(struct r2_group_client){.passive=true,.source=true,.root=111,.boot=9,.source_epoch=4,.publication=5,.at=now,.until=now+1000,.uncertainty_us=40000};
 exchange.control=false;exchange.server=false;exchange.peer_capabilities=4;poor_direct=false;missed_opportunities=0;direct_accepted=0;
 for(unsigned i=0;i<10;i++){struct r2_join_record q=message(&n,R2_JOIN_PROOF,now+2000);assert(!q.demand);}
 r2_group_admission_direct_opportunity(0);r2_group_admission_direct_opportunity(0);assert(!poor_direct);
 r2_group_admission_direct_opportunity(0);assert(poor_direct);
 struct r2_join_record q=message(&n,R2_JOIN_PROOF,now+2000);assert(q.demand&&!q.source_flags&&!q.remaining_us);
 r2_group_admission_direct_opportunity(1);assert(!poor_direct);
 /* A fresh timing response alone is not an admission ACK. */
 timing_context.map_until=now;received_request=true;
 q=message(&n,R2_JOIN_ACCEPT,now+4000);assert(q.demand==3&&!q.source_flags);
 n.capabilities=0;active=true;sent_demand=true;poor_direct=true;demand=(struct r2_local_demand){0};
 exchange=(struct r2_join_exchange){.self=n.identity,.peer=88888,.boot=n.boot,.peer_boot=10,
  .nonce=1,.remote_nonce=2,.until=now+5000000,.proof_at=now,.challenged=true,.peer_capabilities=4};
 q=(struct r2_join_record){.kind=R2_JOIN_ACCEPT,.capabilities=4,.sender=88888,.receiver=n.identity,
  .sender_boot=10,.receiver_boot=n.boot,.client_nonce=1,.server_nonce=2,.sequence=3,.created_us=now+1000,
  .echo_us=now,.root=111,.root_boot=9,.source_epoch=4,.publication=5,.demand=3};strcpy(q.name,"relay");
 e.at=now+2000;assert(!r2_group_join_encode(e.data,&q,&n.group));r2_group_admission_receive(&n,&e,e.at);
 assert(!demand.at&&client.source); /* refusal preserves bounded anchor */

 puts("PASS JOINv2 local demand: unlisted IDs, no GM service/membership, old compatible publication, replay no renewal, three actual missed opportunities, quiet no demand, context not permission");
}
static void pending_after_coarse(void){
 struct r2_network n={.group={.id=1,.epoch=1,.psk={1,2,3}},.identity=99999,.boot=200,.capabilities=4,.map_bound_us=40000};
 strcpy(n.name,"relay");uint64_t now=400000000;
 active=waiting=promote=false;retry_at=close_at=0;client=(struct r2_group_client){0};demand=queued_demand=(struct r2_local_demand){0};
 struct r2_group_client context={.source=true,.root=111,.boot=9,.source_epoch=4,.publication=99,.at=now,
  .until=now+1000000,.map_until=now+50000,.utc_us=1791500000000000ULL};
 r2_group_admission_timing(&context);context.source=false;context.map_until=0;r2_group_admission_timing(&context);
 now+=100000;transport_ready=true;transport_peer=77777;server=true;
 struct r2_join_record r={.kind=R2_JOIN_REQUEST,.capabilities=7,.sender=transport_peer,.receiver=n.identity,.sender_boot=44,.client_nonce=91,.sequence=1,.created_us=now};strcpy(r.name,"client");
 struct r2_ble_event e={.peer=4,.length=R2_GROUP_JOIN_BYTES,.at=now};assert(!r2_group_join_encode(e.data,&r,&n.group));
 r2_group_admission_receive(&n,&e,now);r2_group_admission_tick(&n,now+1000);
 struct r2_join_record reply;assert(!r2_group_join_decode(&reply,sent_wire,sizeof sent_wire,&n.group));
 r.kind=R2_JOIN_PROOF;r.receiver_boot=n.boot;r.server_nonce=reply.server_nonce;r.echo_us=reply.created_us;r.created_us=now+2000;r.sequence=2;
 r.demand=1;r.root=111;r.root_boot=9;r.source_epoch=4;r.publication=5;e.at=r.created_us;
 assert(!r2_group_join_encode(e.data,&r,&n.group));r2_group_admission_receive(&n,&e,e.at);r2_group_admission_tick(&n,now+3000);
 assert(!r2_group_join_decode(&reply,sent_wire,sizeof sent_wire,&n.group));
 assert(reply.demand==4&&reply.source_flags&&reply.source_bound_us>40000&&!demand.at&&queued_demand.until==now+30003000);
 uint64_t original=queued_demand.until;
 transport_ready=false;r2_group_admission_tick(&n,now+200000);assert(!demand.at&&queued_demand.until==original);
 context.source=true;context.at=now+300000;context.map_until=context.at+50000;
 r2_group_admission_timing(&context);r2_group_admission_tick(&n,context.at);
 assert(demand.received&&demand.until==original&&!queued_demand.until&&demand.at==context.at);
 queued_demand=demand;context.boot++;r2_group_admission_timing(&context);assert(!queued_demand.until&&coarse_context.source);
 queued_demand=demand;n.local_gm=true;r2_group_admission_tick(&n,context.at+1000);assert(!queued_demand.until);
 context.source=false;r2_group_admission_timing(&context);received_request=false;
 reply=message(&n,R2_JOIN_ACCEPT,context.until);assert(!reply.source_flags); /* expiry cannot become freshness */
 /* No UTC is necessary to queue service. First actual map binds scope once. */
 timing_context=coarse_context=(struct r2_group_client){0};n.local_gm=false;
 queued_demand=(struct r2_local_demand){.peer=77777,.peer_boot=44,.at=now,.until=now+30000000,.received=true};
 demand=(struct r2_local_demand){0};r2_group_admission_tick(&n,now+1000);
 assert(queued_demand.until&&!demand.at&&!queued_demand.root);
 context.source=true;context.at=now+2000;context.map_until=now+52000;context.until=now+1000000;
 r2_group_admission_timing(&context);assert(queued_demand.until);
 r2_group_admission_tick(&n,now+2000);assert(demand.root==context.root&&demand.until==now+30000000&&!queued_demand.until);
 demand=(struct r2_local_demand){0};client=(struct r2_group_client){.passive=true};n.capabilities=0;
 exchange.server=exchange.control=false;exchange.peer_capabilities=4;poor_direct=false;
 missed_opportunities=0;
 reply=message(&n,R2_JOIN_PROOF,now+3000);assert(!reply.demand);
 transport_ready=false;
 for(unsigned i=0;i<3;i++){scan_opportunity=true;r2_group_admission_tick(&n,now+3000+i);}
 reply=message(&n,R2_JOIN_PROOF,now+3003);assert(reply.demand==1&&!reply.root&&!reply.source_flags);
 n.capabilities=4;received_request=true;timing_context=coarse_context=(struct r2_group_client){0};
 reply=message(&n,R2_JOIN_ACCEPT,now+3000);assert(reply.demand==4&&!reply.root&&!reply.source_flags);
 uint8_t wire[R2_GROUP_JOIN_BYTES];assert(!r2_group_join_encode(wire,&reply,&n.group));
 puts("PASS coarse bootstrap explicit uncertainty; PENDING emits no live demand until matching map returns, original expiry retained, boot/GM cancel");
}
static void local_map_anchor(void){
 struct r2_network n={.map_bound_us=40000,.capabilities=7};
 client=(struct r2_group_client){.source=true,.passive=false,.peer=999,.peer_boot=20,
  .root=111,.boot=10,.source_epoch=3,.at=1,.until=2,.uncertainty_us=90000};
 timing_context=(struct r2_group_client){.source=true,.root=111,.boot=10,.source_epoch=3,
  .publication=17,.at=1000000,.map_until=1050000,.until=2000000,.utc_us=1791500000000000ULL};
 refresh_required=true;anchor_from_live_map(&n,1001000);
 assert(client.at==1001000&&client.utc_us==timing_context.utc_us+1000&&
  client.uncertainty_us==40066&&client.until==1959934&&!refresh_required);
 assert(!client.passive&&client.peer==999&&client.peer_boot==20&&client.publication==17);
 uint64_t original_until=client.until;uint32_t original_bound=client.uncertainty_us;
 anchor_from_live_map(&n,1001000);assert(client.until==original_until&&client.uncertainty_us==original_bound);
 anchor_from_live_map(&n,1002000);assert(client.until<original_until&&client.uncertainty_us>original_bound);
 assert(client.until<timing_context.until); /* repeated use never renews that sample */
 struct r2_group_client before=client;
 timing_context.source=false;anchor_from_live_map(&n,1002000);assert(!memcmp(&before,&client,sizeof client));
 timing_context.source=true;timing_context.boot++;anchor_from_live_map(&n,1002000);assert(!memcmp(&before,&client,sizeof client));
 timing_context.boot--;anchor_from_live_map(&n,1050000);assert(!memcmp(&before,&client,sizeof client));
 timing_context.until=1040000;anchor_from_live_map(&n,1002000);assert(!memcmp(&before,&client,sizeof client));
 client.source=false;timing_context.until=2000000;anchor_from_live_map(&n,1002000);assert(!client.source);
 client=(struct r2_group_client){0};timing_context=(struct r2_group_client){0};refresh_required=false;
 puts("PASS live same-scope map refreshes bounded coarse anchor; no role change, cold bootstrap, expired map or scope bypass");
}
int main(void){
 local_map_anchor();
 struct r2_network n={.group={.id=1},.identity=10,.boot=100,.self=0,.profile_client=255,.map_bound_us=40000};
 r2_group_admission_tick(&n,1000);assert(client.passive&&needed&&mode&&!sends&&n.profile_client==0);
 /* Accepted freshness is independent of elapsed original source permission.
  * No repeated bootstrap at3s/8s, no GM seat, no relay activation mutation. */
 client=(struct r2_group_client){.passive=true,.source=true,.at=1000000,.until=2000000,.uncertainty_us=40000,.utc_us=1791500000000000ULL,.root=20,.boot=200,.peer=20,.peer_boot=200};
 uint64_t deadline=r2_group_anchor_deadline(client.at,client.uncertainty_us);
 assert(deadline==105900000);
 for(uint64_t t=1000000;t<30950000;t+=1000000){r2_group_admission_tick(&n,t);assert(!needed&&!urgent&&!sends&&!n.members_mask);}
 r2_group_admission_tick(&n,30950000);assert(needed&&!urgent);
 r2_group_admission_tick(&n,43449999);assert(needed&&!urgent);
 r2_group_admission_tick(&n,43450000);assert(needed&&urgent);
 struct r2_join_time a={.utc_us=client.utc_us,.uncertainty_us=client.uncertainty_us};uint64_t upper;
 assert(r2_group_coarse_upper(&a,client.at,deadline-1,&upper));
 assert(!r2_group_coarse_upper(&a,client.at,deadline,&upper));
 r2_group_admission_require_anchor();assert(!client.source&&needed);
 /* Missing-source ACCEPT cannot resurrect a scope or reset uncertainty age. */
 exchange=(struct r2_join_exchange){.peer=20,.peer_boot=201};
 struct r2_join_record empty={0};accepted_peer(&n,&empty,deadline,deadline+100);
 assert(client.passive&&!client.source&&client.at==1000000&&!n.members_mask);
 /* Causal control bootstrap stores time without changing GM/control role. */
 n.capabilities=3;n.admission.eligible=true;n.profile_client=255;client=(struct r2_group_client){0};mode=false;
 exchange.control=true;
 struct r2_join_record timed={.kind=R2_JOIN_ACCEPT,.sender=20,.receiver=10,.sender_boot=201,.receiver_boot=100,.client_nonce=1,.server_nonce=2,.sequence=1,.source_flags=1,.source_bound_us=40000,.remaining_us=1000000,.root=20,.root_boot=201,.source_epoch=1,.publication=1,.utc_us=1791500000000000ULL};
 strcpy(timed.name,"root");
 accepted_peer(&n,&timed,1000000,1001000);
 assert(client.source&&!client.passive&&!mode&&n.profile_client==255);
 combined_capabilities();population();local_demand_test();pending_after_coarse();
 n.capabilities=0;
 client=(struct r2_group_client){.passive=true,.source=true,.at=1000000,.until=1500000,.uncertainty_us=40000,.utc_us=1791500000000000ULL};
 r2_group_admission_refresh_anchor();r2_group_admission_tick(&n,4000000);assert(client.source&&client.until==1500000&&needed&&urgent);
 puts("PASS passive bootstrap scheduling: actual uncertainty deadline, 100ms refresh/125ms urgency, no3s/8s renewal, no GM membership, fail-closed scope invalidation");
}

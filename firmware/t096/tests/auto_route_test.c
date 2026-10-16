/* Actual discovery and JOIN owners; no controller/RF/UTC qualification. */
#include "ble_transport_fake.h"
static int sys_csrand_get(void *out,size_t n){memset(out,0x5a,n);return 0;}
#include "../protocol/ble_transport.c"
#include "../src/group_admission.c"
#include <stdio.h>
static int bt_conn_le_create(const bt_addr_le_t *a,const void *b,const void *c,struct bt_conn **out){return -EBUSY;}
void r2_network_joined(struct r2_network *n,unsigned slot,const struct r2_join_exchange *e,uint64_t now){assert(0);}
static void advertisement(uint64_t id,bool passive,int8_t rssi,uint64_t now){
 fake_time=now;bt_addr_le_t addr={.type=BT_ADDR_LE_RANDOM};addr.a.val[0]=(uint8_t)id;
 uint8_t wire[25]={24,BT_DATA_MANUFACTURER_DATA,'G','1',1};if(passive)wire[4]|=64;
 r2_group_put(wire+5,7,8);r2_group_put(wire+13,1,4);r2_group_put(wire+17,id,8);
 struct net_buf_simple data={.data=wire,.len=sizeof wire};scan_report(&addr,rssi,BT_GAP_ADV_TYPE_ADV_IND,&data);
}
static void reset(void){
 memset(peers,0,sizeof peers);peers[0].identity=333;memset(candidates,0,sizeof candidates);candidate_since=0;
 group_id=7;group_epoch=1;local_identity=333;nodes=5;self=0;group_client=true;field_profile=false;
 recent_admission=recent_admission_until=0;stopping=sync_acquiring=scanning=advertising=precision_discovery=false;
 r2_group_admission_reset();
}
static struct r2_join_record gm_time(uint64_t now){
 struct r2_join_record r={.kind=R2_JOIN_ACCEPT,.capabilities=3,.sender=111,.sender_boot=1000,.receiver=333,.receiver_boot=33,.client_nonce=1,.server_nonce=2,.sequence=2,.name="GM",.root=111,.root_boot=1000,.source_epoch=1,.publication=1,.utc_us=1791504000000000ULL,.remaining_us=1000000,.source_flags=1,.source_bound_us=40000,.created_us=now};return r;
}
static void discovery(void){
 reset();r2_ble_admission_policy(0,true);
 advertisement(111,false,-95,1000000);advertisement(222,true,-40,1010000);
 candidate_select(1030000);assert(!peers[4].identity);
 candidate_select(1040000);assert(peers[4].identity==222);
 r2_ble_admission_policy(222,true);advertisement(111,false,-20,1050000);candidate_select(1050000);assert(peers[4].identity==222);
 struct bt_conn owned_connection={0};peers[4].conn=&owned_connection;
 advertisement(223,true,-15,1060000);r2_ble_admission_policy(0,true);candidate_select(1060000);assert(peers[4].identity==222);
 peers[4].conn=NULL;candidate_select(1060000);assert(peers[4].identity==223);
 candidate_cooldown(223,1060000,2000000);candidate_select(1060000);assert(peers[4].identity==222);
 r2_ble_admission_completed(222,1060000);candidate_select(1060000);assert(peers[4].identity==111);
 advertisement(222,true,-35,1070000);advertisement(223,true,-25,1070000);candidate_select(1070000);assert(peers[4].identity==111);
 r2_ble_admission_policy(222,true);candidate_select(1070000);assert(!peers[4].advertised_at); /* No fallback through a live pinned lease. */
 advertisement(222,true,-35,6070001);candidate_select(6070001);assert(peers[4].identity==222);
 advertisement(223,true,-10,6070001);r2_ble_admission_policy(0,false);advertisement(111,false,-95,6070001);candidate_select(6070001);assert(peers[4].identity==111);
 r2_ble_admission_completed(111,6070001);r2_ble_admission_anchor_deadline(111,6170001);assert(candidates[0].cooldown==6170001);r2_ble_admission_anchor_deadline(111,6370001);assert(candidates[0].cooldown==6170001);
 candidate_select(6270001);assert(!peers[4].advertised_at); /* stale discovery is not a retry */
 puts("PASS candidate order/RSSI, role-aware seeking, authenticated parent pin, owner exclusion, independent cooldowns, healthy direct preference and stale rejection");
}
static void health(void){
 reset();struct r2_link_quality_view q={.received=16,.last_rx=1000000};
 r2_group_admission_direct_quality(&q,q.last_rx);assert(!poor_direct); /* map can be unavailable */
 q.poor=true;r2_group_admission_direct_quality(&q,q.last_rx);assert(poor_direct);
 struct r2_network n={.identity=333,.map_bound_us=40000,.capabilities=6};
 exchange=(struct r2_join_exchange){.peer=111,.peer_boot=1000};struct r2_join_record r=gm_time(13000000);
 accepted_peer(&n,&r,13000000,13020000);assert(client.source&&poor_direct);
 r2_group_admission_direct_opportunity(999,14000000);assert(poor_direct);
 q.poor=false;r2_group_admission_direct_quality(&q,q.last_rx);assert(poor_direct); /* one packet does not clear loss */
 q.healthy=true;r2_group_admission_direct_quality(&q,q.last_rx);assert(!poor_direct);
 reset();
 r2_group_admission_direct_opportunity(0,1000000);
 r2_group_admission_direct_opportunity(0,2000000);
 r2_group_admission_direct_opportunity(0,3000000);assert(!poor_direct);
 r2_group_admission_direct_opportunity(0,4000000);assert(poor_direct);
 q=(struct r2_link_quality_view){.received=16,.last_rx=5000000,.healthy=true};
 r2_group_admission_direct_quality(&q,q.last_rx);assert(!poor_direct);
 r2_group_admission_direct_opportunity(0,5100000);assert(!poor_direct);
 r2_group_admission_direct_quality(&q,9000000);
 r2_group_admission_direct_opportunity(0,9000000);
 r2_group_admission_direct_quality(&q,10000000);
 r2_group_admission_direct_opportunity(0,10000000);
 r2_group_admission_direct_quality(&q,11000000);
 r2_group_admission_direct_opportunity(0,11000000);
 r2_group_admission_direct_quality(&q,12000000);
 r2_group_admission_direct_opportunity(0,12000000);assert(poor_direct); /* stale good mailbox cannot hide absence */
 puts("PASS actual delivery hysteresis independent of map warmup and coarse JOIN, conservative cold discovery");
}

static void renewals(void){
 reset();struct r2_network n={.identity=333,.boot=33,.capabilities=6,.map_bound_us=40000,.group={.id=7,.epoch=1,.psk={1}}};
 client.passive=true;demand=(struct r2_local_demand){111,1000,222,2000,1000000,361000000,1,false};
 exchange=(struct r2_join_exchange){.peer=222,.peer_boot=2000,.peer_capabilities=6,.server=false};
 assert(!message(&n,R2_JOIN_PROOF,300999999).demand); /* No new short demand lease/renewal. */
 for(unsigned i=0;i<20;i++){
  uint64_t now=301000000+300000000ULL*i;poor_direct=false; /* selected relay does not need failed direct evidence to renew */
  exchange=(struct r2_join_exchange){.peer=222,.peer_boot=2000,.peer_capabilities=6,.server=false};
  struct r2_join_record proof=message(&n,R2_JOIN_PROOF,now);assert(proof.demand==1&&!proof.source_flags&&!proof.root);
  demand.at=now;demand.until=now+R2_LOCAL_DEMAND_LEASE_US;r2_group_admission_tick(&n,now+R2_LOCAL_DEMAND_RENEW_US);assert(admission_parent==222&&admission_seek_relay&&bootstrap_needed);
 }
 struct r2_local_demand saved=demand;
 r2_group_admission_parent_unavailable(999,demand.at+2000000);assert(demand.until==saved.until);
 r2_group_admission_parent_unavailable(222,demand.at+2000000);assert(!demand.until&&!admission_parent&&bootstrap_needed&&bootstrap_urgent);
 demand=saved;
 demand.received=true;r2_group_admission_parent_unavailable(222,demand.at+2000000);assert(demand.until==saved.until);demand.received=false;
 /* Lease expiry permits new discovery; a leaf cannot accept second-hop demand. */
 r2_group_admission_tick(&n,demand.until);assert(!admission_parent);
 received_request=true;relay_allowed=false;exchange.server=true;struct r2_join_record refusal=message(&n,R2_JOIN_ACCEPT,demand.until);assert(refusal.demand==3&&!refusal.source_flags);
 queued_demand=(struct r2_local_demand){.peer=444,.peer_boot=4000,.until=demand.until+30000000,.received=true};
 timing_context=(struct r2_group_client){.source=true,.root=111,.boot=1000,.source_epoch=1,.publication=1,.at=demand.until,.until=demand.until+1000000,.map_until=demand.until+500000};
 relay_allowed=false;r2_group_admission_tick(&n,demand.until);assert(!queued_demand.until);
 relay_allowed=true;received_request=true;exchange.server=true;exchange.peer=111;
 refusal=message(&n,R2_JOIN_ACCEPT,demand.until);assert(refusal.demand==3&&!refusal.source_flags);
 queued_demand=(struct r2_local_demand){.peer=111,.peer_boot=1000,.until=demand.until+30000000,.received=true};r2_group_admission_tick(&n,demand.until);assert(!queued_demand.until);
 poor_direct=true;n.capabilities=3;exchange.server=exchange.control=false;exchange.peer=222;exchange.peer_capabilities=6;
 struct r2_join_record proof=message(&n,R2_JOIN_PROOF,demand.until);assert(!proof.demand); /* GM mode has no alternate relay owner. */
 puts("PASS twenty five-minute renewal decisions across100min, no direct-health dependency or GM bounce, exact lease expiry and depth2 refusal");
}
static void authenticated_pending(void){
 for(unsigned pending=0;pending<2;pending++){
  reset();uint64_t now=1000000;struct r2_network n={.identity=333,.boot=33,.capabilities=6,.map_bound_us=40000,.group={.id=7,.epoch=1,.psk={1}}};
  client.passive=true;active=true;sent_demand=true;
  assert(!r2_group_join_begin(&exchange,false,333,222,33,1,now));exchange.request_at=now;
  struct r2_join_record r={.kind=R2_JOIN_CHALLENGE,.capabilities=6,.sender=222,.receiver=333,.sender_boot=2000,.receiver_boot=33,.client_nonce=1,.server_nonce=2,.sequence=1,.created_us=now+1000,.echo_us=now,.name="RC"};
  assert(!r2_group_join_receive(&exchange,&r,now+2000));exchange.proof_at=now+3000;
  r.kind=R2_JOIN_ACCEPT;r.sequence=2;r.created_us=now+4000;r.echo_us=exchange.proof_at;
  r.root=111;r.root_boot=1000;r.source_epoch=1;r.publication=1;r.utc_us=1791504000000000ULL;
  r.remaining_us=1000000;r.source_flags=1;r.source_bound_us=40000;r.demand=pending?4:2;
  struct r2_ble_event e={.peer=4,.length=R2_GROUP_JOIN_BYTES,.at=now+5000};assert(!r2_group_join_encode(e.data,&r,&n.group));
  e.data[50]^=1;r2_group_admission_receive(&n,&e,e.at);assert(!exchange.accepted&&!demand.until);e.data[50]^=1;
  r2_group_admission_receive(&n,&e,e.at);assert(exchange.accepted&&client.source);
  if(pending){
   assert(!demand.until&&pending_parent==222&&pending_parent_boot==2000&&pending_parent_until==e.at+R2_LOCAL_DEMAND_LEASE_US);
   for(unsigned i=0;i<50;i++){struct r2_link_quality_view q={.received=i+1,.last_rx=now+10000+i*62500,.healthy=i==49};r2_group_admission_direct_quality(&q,q.last_rx);} /* actual RX continuity before estimator warmup */
   r2_group_admission_tick(&n,now+10000+49*62500);assert(admission_parent==222&&bootstrap_needed);
   assert(!message(&n,R2_JOIN_PROOF,now+10000+49*62500).demand);
   queued_demand=(struct r2_local_demand){.peer=222,.peer_boot=2000,.until=now+R2_LOCAL_DEMAND_LEASE_US,.received=true};
   demand=(struct r2_local_demand){.peer=999,.peer_boot=9999,.until=now+R2_LOCAL_DEMAND_LEASE_US,.received=true};exchange.server=true;exchange.control=false;
   r.kind=R2_JOIN_PROOF;r.demand=0;r.sender_boot=2001;accepted_peer(&n,&r,now,now+1000);assert(queued_demand.until&&demand.until);
   r.sender_boot=2000;uint8_t bytes[R2_GROUP_JOIN_BYTES];struct r2_join_record signed_proof;
   assert(!r2_group_join_encode(bytes,&r,&n.group)&&!r2_group_join_decode(&signed_proof,bytes,sizeof bytes,&n.group));
   accepted_peer(&n,&signed_proof,now,now+2000);assert(!queued_demand.until&&demand.until);
  }else assert(demand.peer==222&&demand.peer_boot==2000&&demand.root==111&&demand.until==e.at+R2_LOCAL_DEMAND_LEASE_US);
 }
 reset();struct r2_network n={.identity=333,.boot=33,.capabilities=6,.map_bound_us=40000,.group={.id=7,.epoch=1,.psk={1}}};
 client=(struct r2_group_client){.passive=true,.source=true,.root=111,.boot=1000,.source_epoch=1,.publication=1,.at=1000000,.until=2000000,.uncertainty_us=40000};
 r2_group_admission_tick(&n,2000000);assert(bootstrap_needed&&bootstrap_urgent); /* Original source expiry beats coarse-error budget. */
 puts("PASS real signed JOIN: corrupt ACCEPT refused, PENDING retains direct owner, LIVE binds authenticated parent/root/boot and original lease");
}
static void pending_scope(void){
 reset();
 struct r2_group_client c={.passive=true,.root=111,.boot=1000,.source_epoch=1,.publication=1,.at=1000000};
 r2_group_admission_timing(&c);
 queued_demand=(struct r2_local_demand){.peer=222,.peer_boot=2000,.root=111,.root_boot=1000,.source_epoch=1,.at=1000001,.until=361000001,.received=true};
 /* A second no-map snapshot of the SAME known scope is not a new root. */
 c.at+=20000;r2_group_admission_timing(&c);assert(queued_demand.until==361000001);
 struct r2_network n={.identity=333,.capabilities=6,.group={.id=7}};
 client.passive=true;
 /* Loss of the currently available map/anchor can produce an empty owner
  * snapshot. It cannot discard the bounded request before recovery. */
 c=(struct r2_group_client){0};r2_group_admission_timing(&c);r2_group_admission_tick(&n,1100000);
 assert(queued_demand.until==361000001);
 c=(struct r2_group_client){.passive=true,.root=111,.boot=1000,.source_epoch=1,.publication=1,.at=1200000};
 r2_group_admission_timing(&c);
 c.boot++;r2_group_admission_timing(&c);assert(!queued_demand.until);
 puts("PASS pending demand survives same-scope map loss; authenticated root change fences it");
}
static void live_publication(void){
 reset();struct r2_network n={.identity=333,.boot=33,.capabilities=6,.map_bound_us=40000,.group={.id=7,.epoch=1,.psk={1}}};
 timing_context=(struct r2_group_client){.source=true,.root=111,.boot=1000,.source_epoch=1,.publication=1,.at=1000000,.until=2000000,.map_until=1500000};
 exchange=(struct r2_join_exchange){.server=true,.peer=222,.peer_boot=2000};received_request=true;
 /* A direct receive map cannot prove a not-yet-created relay PA exists. */
 struct r2_join_record r=message(&n,R2_JOIN_ACCEPT,1000100);assert(r.demand==4);
 r2_group_admission_relay_ready(true);r=message(&n,R2_JOIN_ACCEPT,1000101);assert(r.demand==2);
 r=message(&n,R2_JOIN_ACCEPT,1500000);assert(r.demand==4); /* map expiry wins */
 r2_group_admission_relay_ready(false);r=message(&n,R2_JOIN_ACCEPT,1000102);assert(r.demand==4);
 r2_group_admission_reset();assert(!relay_ready);
 puts("PASS direct map alone yields PENDING, never LIVE relay publication");
}
static void guest_bootstrap(void){
 reset();struct r2_network n={.identity=333,.boot=33,.capabilities=6,.map_bound_us=40000};
 client=(struct r2_group_client){.passive=true,.source=true,.peer=111,.peer_boot=1000,.root=111,.boot=1000,.source_epoch=1,.publication=1,.at=1000000,.until=2000000,.utc_us=1791504000000000ULL,.uncertainty_us=40000};
 struct r2_group_client before=client;
 exchange=(struct r2_join_exchange){.server=true,.peer=222,.peer_boot=2000};
 struct r2_join_record r=gm_time(1001000);r.sender=222;r.sender_boot=2000;r.kind=R2_JOIN_PROOF;
 /* A timed guest is not a new selected upstream or a new replay anchor. */
 accepted_peer(&n,&r,1000000,1002000);assert(!memcmp(&client,&before,sizeof client));
 puts("PASS authenticated downstream guest cannot replace selected upstream coarse anchor");
}
static void publisher_bootstrap(void){
 reset();struct r2_network n={.identity=333,.boot=33,.capabilities=6,.group={.id=7}};
 client.passive=true;poor_direct=true;
 demand=(struct r2_local_demand){.root=111,.root_boot=1000,.peer=222,.peer_boot=2000,.at=1000000,.until=361000000,.source_epoch=1,.received=true};
 r2_group_admission_tick(&n,2000000);
 assert(admission_parent==111&&!admission_seek_relay);
 advertisement(222,true,-20,2000000);advertisement(111,false,-90,2000010);
 candidate_select(2040000);assert(peers[4].identity==111);
 puts("PASS publishing RC reacquires its authenticated root; never chooses its downstream client for bootstrap");
}
int main(int argc,char **argv){
 if(argc==2){
  if(!strcmp(argv[1],"pending-scope"))pending_scope();
  else if(!strcmp(argv[1],"live-publication"))live_publication();
  else if(!strcmp(argv[1],"guest-bootstrap"))guest_bootstrap();
  else if(!strcmp(argv[1],"publisher-bootstrap"))publisher_bootstrap();
  else assert(0);
  return 0;
 }
 discovery();health();renewals();authenticated_pending();return 0;
}

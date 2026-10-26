/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Actual transport/JOIN server bodies; public BLE callbacks are fake. */
#define main historical_transport_main
#include "gm_standby_transport_test.c"
#undef main
static void one(bool serving,bool fault_before_proof,bool gm_request){
 reset();r2_ble_gm5_policy(false,0,false);ready_guest(gm_request?3:4);
 const char *short_name="G";
 struct r2_gm7_admission_source source={.group=&n.group,.identity=333,.boot=33,
  .utc_us=UINT64_C(1792800000123456),.sampled_us=fake_time,.until_us=fake_time+1200000,
  .timing_epoch=7,.train=9,.map_bound_us=40000,.name=short_name,.serving=serving};
 struct r2_join_exchange receiver;
 assert(!r2_group_join_begin(&receiver,false,111,333,11,77,fake_time));
 receiver.request_at=fake_time;receiver.control=gm_request;
 struct r2_join_record r={.kind=R2_JOIN_REQUEST,.sender=111,.receiver=333,.sender_boot=11,
  .client_nonce=77,.sequence=1,.created_us=fake_time,.capabilities=gm_request?3:4,
  .control=gm_request,.name="UnchangedReceiver"};
 struct r2_ble_event e={.peer=4,.at=fake_time,.length=R2_GROUP_JOIN_BYTES};
 assert(!r2_group_join_encode(e.data,&r,&n.group));r2_group_admission_gm7_receive(&source,&e,fake_time);
 r2_group_admission_gm7_tick(&source,++fake_time);
 if(!serving){assert(!active&&!waiting&&!promote);r2_group_admission_reset();return;}
 struct r2_join_record reply;assert(!r2_group_join_decode(&reply,peers[4].tx[0],R2_GROUP_JOIN_BYTES,&n.group));
 assert(reply.kind==R2_JOIN_CHALLENGE&&!reply.control&&!strcmp(reply.name,"G"));
 assert(!r2_group_join_receive(&receiver,&reply,fake_time));
 if(fault_before_proof)source.serving=false;
 r.kind=R2_JOIN_PROOF;r.receiver_boot=33;r.server_nonce=reply.server_nonce;
 r.echo_us=reply.created_us;r.control=false;r.sequence=2;r.created_us=++fake_time;
 receiver.proof_at=fake_time;e.at=fake_time;
 assert(!r2_group_join_encode(e.data,&r,&n.group));r2_group_admission_gm7_receive(&source,&e,fake_time);
 r2_group_admission_gm7_tick(&source,++fake_time);
 if(fault_before_proof){assert(!active&&!waiting&&!promote);r2_group_admission_reset();return;}
 assert(!r2_group_join_decode(&reply,peers[4].tx[0],R2_GROUP_JOIN_BYTES,&n.group));
 assert(reply.kind==R2_JOIN_ACCEPT&&!reply.control&&!promote&&exchange.accepted);
 assert(!r2_group_join_receive(&receiver,&reply,fake_time));
 bool time=serving&&!fault_before_proof;
 assert(!!reply.source_flags==time);
 if(time){
  assert(reply.root==333&&reply.root_boot==33&&reply.source_epoch==7&&reply.publication==9);
  assert(reply.utc_us==source.utc_us+fake_time-source.sampled_us);
  assert(reply.remaining_us==source.until_us-fake_time);
  struct r2_join_time accepted;assert(r2_group_join_rc_time(&accepted,&reply,receiver.proof_at,fake_time,40000));
  assert(accepted.until<=source.until_us); /* a receipt never renews the original */
 }
 assert(!client.source&&!client.passive&&!n.hot_ready&&!n.standby_authorized);
 assert(!n.peer[1].identity); /* even a GM-capable proof never promotes HOT */
 uint64_t until=exchange.until;fake_time+=110000;r2_group_admission_gm7_tick(&source,fake_time);
 assert(!active&&exchange.until==until);
 r2_group_admission_reset();assert(!gm7_server_only);
}
int main(void){one(true,false,false);one(false,false,false);one(true,true,false);one(true,false,true);
 puts("PASS actual GM7 facade → unchanged client JOIN codec/exchange, fractional UTC, original source expiry, immediate source fault, short names, no HOT/control promotion and reset isolation");return 0;}

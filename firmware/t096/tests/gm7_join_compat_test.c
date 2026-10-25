/* MIT. Copyright (c) 2026 Jonathan Yantis. Actual shared transport/JOIN; fake host edges. */
#define main historical_gm7_transport_main
#include "gm7_transport_test.c"
#undef main
#include "../src/group_admission.c"
#include "rc_admission_policy.h"
static struct r2_group_config test_group={.id=456,.epoch=1,.psk={1}};
static uint8_t server_address[16];
static struct r2_gm7_admission_source src;
static unsigned rounds;
static void service_poll(void){
 ++fake_time;
 r2_ble_poll(fake_time);
 r2_group_admission_gm7_tick(&src,fake_time);
 struct r2_ble_event event;
 while(r2_ble_receive(&event))
  r2_group_admission_gm7_receive(&src,&event,fake_time);
}
static void begin_server(void){
 if(transport_started)assert(r2_ble_closed());
 r2_group_pair_key(server_address,&test_group,123,123);server_address[5]|=0xc0;
 r2_group_admission_reset();
 assert(!r2_ble_group_start(123,456,1,server_address));
 r2_ble_gm7_server(true); /* Production start() does not set relay_server. */
 assert(!relay_server&&gm7_server&&!group_client);
 src=(struct r2_gm7_admission_source){.group=&test_group,.identity=123,.boot=33,
  .utc_us=UINT64_C(1792800000123456),.sampled_us=fake_time,.until_us=fake_time+1200000,
  .timing_epoch=7,.train=9,.map_bound_us=40000,.name="GM",.serving=true};
 fake_early_tx=true;
}
static void incoming_client(struct bt_conn *c,unsigned role){
 uint8_t address[16];r2_group_pair_key(address,&test_group,999,999);address[5]|=0xc0;
 *c=(struct bt_conn){.refs=1,.role=role,.state=BT_CONN_STATE_CONNECTED,.interval=6,.timeout=100,
  .address={.type=BT_ADDR_LE_RANDOM}};memcpy(c->address.a.val,address,6);
 callbacks.connected(c,0);
 assert(peers[0].conn==c&&c->refs==2&&!peers[0].identity);
}
static void ready_incoming(struct bt_conn *c){
 incoming_client(c,BT_CONN_ROLE_PERIPHERAL);
 unsigned dc=disconnects;service_poll();
 assert(!peers[0].disconnect_requested&&disconnects==dc&&peers[0].mtu.func);
 /* Poll between every actual public callback; no preseeded ready fields. */
 att_mtu_updated(c,226,226);service_poll();assert(!peers[0].disconnect_requested);
 peers[0].mtu.func(c,0,&peers[0].mtu);service_poll();
 assert(peers[0].lookup.func&&!r2_ble_ready(4));
 located(c,0,&peers[0].lookup,discovery_marker,sizeof discovery_marker);service_poll();
 assert(r2_ble_ready(4)&&!peers[0].disconnect_requested);
}
static void close_client(struct bt_conn *c,bool stopping_service){
 if(stopping_service)r2_ble_stop();else r2_ble_admission_release();
 assert(peers[0].disconnect_requested&&c->refs==2);
 callbacks.disconnected(c,19);service_poll();
 assert(!peers[0].conn&&c->refs==1);bt_conn_unref(c);assert(!c->refs);
 if(stopping_service)assert(r2_ble_closed());
}
static void deliver(struct bt_conn *c,const struct r2_join_record *r){
 uint8_t wire[R2_GROUP_JOIN_BYTES];assert(!r2_group_join_encode(wire,r,&test_group));
 assert(write_record(c,NULL,wire,sizeof wire,0,0)==sizeof wire);
 service_poll();service_poll();
}
static struct r2_join_record sent(void){
 struct r2_join_record r;assert(!r2_group_join_decode(&r,peers[0].tx[0],R2_GROUP_JOIN_BYTES,&test_group));return r;
}
static void join_round(bool serving,bool fault){
 struct bt_conn c;ready_incoming(&c);src.serving=serving;
 struct r2_join_exchange receiver;uint64_t nonce=77+rounds++;
 assert(!r2_group_join_begin(&receiver,false,999,123,11,nonce,fake_time));
 receiver.request_at=fake_time;
 struct r2_join_record request={.kind=R2_JOIN_REQUEST,.sender=999,.receiver=123,.sender_boot=11,
  .client_nonce=nonce,.sequence=1,.created_us=fake_time,.capabilities=4,.name="UnchangedRC"};
 deliver(&c,&request);struct r2_join_record challenge=sent();
 assert(challenge.kind==R2_JOIN_CHALLENGE&&!challenge.control&&!peers[0].disconnect_requested);
 assert(!r2_group_join_receive(&receiver,&challenge,fake_time));
 if(fault)src.serving=false;
 struct r2_join_record proof=request;proof.kind=R2_JOIN_PROOF;proof.sequence=2;
 proof.receiver_boot=33;proof.server_nonce=challenge.server_nonce;proof.echo_us=challenge.created_us;
 proof.created_us=++fake_time;receiver.proof_at=fake_time;
 deliver(&c,&proof);struct r2_join_record accepted=sent();
 assert(accepted.kind==R2_JOIN_ACCEPT&&!accepted.control&&exchange.accepted&&!promote);
 assert(!r2_group_join_receive(&receiver,&accepted,fake_time));
 struct r2_join_time time;bool positive=serving&&!fault;
 assert(r2_group_join_rc_time(&time,&accepted,receiver.proof_at,fake_time,40000)==positive);
 if(positive){assert(accepted.root==123&&accepted.root_boot==33&&accepted.source_epoch==7&&accepted.publication==9);
  assert(time.until<=src.until_us&&accepted.utc_us==src.utc_us+accepted.created_us-src.sampled_us);
 }else assert(!accepted.source_flags&&!accepted.root&&!accepted.root_boot&&!accepted.remaining_us);
 assert(!r2_ble_control_available()&&!r2_ble_control_member()&&r2_ble_admission_promote()==-EACCES);
 assert(!client.source&&!client.passive&&nodes==1&&!control_members&&coordinator==255);
 /* The server's unchanged100ms accepted close timer releases this connection. */
 uint64_t original=exchange.until;fake_time=close_at;service_poll();assert(peers[0].disconnect_requested&&exchange.until==original);
 close_client(&c,false);assert(!active);
 fake_time+=300000;src.sampled_us=fake_time;src.until_us=fake_time+1200000;src.utc_us+=300000;
}
static void expiry_release_stop(void){
 begin_server();struct bt_conn c;incoming_client(&c,BT_CONN_ROLE_PERIPHERAL);service_poll();
 uint64_t end=peers[0].provisional_until;assert(peers[0].mtu.func&&!peers[0].disconnect_requested);
 fake_time=end-2;service_poll();assert(!peers[0].disconnect_requested&&peers[0].provisional_until==end);
 service_poll();assert(fake_time==end&&peers[0].disconnect_requested&&peers[0].provisional_until==end);
 close_client(&c,true);
 begin_server();incoming_client(&c,BT_CONN_ROLE_PERIPHERAL);service_poll();assert(peers[0].mtu.func);
 r2_ble_admission_release();assert(peers[0].disconnect_requested);close_client(&c,true);
 begin_server();ready_incoming(&c);fake_early_tx=false;
 uint8_t low[R2_AUTH_BYTES]={12,R2_AUTH_HELLO};assert(!r2_ble_send(4,low)&&peers[0].tx_busy==1);
 r2_ble_stop();callbacks.disconnected(&c,19);service_poll();assert(peers[0].conn==&c&&c.refs==2&&!r2_ble_closed());
 tx_done(&c,&peers[0].completion[0]);service_poll();assert(!peers[0].conn&&c.refs==1&&r2_ble_closed());bt_conn_unref(&c);
 begin_server();incoming_client(&c,0);service_poll();assert(!peers[0].admission_server&&peers[0].disconnect_requested);close_client(&c,true);
}
static void duplicate_and_restart(void){
 begin_server();struct bt_conn first,second;ready_incoming(&first);
 second=first;second.refs=1;callbacks.connected(&second,0);service_poll();
 assert(peers[0].conn==&first&&!peers[0].disconnect_requested&&last_disconnect==&second&&second.refs==2);
 callbacks.disconnected(&second,19);service_poll();assert(second.refs==1&&r2_ble_ready(4));bt_conn_unref(&second);
 struct bt_conn *old=&first;close_client(&first,true);
 begin_server();ready_incoming(&second);uint32_t incarnation=peers[0].incarnation;
 callbacks.disconnected(old,19);att_mtu_updated(old,23,23);tx_done(old,&peers[0].completion[0]);service_poll();
 assert(peers[0].conn==&second&&!peers[0].disconnect_requested&&peers[0].incarnation==incarnation&&r2_ble_ready(4));
 close_client(&second,true);
}
int main(void){
 assert(!relay_server);begin_server();
 assert(rc_admission_g1_direct(129,true)&&!rc_admission_g1_direct(193,true)&&!rc_admission_g1_direct(129,false));
 join_round(true,false);join_round(true,false);join_round(false,false);join_round(true,true);
 r2_ble_stop();assert(r2_ble_closed());
 expiry_release_stop();duplicate_and_restart();
 /* Existing production client path and GM/RC transport restart regression. */
 client_bootstrap();
 puts("PASS actual GM-only JOIN incoming MTU/lookup/REQUEST/CHALLENGE/PROOF/ACCEPT bootstrap+renewal, ordinaryRC timecodec, NoSource, originalexpiry/release/stop/TXdrain/duplicate/stalecallback/outgoing rejection; fakehost only");
 return 0;
}

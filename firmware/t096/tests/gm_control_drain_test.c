/* MIT. Copyright (c) 2026 Jonathan Yantis.
 * Production GM owner with hostile synchronous HCI/callback interleavings. */
#define R2_TIMING_EVENTS_TEST 1
#define bt_le_scan_stop unused_fake_scan_stop
#define bt_conn_disconnect unused_fake_disconnect
#include "ble_transport_fake.h"
#undef bt_le_scan_stop
#undef bt_conn_disconnect
static int stop_result,close_result,parameter_result;
static uint32_t parameter_a,parameter_b;
void r2_timing_event(unsigned kind,int result,uint64_t peer,uint32_t detail,uint32_t a,uint32_t b){
 (void)kind;(void)peer;if(detail==23){parameter_result=result;parameter_a=a;parameter_b=b;}
}
static uint64_t stop_delay;
static bool disconnect_during_call;
static int bt_le_scan_stop(void){++scan_stops;fake_time+=stop_delay;return stop_result;}
static int bt_conn_disconnect(struct bt_conn *,uint8_t);
static int sys_csrand_get(void *out,size_t n){memset(out,0x5a,n);return 0;}
#include "../protocol/ble_transport.c"
#include <stdio.h>
static int bt_conn_le_create(const bt_addr_le_t *a,const void *b,const void *c,struct bt_conn **out){return -EBUSY;}
static int bt_conn_disconnect(struct bt_conn *c,uint8_t reason){
 ++disconnects;last_disconnect=c;
 if(disconnect_during_call)callbacks.disconnected(c,62);
 return close_result;
}
static void reset(void){
 memset(peers,0,sizeof peers);memset(duplicates,0,sizeof duplicates);
 memset(&first_fault,0,sizeof first_fault);memset(gm_diag_state,0,sizeof gm_diag_state);
 stop_error=0;scan_stop_until=0;stop_result=close_result=0;stop_delay=0;disconnect_during_call=false;
 fake_time=1000000;self=0;nodes=1;group_id=1;local_identity=10;group_client=false;
 stopping=advertising=precision_discovery=sync_acquiring=false;scanning=true;
}
static void normal_owner_retirement(void){
 for(unsigned response=0;response<4;response++)for(unsigned order=0;order<3;order++){
  reset();scanning=false;nodes=2;gm5_mode=true;gm5_selected=20;
  struct bt_conn conn={.refs=2};struct peer *p=&peers[1];
  p->conn=&conn;p->identity=20;p->incarnation=7;p->authenticated=true;
  atomic_set(&p->connected,1);atomic_set(&p->tx_busy,3);
  close_result=response==0?0:response==1?-EACCES:response==2?-ENOTCONN:-EIO;
  unsigned calls=disconnects;r2_ble_disconnect(1);
  assert(disconnects==calls+1&&p->connect_until==6000000&&p->disconnect_requested);
  assert(stop_error==(response==3?-EIO:0)&&!r2_ble_ready(1));
  for(unsigned n=0;n<100;n++)r2_ble_disconnect(1);
  assert(disconnects==calls+1&&p->connect_until==6000000);
  if(order==0)callbacks.disconnected(&conn,19);
  if(order!=2){atomic_clear(&p->tx_busy);if(order==1)callbacks.disconnected(&conn,19);}
  fake_time=5999999;r2_ble_poll(fake_time);
  if(order!=2){assert(!p->conn&&conn.refs==1&&!p->identity);}
  else{
   assert(p->conn==&conn&&conn.refs==2);fake_time=6000000;r2_ble_poll(fake_time);
   assert(stop_error&&p->conn==&conn&&atomic_get(&p->tx_busy)==3);
   callbacks.disconnected(&conn,19);r2_ble_poll(++fake_time);assert(p->conn==&conn);
   atomic_clear(&p->tx_busy);r2_ble_poll(++fake_time);assert(!p->conn&&conn.refs==1&&stop_error);
  }
 }
 /* Natural loss plus withheld TX and a pending public call share the first
  * callback's retirement deadline; neither repeated callback can extend it. */
 reset();scanning=false;nodes=2;gm5_mode=true;
 struct bt_conn conn={.refs=2};struct peer *p=&peers[1];p->conn=&conn;p->identity=20;
 atomic_set(&p->connected,1);atomic_set(&p->tx_busy,1);
 callbacks.disconnected(&conn,19);fake_time=2000000;callbacks.disconnected(&conn,19);
 assert(p->connect_until==6000000);gatt_request.pending=true;gatt_request.peer=1;
 fake_time=6000000;r2_ble_poll(fake_time);assert(stop_error==-ETIMEDOUT&&conn.refs==2);
 gatt_request.pending=false;atomic_clear(&p->tx_busy);r2_ble_poll(++fake_time);assert(conn.refs==1&&!p->conn);
 /* A synchronous natural callback during HCI refusal also retires exactly once. */
 reset();scanning=false;nodes=2;gm5_mode=true;p=&peers[1];p->conn=&conn;conn.refs=2;
 close_result=-ENOTCONN;disconnect_during_call=true;r2_ble_disconnect(1);r2_ble_poll(++fake_time);
 assert(!stop_error&&!p->conn&&conn.refs==1);
 gm5_mode=false;
 puts("PASS normal owner one-shot close; actual rc; immutable5s; both callback orders; missing TX/disconnect/GATT retain ownership and fault; natural loss and synchronous race");
}
static void negotiated_retirement(void){
 for(unsigned withheld=0;withheld<2;withheld++){
  reset();scanning=false;nodes=2;gm5_mode=true;
  struct bt_conn c={.refs=2,.role=BT_CONN_ROLE_PERIPHERAL,.state=BT_CONN_STATE_CONNECTED,
                    .interval=40,.latency=4,.timeout=500};
  struct peer *p=&peers[1];p->conn=&c;p->identity=20;p->incarnation=443;
  atomic_set(&p->connected,1);atomic_set(&p->tx_busy,3);
  unsigned calls=disconnects;r2_ble_disconnect(1);uint64_t until=p->connect_until;
  assert(disconnects==calls+1);
  fake_time=4000000;r2_ble_poll(fake_time);assert(!stop_error&&p->conn==&c&&c.refs==2);
  assert(until==11000000);
  assert(parameter_result==((BT_CONN_STATE_CONNECTED<<16)|500)&&parameter_a==(40u|(4u<<16))&&parameter_b==443);
  c.timeout=3200;r2_ble_disconnect(1);assert(p->connect_until==until&&disconnects==calls+1);
  if(!withheld){callbacks.disconnected(&c,19);atomic_clear(&p->tx_busy);r2_ble_poll(++fake_time);
   assert(!stop_error&&!p->conn&&c.refs==1);}
  else {fake_time=until-1;r2_ble_poll(fake_time);assert(!stop_error&&p->conn==&c);
   fake_time=until;r2_ble_poll(fake_time);assert(stop_error==-ETIMEDOUT&&p->conn==&c&&c.refs==2);
   callbacks.disconnected(&c,19);r2_ble_poll(++fake_time);assert(p->conn==&c);
   atomic_clear(&p->tx_busy);r2_ble_poll(++fake_time);assert(!p->conn&&c.refs==1);}
 }
 /* Failed parameter reads retain ownership and preserve their actual error. */
 reset();scanning=false;nodes=2;struct bt_conn c={.refs=2,.info_error=-EIO};
 peers[1].conn=&c;r2_ble_disconnect(1);assert(stop_error==-EIO&&peers[1].conn==&c&&c.refs==2);
 reset();scanning=false;nodes=2;c=(struct bt_conn){.refs=2,.state=BT_CONN_STATE_CONNECTED,.timeout=3200};
 peers[1].conn=&c;r2_ble_disconnect(1);assert(!stop_error&&peers[1].connect_until==38000000);
 reset();scanning=false;nodes=2;c=(struct bt_conn){.refs=2,.state=BT_CONN_STATE_CONNECTED};
 peers[1].conn=&c;r2_ble_disconnect(1);assert(stop_error==-EINVAL&&peers[1].conn==&c);
 reset();scanning=false;nodes=2;c=(struct bt_conn){.refs=2,.state=0,.timeout=500,.interval=24,.latency=2};
 peers[1].conn=&c;r2_ble_disconnect(1);assert(!stop_error&&peers[1].connect_until==6000000);
 assert(parameter_result==500&&parameter_a==(24u|(2u<<16))); /* Host disconnected, app callback missing. */
 puts("PASS negotiated5s supervision + original5s callback allowance; late legal close, immutable boundary, unknown info fenced");
}
int main(void){
 negotiated_retirement();
 normal_owner_retirement();
 for(unsigned reason=0;reason<2;reason++){
  reset();struct bt_conn orphan={.address={.type=1,.a={{99,0,0,0,0,0xc0}}},.refs=1};
  duplicate_track(&orphan,fake_time);uint64_t until=duplicates[3].until;
  close_result=reason?-ENOTCONN:-EACCES;disconnect_during_call=true;
  unsigned calls=disconnects;duplicate_poll(fake_time);
  assert(!stop_error&&orphan.refs==2&&duplicates[3].dead&&duplicates[3].until==until);
  duplicate_poll(++fake_time);assert(!stop_error&&orphan.refs==1&&!duplicates[3].conn&&disconnects==calls+1);
 }
 reset();struct bt_conn orphan={.address={.type=1,.a={{99,0,0,0,0,0xc0}}},.refs=1};
 duplicate_track(&orphan,fake_time);close_result=-EACCES;unsigned calls=disconnects;
 duplicate_poll(fake_time);assert(!stop_error&&orphan.refs==2);
 fake_time=5999999;duplicate_poll(fake_time);assert(!stop_error&&orphan.refs==2&&disconnects==calls+1);
 callbacks.connected(&orphan,0);assert(duplicates[3].until==6000000); /* no rearm */
 fake_time=6000000;duplicate_poll(fake_time);assert(stop_error==-ETIMEDOUT&&orphan.refs==2);
 callbacks.disconnected(&orphan,62);duplicate_poll(++fake_time);assert(orphan.refs==1&&stop_error==-ETIMEDOUT);
 reset();duplicate_track(&orphan,fake_time);close_result=-EIO;duplicate_poll(fake_time);
 assert(stop_error==-EIO&&orphan.refs==2);callbacks.disconnected(&orphan,19);duplicate_poll(++fake_time);assert(orphan.refs==1);
 for(unsigned transient=0;transient<3;transient++){
  reset();stop_result=transient==0?-EACCES:transient==1?-EBUSY:-EAGAIN;
  unsigned calls=scan_stops;assert(!stop_scan(fake_time));assert(scanning&&!stop_error&&scan_stop_until==1500000);
  assert(!stop_scan(fake_time+19999)&&scan_stops==calls+1&&scan_stop_until==1500000);
  stop_result=0;assert(stop_scan(fake_time+20000));assert(!scanning&&!stop_error&&!scan_stop_until&&scan_stops==calls+2);
 }
 reset();stop_result=-EACCES;assert(!stop_scan(fake_time));
 for(unsigned i=1;i<25;i++)assert(!stop_scan(fake_time+i*20000)&&scan_stop_until==1500000&&!stop_error&&scanning);
 unsigned calls2=scan_stops;assert(!stop_scan(1500000));assert(stop_error==-ETIMEDOUT&&scanning&&!scan_stop_until&&scan_stops==calls2);
 stop_result=0;assert(stop_scan(1500001)&&!scanning&&stop_error==-ETIMEDOUT); /* actual drain cannot erase failure */
 gm_diagnostic_publish();assert(gm_diag_state[0]==1&&(int32_t)gm_diag_state[9]==-ETIMEDOUT); /* no console poll required */
 reset();stop_result=-EIO;assert(!stop_scan(fake_time)&&stop_error==-EIO&&scanning&&!scan_stop_until);
 reset();stop_result=-EACCES;assert(!stop_scan(fake_time));stop_result=0;stop_delay=500000;
 assert(!stop_scan(fake_time+20000)&&!scanning&&!scan_stop_until&&stop_error==-ETIMEDOUT&&first_fault.operation==6);
 reset();group_client=true;stop_result=-EACCES;assert(!stop_scan(fake_time)&&stop_error==-EACCES&&!scan_stop_until);
 reset();stop_result=-EACCES;assert(!stop_scan(fake_time));stopping=true;stop_result=0;
 r2_ble_poll(fake_time+20000);assert(!scanning&&!scan_stop_until&&!stop_error); /* stop continues through drain */
 puts("PASS native GM scan retry20ms/original500ms; hard/RC errors retained; duplicate callback races drain once/original2s; missing callbacks fail; terminal snapshot retained");
}

/* Exercise actual callback and owner code, including legal early callbacks. */
#include <sys/types.h>
#include "ble_transport_fake.h"
static int random_result;
static int sys_csrand_get(void *out,size_t n){memset(out,random_result==1?0:0x5a,n);return random_result<0?random_result:0;}
#include "../protocol/ble_transport.c"
#include <stdio.h>
static struct bt_conn connection;
static void gm_peer_first_mtu(void)
{
 for(unsigned delayed=0;delayed<3;delayed++){
  memset(peers,0,sizeof peers);memset(&gatt_request,0,sizeof gatt_request);
  self=0;nodes=5;group_id=1;group_client=field_profile=stopping=scanning=advertising=false;
  transport_owner=NULL;transport_started=false;stop_error=0;fake_time=1000000;
  atomic_clear(&error_count);first_fault=(struct r2_transport_fault){0};
  connection.refs=1;struct peer *p=&peers[4];p->conn=&connection;p->identity=20;
  p->connected=1;p->admission_server=true;p->provisional_until=fake_time+5000000;
  unsigned calls=fake_mtu_calls,lookups=reads;
  /* The host also reports initial23B attachment: this cannot admit data. */
  att_mtu_updated(&connection,23,23);assert(!r2_ble_ready(4));
  if(delayed==1){
   /* Peer request sets the host's exchanged bit before its public MTU callback
    * is delivered. EALREADY does not prove an adequate negotiated value. */
   fake_gatt_result=-EALREADY;r2_ble_poll(fake_time);
   assert(!r2_ble_errors()&&!r2_ble_ready(4)&&!p->lookup.func);
   assert(fake_mtu_calls==calls+1);calls++;
   r2_ble_poll(fake_time+1);assert(fake_mtu_calls==calls+1&&!p->lookup.func);calls++;
  }
  if(delayed==2){fake_gatt_result=0;r2_ble_poll(fake_time);assert(fake_mtu_calls==calls+1);calls++;}
  fake_gatt_result=0;
  att_mtu_updated(&connection,R2_GROUP_CONTROL_MAX+3,R2_GROUP_CONTROL_MAX+3);
  r2_ble_poll(fake_time+2);
  if(delayed==2){
   assert(reads==lookups&&!atomic_get(&p->mtu_done));
   mtu_done(&connection,0,&p->mtu);r2_ble_poll(fake_time+3);
  }
  assert(fake_mtu_calls==calls&&reads==lookups+1&&p->lookup.func);
  assert(atomic_get(&p->mtu_done)==(delayed==2?1:0)); /* no invented completion */
  located(&connection,0,&p->lookup,discovery_marker,sizeof discovery_marker);
  assert(r2_ble_ready(4)&&!r2_ble_errors());
  int slot=r2_ble_admission_promote();assert(slot>0&&slot<4&&r2_ble_ready(slot));
  p=&peers[slot];assert(atomic_get(&p->mtu_done)==(delayed==2?1:0));
  callbacks.disconnected(&connection,19);
  att_mtu_updated(&connection,R2_GROUP_CONTROL_MAX+3,R2_GROUP_CONTROL_MAX+3);
  assert(!r2_ble_ready(slot)&&!atomic_get(&p->att_mtu));
  r2_ble_poll(fake_time+3);assert(!connection.refs&&!p->conn);
 }
 /* A peer that negotiated only23B remains unready and expires at the
  * original provisional deadline; no API-error/retry loop extends it. */
 memset(peers,0,sizeof peers);connection.refs=1;struct peer *p=&peers[4];
 p->conn=&connection;p->identity=20;p->connected=1;p->admission_server=true;
 fake_time=2000000;p->provisional_until=fake_time+5000000;
 att_mtu_updated(&connection,23,23);fake_gatt_result=-EALREADY;
 unsigned calls=fake_mtu_calls,stopped=disconnects;r2_ble_poll(fake_time);
 r2_ble_poll(fake_time+4999999);
 assert(fake_mtu_calls==calls+2&&!r2_ble_ready(4)&&!p->lookup.func&&!r2_ble_errors());
 assert(disconnects==stopped);r2_ble_poll(fake_time+5000000);assert(disconnects==stopped+1);
 callbacks.disconnected(&connection,19);fake_gatt_result=0;r2_ble_poll(fake_time+5000001);
 assert(!connection.refs&&!p->conn);
 puts("PASS peer-first public negotiated MTU permits discovery without fabricated own completion; initial/small23B and disconnected callbacks remain unready; EALREADY never counts as a fault or extends provisional expiry");
}
static bool early;static unsigned creates,last_interval,last_supervision;
static int bt_conn_le_create(const bt_addr_le_t *address,const void *create,
 const void *params,struct bt_conn **out)
{
 ++creates;last_interval=((const struct bt_le_conn_param *)params)->interval_min;last_supervision=((const struct bt_le_conn_param *)params)->timeout;connection.address=*address;
 connection.refs=2; /* host lifecycle + API return reference */
 if(early)callbacks.connected(&connection,0);
 *out=&connection;
 return 0;
}
static void priority_slots(void)
{
 memset(peers,0,sizeof peers);stopping=false;self=0;nodes=2;
 struct peer *p=&peers[1];p->conn=&connection;p->connected=p->discovered=1;p->att_mtu=226;
 for(unsigned j=0;j<2;j++)p->completion[j]=(struct tx_context){p,j};
 uint8_t low[R2_AUTH_BYTES]={12,R2_AUTH_HELLO},high[R2_AUTH_BYTES]={12,R2_AUTH_SERVICE};
 assert(!r2_ble_send(1,low));assert(atomic_get(&p->tx_busy)==1);
 assert(r2_ble_send(1,low)==-EAGAIN);assert(!r2_ble_send(1,high));
 assert(atomic_get(&p->tx_busy)==3);assert(!memcmp(p->tx[0],low,sizeof low));
 assert(!memcmp(p->tx[1],high,sizeof high));assert(r2_ble_send(1,high)==-EAGAIN);
 tx_done(&connection,&p->completion[0]);assert(atomic_get(&p->tx_busy)==2);
 assert(r2_ble_send(1,low)==-EAGAIN);high[1]=R2_AUTH_ACK;
 assert(!r2_ble_send(1,high));assert(p->tx[1][1]==R2_AUTH_SERVICE);
 tx_done(&connection,&p->completion[1]);tx_done(&connection,&p->completion[0]);
 assert(!atomic_get(&p->tx_busy));assert(!r2_ble_send(1,low));
 tx_done(&connection,&p->completion[0]);
}
static void discovery_budget(void)
{
 memset(peers,0,sizeof peers);self=0;nodes=3;stopping=scanning=precision_discovery=false;
 unsigned starts=scan_starts,stops=scan_stops;
 r2_ble_poll(1000000);assert(scanning&&last_scan.interval==0xa0&&last_scan.window==0x50);
 peers[1].conn=&connection;peers[1].connected=1;
 r2_ble_poll(1002000);assert(scanning&&last_scan.window==0x10);
 assert(scan_starts==starts+2&&scan_stops==stops+1);
 /* A missing peer cannot restore aggressive discovery while a source link lives. */
 precision_discovery=true;r2_ble_poll(1004000);assert(last_scan.window==0x10&&scan_starts==starts+2);
 peers[1].conn=NULL;peers[1].connected=0;r2_ble_poll(1006000);
 assert(last_scan.window==0x50&&scan_starts==starts+3&&scan_stops==stops+2);
 precision_discovery=false;r2_ble_stop();assert(!scanning);stopping=false;
 memset(peers,0,sizeof peers);
}
static void group_admission_lifetime(void)
{
 /* Independent public-boundary scenario begins at a modeled fresh boot. */
 transport_started=false;stop_error=0;
 memset(peers,0,sizeof peers);stopping=scanning=precision_discovery=false;
 const uint8_t address[6]={1,2,3,4,5,0xc0};
 assert(!r2_ble_group_start(1,1,1,address));
 struct peer *p=&peers[4];p->identity=5;p->address.type=1;p->address.a.val[0]=5;
 p->provisional_until=2000000;p->advertised_at=1000000;p->retry=1;
 advertising=true;unsigned stops=adv_stops;early=true;
 unsigned before=creates;r2_ble_sync_acquiring(true);r2_ble_poll(1000000);
 assert(creates==before&&!p->conn);r2_ble_sync_acquiring(false);
 r2_ble_poll(1000000);assert(adv_stops>stops&&p->conn==&connection&&last_interval==40&&last_supervision==100);
 assert(connection.refs==2);
 /* ID binding is permitted before our own GATT discovery completes, but not
  * to an address that differs from the authenticated identity derivation. */
 uint8_t wrong[6]={9};assert(r2_ble_admission_bind(5,wrong)==-EACCES);
 assert(!r2_ble_admission_bind(5,p->address.a.val));
 assert(!r2_ble_ready(4));p->discovered=p->mtu_done=1;p->handle=42;att_mtu_updated(&connection,226,226);
 assert(r2_ble_ready(4));atomic_set(&p->tx_busy,1);
 assert(r2_ble_admission_promote()==-EAGAIN);atomic_clear(&p->tx_busy);
 assert(r2_ble_admission_promote()==1&&peers[1].identity==5);
 assert(!p->conn&&!p->identity&&peers[1].conn==&connection&&connection.refs==2);
 callbacks.disconnected(&connection,19);stopping=true;r2_ble_poll(1100000);
 assert(connection.refs==1&&!peers[1].conn);bt_conn_unref(&connection);
 group_id=0;stopping=false;memset(peers,0,sizeof peers);
}
static void repeated_group_rejoin(void)
{
 /* Independent public-boundary scenario begins at a modeled fresh boot. */
 transport_started=false;stop_error=0;
 for(unsigned order=0;order<2;order++)for(unsigned reset=0;reset<3;reset++){
  memset(peers,0,sizeof peers);transport_started=false;stop_error=0;stopping=scanning=precision_discovery=false;
  const uint8_t own[6]={1,2,3,4,5,0xc0};assert(!r2_ble_group_start(order?9:1,1,1,own));
  struct peer *p=&peers[1];p->identity=5;p->address.type=1;p->address.a.val[0]=5;
  connection.address=p->address;connection.refs=2;p->conn=&connection;p->connected=1;p->authenticated=true;p->provisional_until=2000000;p->incarnation=10+reset;
  atomic_set(&p->tx_busy,1);callbacks.disconnected(&connection,19);r2_ble_poll(1100000);
  assert(p->conn==&connection&&p->identity==5); /* immutable TX callback still owns it */
  atomic_clear(&p->tx_busy);r2_ble_poll(1102000);
  assert(!p->conn&&!p->identity&&!p->authenticated&&p->incarnation>10+reset);
  uint8_t wire[25]={24,BT_DATA_MANUFACTURER_DATA,'G','1',1};
  r2_group_put(wire+5,1,8);r2_group_put(wire+13,1,4);r2_group_put(wire+17,5,8);
  struct net_buf_simple ad={.data=wire,.len=sizeof wire};scan_report(&connection.address,0,BT_GAP_ADV_TYPE_ADV_IND,&ad);
  candidate_select(fake_time+40000);
  assert(peers[4].identity==5&&!peers[1].identity); /* both initiator orders re-enter JOIN */
  bt_conn_unref(&connection);assert(!connection.refs);
 }
 group_id=0;memset(peers,0,sizeof peers);
}
static void field_client_initiates(void){
 /* Independent public-boundary scenario begins at a modeled fresh boot. */
 transport_started=false;stop_error=0;
 memset(peers,0,sizeof peers);stopping=scanning=precision_discovery=sync_acquiring=false;
 const uint64_t ids[4]={11,22,33,0};const uint8_t addresses[3][6]={{1,0,0,0,0,0xc0},{2,0,0,0,0,0xc0},{3,0,0,0,0,0xc0}};
 assert(!r2_ble_field_start(2,ids,addresses));client_window=true;early=false;
 unsigned before=creates;r2_ble_poll(1000000);assert(scanning&&!advertising&&creates==before);
 scan_report(&peers[1].address,0,BT_GAP_ADV_TYPE_ADV_IND,NULL);r2_ble_poll(1000000);
 assert(creates==before+1&&peers[1].conn==&connection&&last_interval==6&&last_supervision==10);
 unsigned stopped=disconnects;r2_ble_poll(1099999);assert(disconnects==stopped);
 r2_ble_poll(1100000);assert(disconnects==stopped+1&&peers[1].conn==&connection);
 callbacks.disconnected(&connection,19);r2_ble_poll(1100001);bt_conn_unref(&connection);assert(!connection.refs);
 client_window=false;r2_ble_poll(1450000);assert(!scanning&&!advertising);
 r2_ble_stop();r2_ble_poll(1450001);assert(r2_ble_closed()&&!r2_ble_stop_error());
 assert(!r2_ble_field_start(1,ids,addresses));client_window=true;
 peers[0].conn=&connection;peers[0].connected=peers[0].discovered=1;peers[0].att_mtu=226;peers[0].authenticated=true;fake_mtu=226;
 r2_ble_topology(0,3);r2_ble_poll(2000000);assert(advertising&&!scanning&&control_ready());
 r2_ble_topology(0,1);r2_ble_poll(2001000);assert(advertising&&!scanning&&control_ready());
 precision_discovery=true;r2_ble_poll(2002000);assert(scanning&&last_scan.window==last_scan.interval);
 precision_discovery=false;peers[0].authenticated=false;r2_ble_poll(2004000);assert(scanning&&!control_ready());
 peers[0].conn=NULL;peers[0].connected=0;
 memset(peers,0,sizeof peers);
 r2_ble_stop();r2_ble_poll(2100000);assert(r2_ble_closed()&&!r2_ble_stop_error());
 assert(!r2_ble_field_start(0,ids,addresses));
 peers[1].conn=&connection;peers[1].connected=peers[1].discovered=1;peers[1].att_mtu=226;peers[1].authenticated=true;
 r2_ble_topology(0,1);r2_ble_poll(2206000);assert(advertising&&!scanning&&control_ready());
 peers[1].authenticated=false;peers[1].conn=NULL;peers[1].connected=0;r2_ble_poll(2208000);assert(scanning&&!control_ready());
 peers[1].conn=NULL;peers[1].connected=0;field_profile=group_client=false;memset(peers,0,sizeof peers);
}
static void control_obligations(void){
 memset(peers,0,sizeof peers);field_profile=false;group_client=false;group_id=7;self=0;nodes=5;public_client=PEERS;
 peers[0].identity=10;peers[1].identity=20;peers[2].identity=30;
 peers[1].conn=&connection;peers[1].connected=peers[1].discovered=1;peers[1].att_mtu=226;peers[1].authenticated=true;fake_mtu=226;
 /* A follower needs its coordinator, not a full mesh to the other GM. */
 r2_ble_topology(1,7);assert(control_ready());
 r2_ble_topology(0,1);assert(!control_ready());
 peers[2].conn=&connection;peers[2].connected=peers[2].discovered=1;peers[2].att_mtu=226;peers[2].authenticated=true;
 assert(control_ready());
 /* Unknown client/candidate discovery is transient slot4, never control. */
 peers[4].identity=99;peers[4].conn=&connection;assert(control_ready());
 peers[1].authenticated=false;assert(!control_ready());peers[1].authenticated=true;
 peers[1].identity=peers[2].identity=0;assert(!control_ready()); /* isolated partition discovery */
 memset(peers,0,sizeof peers);group_id=0;
}
static void group_discovery_fallback(void){
 /* Independent public-boundary scenario begins at a modeled fresh boot. */
 transport_started=false;stop_error=0;
 const uint8_t own_addr[6]={1,2,3,4,5,0xc0};
 for(unsigned client_case=0;client_case<3;client_case++){
  memset(peers,0,sizeof peers);transport_started=false;stop_error=0;stopping=scanning=precision_discovery=sync_acquiring=false;fake_time=1000000;
  assert(!r2_ble_group_start(client_case==2?1:9,1,1,own_addr));group_client=client_case!=0;client_window=true;
  bt_addr_le_t addr={.type=1,.a={{5,0,0,0,0,0xc0}}};
  uint8_t wire[25]={24,BT_DATA_MANUFACTURER_DATA,'G','1',1};r2_group_put(wire+5,1,8);r2_group_put(wire+13,1,4);r2_group_put(wire+17,5,8);
  struct net_buf_simple ad={.data=wire,.len=25};scan_report(&addr,0,BT_GAP_ADV_TYPE_ADV_IND,&ad);
  if(client_case){fake_time+=40000;candidate_select(fake_time);}
  uint64_t original_deadline=peers[4].provisional_until;unsigned before=creates;early=true;r2_ble_poll(fake_time);
  if(!client_case){
   assert(creates==before&&original_deadline==6000000);
   fake_time=3999999;scan_report(&addr,0,BT_GAP_ADV_TYPE_ADV_IND,&ad);r2_ble_poll(fake_time);assert(creates==before&&peers[4].provisional_until==original_deadline);
   fake_time=4000000;scan_report(&addr,0,BT_GAP_ADV_TYPE_ADV_IND,&ad);r2_ble_poll(fake_time);
  }
  assert(creates==before+1&&peers[4].conn==&connection&&!peers[4].admission_server&&peers[4].provisional_until==original_deadline);
  struct bt_conn duplicate={.address=addr,.refs=1,.role=BT_CONN_ROLE_PERIPHERAL};unsigned d=disconnects;
  callbacks.connected(&duplicate,0);assert(disconnects==d&&peers[4].conn==&connection&&duplicate.refs==2);
  r2_ble_poll(fake_time+1);assert(disconnects==d+1&&last_disconnect==&duplicate&&!r2_ble_closed());
  callbacks.connected(&duplicate,0);r2_ble_poll(fake_time+2);assert(disconnects==d+1&&duplicate.refs==2);
  callbacks.disconnected(&duplicate,19);r2_ble_poll(fake_time+3);assert(duplicate.refs==1);
  callbacks.disconnected(&connection,19);stopping=true;r2_ble_poll(fake_time+1);bt_conn_unref(&connection);assert(!connection.refs&&!peers[4].identity);
 }
 fake_time=1000000;field_profile=group_client=false;group_id=0;memset(peers,0,sizeof peers);
}

static void targeted_discovery(void){
 for(unsigned mode=0;mode<4;mode++){
  memset(peers,0,sizeof peers);field_profile=true;self=2;nodes=3;admission_peer=1;stopping=false;
  struct peer *p=&peers[1];p->conn=&connection;connection.refs=2;p->connected=p->mtu_done=1;p->att_mtu=226;
  unsigned r=reads,d=discovers;r2_ble_poll(1100000);assert(reads==r+1&&discovers==d);
  p->lookup.by_uuid.start_handle=mode==2?0:42;
  uint8_t bad[5]={'B','A','D',0,0};
  located(&connection,mode==1?2:0,&p->lookup,mode==3?bad:discovery_marker,5);
  r2_ble_poll(1102000);r2_ble_poll(1104000);
  assert(reads==r+1);assert(discovers==d+(mode?1:0));
  assert(mode?!p->discovered:(p->discovered==1&&p->handle==42));
  unsigned c=cancels;callbacks.disconnected(&connection,19);r2_ble_poll(1106000);
  assert(cancels==c+3&&!p->conn&&!p->lookup.func&&!p->lookup_done);bt_conn_unref(&connection);
 }
 field_profile=false;group_id=0;memset(peers,0,sizeof peers);
}
static void relay_guest_without_bootstrap(void)
{
 memset(peers,0,sizeof peers);self=0;nodes=PEERS;admission_peer=CONTROL_PEERS;
 group_id=1;group_client=relay_server=client_window=true;
 stopping=scanning=precision_discovery=bootstrap_needed=sync_acquiring=false;
 struct peer *p=&peers[admission_peer];p->conn=&connection;p->connected=1;
 p->admission_server=true;p->provisional_until=6000000;
 unsigned before=disconnects,started=creates;
 assert(!desired(admission_peer));r2_ble_poll(1000000);
 assert(disconnects==before&&p->mtu.func==mtu_done&&creates==started);
 /* This exception never extends the guest deadline or permits outgoing work. */
 r2_ble_poll(6000000);assert(disconnects>before&&creates==started);
 before=disconnects;p->provisional_until=9000000;p->release=true;
 r2_ble_poll(6002000);assert(disconnects>before);p->release=false;
 before=disconnects;p->admission_server=false;
 r2_ble_poll(6004000);assert(disconnects>before);
 before=disconnects;p->admission_server=true;relay_server=false;
 r2_ble_poll(6006000);assert(disconnects>before);
 memset(peers,0,sizeof peers);group_id=0;group_client=relay_server=false;
 client_window=true;stopping=scanning=advertising=false;
}
static void rc_cold_scan_opportunities(void){
 memset(peers,0,sizeof peers);nodes=1;group_client=relay_server=client_window=scanning=true;stopping=false;
 discovery_second=discovery_listen=0;discovery_was_listening=discovery_completed=discovery_reported=false;
 for(uint64_t t=1000000;t<=1200000;t+=10000)discovery_opportunity_poll(t);
 assert(r2_ble_admission_scan_opportunity());assert(!r2_ble_admission_scan_opportunity());
 for(uint64_t t=2000000;t<=2090000;t+=10000)discovery_opportunity_poll(t);
 scanning=false;discovery_opportunity_poll(2200000);assert(!r2_ble_admission_scan_opportunity());
 relay_server=false;scanning=false;
}
static void passive_discovery_hint(void){
 memset(peers,0,sizeof peers);group_id=7;group_epoch=1;local_identity=10;
 group_client=false;self=0;nodes=PEERS;recent_admission=0;
 uint8_t bytes[25]={24,BT_DATA_MANUFACTURER_DATA,'G','1',65};
 r2_group_put(bytes+5,7,8);r2_group_put(bytes+13,1,4);r2_group_put(bytes+17,20,8);
 struct net_buf_simple ad={.data=bytes,.len=sizeof bytes};bt_addr_le_t address={0};address.a.val[0]=20;
 scan_report(&address,0,BT_GAP_ADV_TYPE_ADV_IND,&ad);assert(!peers[CONTROL_PEERS].identity);
 group_client=true;admission_seek_relay=false;admission_parent=0;scan_report(&address,0,BT_GAP_ADV_TYPE_ADV_IND,&ad);candidate_select(fake_time+40000);assert(!peers[CONTROL_PEERS].identity);
 admission_seek_relay=true;candidate_select(fake_time+40000);assert(peers[CONTROL_PEERS].identity==20);
 memset(peers,0,sizeof peers);group_client=false;bytes[4]=1;
 scan_report(&address,0,BT_GAP_ADV_TYPE_ADV_IND,&ad);assert(peers[CONTROL_PEERS].identity==20);
 memset(peers,0,sizeof peers);group_id=0;
}
static void cold_window_rendezvous(void){
 memset(peers,0,sizeof peers);field_profile=false;group_client=true;client_aligned=false;
 sync_acquiring=scanning=client_search_seen=false;client_phase=0;client_resume=0;
 r2_ble_client_seek(100000,true);assert(client_search_seen&&!client_phase);
 r2_ble_client_seek(499999,true);assert(!client_phase);
 scanning=true;r2_ble_client_seek(500000,true);assert(!client_phase);
 scanning=false;r2_ble_client_seek(500000,true);assert(client_phase==100000&&client_resume==1000000&&!client_search_seen);
 r2_ble_client_seek(1000000,true);assert(client_phase==100000); /* no window yet */
 r2_ble_client_seek(1100000,true);assert(client_search_seen);
 r2_ble_client_seek(1600000,true);assert(client_phase==200000&&client_resume==2100000);
 r2_ble_client_seek(2200000,false);r2_ble_client_seek(2800000,false);assert(client_phase==200000);
 client_phase=0;client_resume=0;client_aligned=client_search_seen=false;group_client=false;
}
static void bound_owner(void){
 struct k_thread dedicated,foreign;
 transport_started=owner_bound=false;transport_owner=NULL;
 assert(r2_ble_owner_bind(NULL)==-EINVAL);
 assert(!r2_ble_owner_bind(&dedicated));assert(r2_ble_owner_bind(&foreign)==-EALREADY);
 memset(peers,0,sizeof peers);stopping=false;self=0;nodes=2;
 struct peer *p=&peers[1];p->conn=&connection;p->connected=p->discovered=1;p->att_mtu=226;
 p->completion[0]=(struct tx_context){p,0};uint8_t wire[R2_AUTH_BYTES]={12,R2_AUTH_HELLO};
 fake_current=&foreign;assert(r2_ble_send(1,wire)==-EWOULDBLOCK&&!p->tx_busy);
 fake_current=NULL;assert(r2_ble_send(1,wire)==-EWOULDBLOCK&&!p->tx_busy);
 connection.refs=2;r2_ble_owner_deadline(fake_time+50000);fake_current=&dedicated;assert(!r2_ble_send(1,wire)&&p->tx_busy);tx_done(&connection,&p->completion[0]);
 transport_started=true;owner_bound=false;assert(r2_ble_owner_bind(&foreign)==-EALREADY);
 fake_current=NULL;transport_owner=NULL;owner_bound=false;transport_started=false;
}
static void stale_dispatch(void){++peers[1].incarnation;}
static void disconnect_dispatch(void){callbacks.disconnected(&connection,19);}
static void expired_dispatch(void){fake_time=owner_deadline;}
static void bridge_deadline_lifetime(void){
 struct k_thread dedicated;transport_owner=&dedicated;fake_current=&dedicated;
 memset(peers,0,sizeof peers);group_id=0;stopping=false;self=0;nodes=2;
 struct peer *p=&peers[1];p->conn=&connection;p->connected=p->discovered=1;p->att_mtu=226;
 p->incarnation=9;connection.refs=2;
 for(unsigned j=0;j<2;j++)p->completion[j]=(struct tx_context){p,j};
 uint8_t wire[R2_AUTH_BYTES]={12,R2_AUTH_HELLO};
 unsigned calls=fake_gatt_calls;r2_ble_owner_deadline(fake_time+50000);
 fake_gatt_result=-ENOMEM;assert(r2_ble_send(1,wire)==-ENOMEM&&!p->tx_busy);
 assert(connection.refs==2&&fake_gatt_calls==calls+1&&!gatt_request.pending);
 /* All allocating GATT paths run on the real required SYS context boundary. */
 assert(gatt_call(1,GATT_MTU,0,NULL)==-ENOMEM);
 assert(gatt_call(1,GATT_READ,0,NULL)==-ENOMEM);
 assert(gatt_call(1,GATT_DISCOVER,0,NULL)==-ENOMEM);
 fake_gatt_result=0;fake_early_tx=true;
 assert(!r2_ble_send(1,wire)&&!p->tx_busy&&connection.refs==2);fake_early_tx=false;
 /* Submission failure returns its reference/slot without fake acceptance. */
 fake_submit_result=-EIO;assert(r2_ble_send(1,wire)==-EIO&&!p->tx_busy&&connection.refs==2);fake_submit_result=0;
 for(unsigned mode=0;mode<3;mode++){
  fake_before_dispatch=mode==0?stale_dispatch:mode==1?disconnect_dispatch:expired_dispatch;
  calls=fake_gatt_calls;r2_ble_owner_deadline(fake_time+50000);
  assert(r2_ble_send(1,wire)<0&&!p->tx_busy&&connection.refs==2);
  assert(fake_gatt_calls==calls);p->connected=1;p->dead=0;p->att_mtu=226;
 }
 fake_before_dispatch=NULL;
 /* Timeout cannot recycle an immutable buffer/params/reference. Deferred job
  * must reject before touching ATT, including after disconnect and shutdown. */
 fake_defer=fake_stale_wake=true;r2_ble_owner_deadline(fake_time+50000);uint64_t until=owner_deadline;
 calls=fake_gatt_calls;assert(r2_ble_send(1,wire)==-ETIMEDOUT);
 assert(fake_time==until&&p->tx_busy==1&&connection.refs==3&&gatt_request.pending);
 assert(r2_ble_send(1,wire)==-EAGAIN&&!r2_ble_closed());
 callbacks.disconnected(&connection,19);r2_ble_poll(fake_time);
 assert(p->conn==&connection&&connection.refs==3);
 r2_ble_stop();fake_dispatch();assert(fake_gatt_calls==calls&&!p->tx_busy&&connection.refs==2&&!gatt_request.pending);
 r2_ble_poll(fake_time);assert(!p->conn&&connection.refs==1);bt_conn_unref(&connection);
 /* An outstanding non-TX call similarly fences parameter retirement. */
 stopping=false;p->conn=&connection;p->connected=1;connection.refs=2;
 p->mtu.func=mtu_done;r2_ble_owner_deadline(fake_time+50000);
 assert(gatt_call(1,GATT_MTU,0,NULL)==-ETIMEDOUT&&connection.refs==3);
 callbacks.disconnected(&connection,19);r2_ble_poll(fake_time);
 assert(p->conn==&connection&&p->mtu.func==mtu_done);
 fake_dispatch();r2_ble_poll(fake_time);assert(!p->conn&&connection.refs==1);bt_conn_unref(&connection);
 fake_defer=false;fake_current=NULL;transport_owner=NULL;owner_deadline=0;
 memset(peers,0,sizeof peers);stopping=false;fake_time=1000000;
 puts("fixed SYS ATT bridge: context, allocation failure, early completion, original deadline, incarnation/ref/slot/params and shutdown PASS");
}
static void telemetry_lamps(void)
{
 uint8_t lamp[R2_AUTH_BYTES]={12,R2_AUTH_HELLO};lamp[44]=255;
 assert(lamp_kind(lamp)==0);lamp[44]=1;lamp[101]=1;assert(lamp_kind(lamp)==1);
 const unsigned kinds[]={R2_AUTH_PREPARE,R2_AUTH_ACK,R2_AUTH_COMMIT,R2_AUTH_SERVICE,R2_AUTH_READY};
 for(unsigned i=0;i<5;i++){lamp[1]=kinds[i];assert(lamp_kind(lamp)==i+2);}
 lamp[1]=1;assert(lamp_kind(lamp)==7);lamp[0]=0;assert(lamp_kind(lamp)==7);
 memset(lamp_times,0,sizeof lamp_times);lamp_times[2][0]=1000000;lamp_times[6][1]=1000000;
 uint8_t lights[8];r2_ble_lamps(lights,1100000);assert(lights[2]==1&&lights[6]==2);
 r2_ble_lamps(lights,1374999);assert(lights[2]==1&&lights[6]==2);
 r2_ble_lamps(lights,1375000);assert(!lights[2]&&!lights[6]);
 /* A real timing event survives every 125ms redraw phase and a 20ms model
  * delay, while retaining RX/TX colors and never inventing a correction. */
 for(unsigned phase=0;phase<125000;phase+=1000){
  memset(lamp_times,0,sizeof lamp_times);
  r2_ble_timing_lamps(false,false,2000000);
  r2_ble_lamps(lights,2000000+20000+phase);assert(lights[1]==1&&!lights[5]);
  r2_ble_timing_lamps(true,true,2100000);
  r2_ble_lamps(lights,2100000+20000+phase);assert(lights[1]==2&&lights[5]==2);
  r2_ble_lamps(lights,2475000);assert(!lights[1]&&!lights[5]);
 }
 r2_ble_timing_lamps(false,true,3000000);r2_ble_lamps(lights,2999999);assert(!lights[1]&&!lights[5]);
 r2_ble_lamps(lights,3000000);assert(lights[1]==1&&lights[5]==1);
}
static void retained_gm_diagnostics(void){
 memset(peers,0,sizeof peers);memset(gm_link_events,0,sizeof gm_link_events);gm_mtu_errors=0;
 nodes=2;self=0;stopping=false;fake_time=1000000;
 struct peer *p=&peers[1];p->conn=&connection;p->identity=UINT64_C(0x123456789abcdef0);p->incarnation=7;p->connected=1;
 uint32_t words[R2_GM_TRANSPORT_WORDS];
 r2_ble_gm_diagnostics(words);mtu_done(&connection,14,&p->mtu);gm_diagnostic_publish();r2_ble_gm_diagnostics(words);
 assert(words[0]==1&&words[14]==1&&words[16]==1000000&&words[18]==(1u|(14u<<8))&&words[19]==7&&words[20]==0x9abcdef0&&words[83]==1);
 fake_time=2000000;mtu_done(&connection,0,&p->mtu);gm_diagnostic_publish();r2_ble_gm_diagnostics(words);
 assert(words[14]==1&&words[16]==1000000&&words[22]==1000000&&words[83]==1);
 fake_time=3000000;mtu_done(&connection,17,&p->mtu);gm_diagnostic_publish();r2_ble_gm_diagnostics(words);
 assert(words[14]==2&&words[16]==1000000&&words[22]==3000000&&words[24]==(1u|(17u<<8))&&words[83]==2);
 r2_ble_disconnect(1);callbacks.disconnected(&connection,19);gm_diagnostic_publish();r2_ble_gm_diagnostics(words);
 assert(words[13]==1&&words[15]==1&&words[30]==(1u|(19u<<8)|(1u<<16)));
 gm_link_events[2]=gm_mtu_errors=UINT32_MAX;mtu_done(&connection,14,&p->mtu);assert(gm_link_events[2]==UINT32_MAX&&gm_mtu_errors==UINT32_MAX);
 memset(peers,0,sizeof peers);memset(gm_link_events,0,sizeof gm_link_events);gm_mtu_errors=0;fake_time=1000000;
 puts("PASS GM diagnostics retain first/recent MTU errors across success, incarnation/identity, local-request evidence and saturation");
}
static void unready_core_discovery(void){
 memset(peers,0,sizeof peers);transport_started=owner_bound=false;transport_owner=NULL;
 stop_error=0;stopping=scanning=advertising=precision_discovery=sync_acquiring=false;
 group_client=relay_server=false;fake_time=1000000;
 const uint8_t own[6]={1,2,3,4,5,0xc0};assert(!r2_ble_group_start(10,7,1,own));
 /* A newly eligible follower has no authenticated core link yet. This is
  * precisely the path that previously escaped the ready-core100ms cap. */
 assert(!control_ready());struct peer *p=&peers[4];p->identity=20;p->address.type=1;p->address.a.val[0]=3;
 p->first_seen=p->advertised_at=fake_time;p->provisional_until=fake_time+5000000;p->retry=1;
 early=false;unsigned before=creates;r2_ble_poll(fake_time);
 assert(creates==before+1&&p->conn==&connection&&p->connect_until==fake_time+100000);
 uint64_t original_provisional=p->provisional_until;unsigned closes=disconnects;
 fake_time+=99999;r2_ble_poll(fake_time);assert(disconnects==closes);
 ++fake_time;r2_ble_poll(fake_time);assert(disconnects==closes+1&&p->provisional_until==original_provisional);
 r2_ble_poll(fake_time+1);assert(p->conn==&connection&&creates==before+1); /* Callback still owns it. */
 callbacks.connected(&connection,2);r2_ble_poll(fake_time+2);
 assert(!p->conn&&connection.refs==1&&scanning);bt_conn_unref(&connection);
 puts("PASS unpaired native GM discovery has the original100ms cap, preserves provisional expiry and waits for cancellation callback/reference closure");
}
static void continuous_core_discovery(void){
 for(unsigned hot=0;hot<2;hot++){
  memset(peers,0,sizeof peers);transport_started=owner_bound=false;transport_owner=NULL;
  stop_error=0;stopping=scanning=advertising=precision_discovery=sync_acquiring=false;
  group_client=relay_server=false;fake_time=1000000;
  const uint8_t own[6]={1,2,3,4,5,0xc0};assert(!r2_ble_group_start(10,7,1,own));
  struct bt_conn control={.address={.type=1,.a={{2,0,0,0,0,0xc0}}},.refs=2};
  struct peer *p=&peers[1];p->conn=&control;p->identity=20;p->address=control.address;
  p->connected=p->discovered=p->mtu_done=1;p->att_mtu=226;p->authenticated=true;p->mtu.func=mtu_done;
  r2_ble_topology(1,3);precision_serving=!hot;
  assert(control_ready());r2_ble_poll(fake_time);
  printf("healthy core HOT=%u scan=%u window=%u interval=%u\n",hot,scanning,scan_window,scan_interval);fflush(stdout);
  assert(scanning&&scan_window==0x08&&scan_interval==0x20);
  /* A healthy ready core still initiates a fresh unknown G1 candidate using
   * the one existing transient owner, never its established control slot. */
  p=&peers[4];p->identity=30;p->address.type=1;p->address.a.val[0]=3;
  p->first_seen=p->advertised_at=fake_time;p->provisional_until=fake_time+5000000;p->retry=1;
  early=false;unsigned before=creates;r2_ble_poll(fake_time);
  assert(creates==before+1&&p->conn==&connection&&peers[1].conn==&control);
  unsigned closes=disconnects;fake_time+=99999;r2_ble_poll(fake_time);assert(disconnects==closes);
  ++fake_time;r2_ble_poll(fake_time);assert(disconnects==closes+1&&last_disconnect==&connection);
  callbacks.connected(&connection,2);r2_ble_poll(fake_time+1);
  assert(!peers[4].conn&&peers[1].conn==&control&&r2_ble_ready(1)&&scanning);
  r2_ble_stop();callbacks.disconnected(&control,19);r2_ble_poll(fake_time+2);
  assert(r2_ble_closed()&&control.refs==1&&connection.refs==1);
  bt_conn_unref(&control);bt_conn_unref(&connection);
 }
 puts("PASS serving and HOT cores continuously scan5/20 and admit fresh transient discovery with exact reference closure");
}
int main(int argc,char **argv)
{
 if(argc>1&&!strcmp(argv[1],"gm-mtu")){gm_peer_first_mtu();return 0;}
 if(argc==2&&!strcmp(argv[1],"gm-continuous")){continuous_core_discovery();return 0;}
  if(argc==2&&!strcmp(argv[1],"gm-unready")){unready_core_discovery();return 0;}
 retained_gm_diagnostics();
 if(argc==2&&!strcmp(argv[1],"lamps")){telemetry_lamps();puts("PASS actual telemetry pulse/125ms redraw phases/RX-TX/expiry");return 0;}
 bound_owner();
 bridge_deadline_lifetime();
 cold_window_rendezvous();
 rc_cold_scan_opportunities();
 passive_discovery_hint();
 targeted_discovery();
 repeated_group_rejoin();
 field_client_initiates();control_obligations();group_discovery_fallback();
 r2_ble_client_bootstrap(true,false);assert(!r2_ble_admission_urgent(UINT64_C(900000000)));
 r2_ble_client_bootstrap(true,true);assert(r2_ble_admission_urgent(1));
 r2_ble_client_bootstrap(false,false);assert(!r2_ble_admission_urgent(UINT64_C(900000000)));
 discovery_budget();
 priority_slots();
 group_admission_lifetime();
 telemetry_lamps();
 const uint8_t addresses[2][6]={{1,0,0,0,0,0xc0},{2,0,0,0,0,0xc0}};
 for(unsigned mode=0;mode<2;mode++)for(unsigned cycle=0;cycle<100;cycle++){
  memset(peers,0,sizeof(peers));r2_ble_stop();r2_ble_poll(999999);assert(r2_ble_closed());assert(!r2_ble_start(0,2,addresses));
  early=mode;peers[1].retry=1;unsigned before=creates;r2_ble_poll(1000000);
  assert(creates==before); /* Elapsed retry without a fresh observation is inert. */
  peers[1].advertised_at=1;r2_ble_poll(1000000);assert(creates==before);
  scan_report(&peers[1].address,0,BT_GAP_ADV_TYPE_ADV_IND,NULL);r2_ble_poll(1000000);
  assert(creates==before+1&&!peers[1].advertised_at);
  if(!early)callbacks.connected(&connection,0);
  assert(peers[1].conn==&connection);
  assert(connection.refs==2); /* exactly one owned application reference */
  /* A successful disconnect request is not a completion. Missing callbacks
   * must retain ownership and refuse closure, including beyond the deadline. */
  r2_ble_stop();
  for(unsigned tick=0;tick<10;tick++){
   r2_ble_poll(1000000+tick*1000000);
   assert(!r2_ble_closed());assert(peers[1].conn==&connection);
   assert(connection.refs==2);
  }
  callbacks.disconnected(&connection,19);
  r2_ble_topology(0,0);stopping=true;r2_ble_poll(1100000);
  assert(!peers[1].conn&&connection.refs==1);
  assert(r2_ble_closed());
  bt_conn_unref(&connection);assert(connection.refs==0);
  stopping=false;r2_ble_poll(3000000);assert(creates==before+1&&!peers[1].conn);
 }
 memset(peers,0,sizeof peers);stopping=true;scanning=advertising=false;group_id=0;fake_time=1000000;
 struct bt_conn orphan={.address={.type=1,.a={{99,0,0,0,0,0xc0}}},.refs=1};
 callbacks.connected(&orphan,0);assert(orphan.refs==2&&!r2_ble_closed());
 /* Pending-create cancellation completes through connected(error), without
  * a disconnected callback. Its retained duplicate owner must drain once. */
 callbacks.connected(&orphan,0x02);r2_ble_poll(1000000);
 assert(orphan.refs==1&&r2_ble_closed());
 callbacks.connected(&orphan,0);assert(orphan.refs==2&&!r2_ble_closed());
 unsigned attempts=disconnects;disconnect_result=-EIO;r2_ble_poll(1000000);
 assert(stop_error==-EIO&&disconnects==attempts+1&&orphan.refs==2&&!r2_ble_closed());
 fake_time=2000000;callbacks.connected(&orphan,0);r2_ble_poll(2999999);assert(stop_error==-EIO&&orphan.refs==2);
 r2_ble_poll(3000000);assert(stop_error==-ETIMEDOUT&&disconnects==attempts+1&&!r2_ble_closed());
 callbacks.disconnected(&orphan,19);r2_ble_poll(3000001);assert(orphan.refs==1&&r2_ble_closed()&&r2_ble_stop_error()==-ETIMEDOUT);
 assert(r2_ble_start(0,2,addresses)==-ETIMEDOUT); /* Fault stays fenced despite real drain. */
 disconnect_result=0;stop_error=0;fake_time=1000000;
 memset(peers,0,sizeof peers);nodes=1;stopping=false;group_client=true;relay_server=false;
 client_window=true;scanning=true;
 for(uint64_t t=10000000;t<=10200000;t+=2000)discovery_opportunity_poll(t);
 assert(r2_ble_admission_scan_opportunity());assert(!r2_ble_admission_scan_opportunity());
 /* Quiet and an owned ACL cannot count as a cold discovery opportunity. */
 client_window=false;
 for(uint64_t t=11000000;t<=11200000;t+=2000)discovery_opportunity_poll(t);
 assert(!r2_ble_admission_scan_opportunity());client_window=true;peers[0].conn=&connection;
 for(uint64_t t=12000000;t<=12200000;t+=2000)discovery_opportunity_poll(t);
 assert(!r2_ble_admission_scan_opportunity());peers[0].conn=NULL;
 discovery_opportunity_poll(13000000);discovery_opportunity_poll(13200000);
 assert(!r2_ble_admission_scan_opportunity()); /* long unsampled owner gap */
 group_client=true;client_resume=0;client_phase=0;
 r2_ble_client_defer(2500000);assert(!r2_ble_client_window_open(2100000,400000));
 r2_ble_client_defer(2200000);assert(client_resume==2500000);
 assert(r2_ble_client_window_open(3100000,400000));client_resume=0;
 group_client=client_window=bootstrap_needed=true;field_profile=false;bootstrap_urgent=false;
 assert(r2_ble_admission_reserves_scan(50000)&&!r2_ble_admission_reserves_scan(100000));
 bootstrap_urgent=true;assert(r2_ble_admission_reserves_scan(150000)&&!r2_ble_admission_reserves_scan(200000));
 client_window=false;assert(!r2_ble_admission_reserves_scan(50000));
 uint64_t challenge;random_result=0;assert(!r2_ble_admission_nonce(&challenge)&&challenge);
 random_result=1;assert(r2_ble_admission_nonce(&challenge)==-EIO);
 random_result=-EIO;assert(r2_ble_admission_nonce(&challenge)==-EIO);
 relay_guest_without_bootstrap();
 memset(peers,0,sizeof peers);group_client=true;relay_server=false;sync_acquiring=false;scanning=false;
 client_phase=0;client_resume=0;client_aligned=false;
 assert(!r2_ble_client_align(1400000,1813000)); /* old active half */
 assert(r2_ble_client_align(1650000,2063000));
 assert(client_phase==787000&&client_resume==2150000);
 assert(!r2_ble_client_window_open(1700000,450000));
 assert(r2_ble_client_window_start(2787000)==2787000);
 assert(r2_ble_client_window_open(2787000,450000));
 assert(r2_ble_client_window_open(3236999,450000));
 assert(!r2_ble_client_window_open(3237000,450000));
 assert(!r2_ble_client_align(3650000,4000000)); /* no repeated phase jumps */
 puts("PASS actual transport: early/late connected callback,200 connection lifecycles,exact reference balance");
}

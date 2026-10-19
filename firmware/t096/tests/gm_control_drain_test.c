/* MIT. Copyright (c) 2026 Jonathan Yantis.
 * Production GM owner with hostile synchronous HCI/callback interleavings. */
#define bt_le_scan_stop unused_fake_scan_stop
#define bt_conn_disconnect unused_fake_disconnect
#include "ble_transport_fake.h"
#undef bt_le_scan_stop
#undef bt_conn_disconnect
static int stop_result,close_result;
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
int main(void){
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
 fake_time=2999999;duplicate_poll(fake_time);assert(!stop_error&&orphan.refs==2&&disconnects==calls+1);
 callbacks.connected(&orphan,0);assert(duplicates[3].until==3000000); /* no rearm */
 fake_time=3000000;duplicate_poll(fake_time);assert(stop_error==-ETIMEDOUT&&orphan.refs==2);
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

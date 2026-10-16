#include "ble_transport_fake.h"
static int sys_csrand_get(void *out,size_t n){memset(out,0x5a,n);return 0;}
#include "../protocol/ble_transport.c"
static struct bt_conn connection;
static int bt_conn_le_create(const bt_addr_le_t *a,const void *create,const void *params,struct bt_conn **out){connection.address=*a;connection.refs=2;*out=&connection;return 0;}
#include <stdio.h>
int main(void){
 uint8_t own[6]={1,2,3,4,5,0xc0};assert(!r2_ble_group_start(1,1,1,own));assert(r2_ble_group_start(1,1,1,own)==-EBUSY);
 for(unsigned i=0;i<1000;i++){
  struct peer *p=&peers[1];p->identity=9;p->authenticated=true;p->incarnation=i+1;p->conn=&connection;connection.refs=2;p->connected=1;
  r2_ble_stop();assert(!r2_ble_closed()&&r2_ble_group_start(1,1,1,own)==-EBUSY);
  callbacks.disconnected(&connection,19);r2_ble_poll(1100000);assert(r2_ble_closed()&&connection.refs==1);bt_conn_unref(&connection);assert(connection.refs==0);
  assert(!r2_ble_group_start(1,1,1,own));assert(!peers[1].identity&&!peers[1].authenticated&&!peers[1].conn&&!peers[1].tx_busy&&peers[1].completion[0].peer==&peers[1]);
 }
 assert(queue_purges==1000);puts("PASS actual transport1000 owned stop/restarts: active/pending connection cannot restart, callbacks/references drain, per-peer/auth/tx state and old incoming queue reset, no leaked application connection references");
}

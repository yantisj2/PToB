/* Actual transport and election core; refused controller scan, synthetic GPS. */
#define bt_le_scan_start old_fake_scan_start
#include "ble_transport_fake.h"
#undef bt_le_scan_start
static int bt_le_scan_start(const void *p,void (*cb)(const bt_addr_le_t *,int8_t,uint8_t,struct net_buf_simple *)){return -EIO;}
static int sys_csrand_get(void *out,size_t n){memset(out,0x5a,n);return 0;}
#include "../protocol/ble_transport.c"
#include "network.h"
#include <stdio.h>
static int bt_conn_le_create(const bt_addr_le_t *a,const void *b,const void *c,struct bt_conn **out){return -EBUSY;}
int main(void){
 uint8_t own[6]={1,2,3,4,5,0xc0};assert(!r2_ble_group_start(1,1,1,own));
 struct r2_network n;assert(!r2_network_init(&n,0,4,1,100,1000,40000,1000000));r2_network_capabilities(&n,1,1);
 for(fake_time=1000000;fake_time<70000000;fake_time+=10000){
  r2_ble_poll(fake_time);
  if(r2_ble_stop_error()){r2_network_stop(&n,fake_time);break;}
  struct r2_clock c={0};c.view.source_available=true;c.view.source_epoch=1;
  c.last_pair_edge=c.last_rmc=fake_time/1000000*1000000;c.last_pair_utc=1791504000000000LL+c.last_pair_edge;
  r2_network_tick(&n,&c,fake_time);
 }
 printf("SCAN_FAULT error=%d local_gm=%u stopped=%u at=%llu\n",r2_ble_stop_error(),n.local_gm,n.stopped,(unsigned long long)fake_time);fflush(stdout);
 assert(r2_ble_stop_error()==-EIO&&!n.local_gm&&n.stopped);
 struct r2_transport_fault f;r2_ble_fault_snapshot(&f);assert(f.error==-EIO&&f.operation==4);
 puts("PASS failed public scan start cannot silently qualify an isolated GM; first controller fault retained");
}

/* Production transport; model the public host's EALREADY/data semantics. */
#define bt_le_adv_start old_fake_adv_start
#define bt_le_adv_stop old_fake_adv_stop
#define bt_le_adv_update_data old_fake_adv_update
#include "ble_transport_fake.h"
#undef bt_le_adv_start
#undef bt_le_adv_stop
#undef bt_le_adv_update_data
static bool air_running;
static unsigned air_starts,air_updates;
static uint8_t air_hint[23];
static int adv_update_result;
static int bt_le_adv_start(const void *p,const void *data,size_t count,const void *sd,size_t sn){
 if(air_running)return -EALREADY;
 const struct bt_data *ad=data;assert(count==2&&ad[1].data_len==23);
 memcpy(air_hint,ad[1].data,23);air_running=true;air_starts++;return 0;
}
static int bt_le_adv_update_data(const struct bt_data *ad,size_t count,const struct bt_data *sd,size_t sn){
 if(adv_update_result){if(adv_update_result==-EAGAIN)air_running=false;return adv_update_result;}
 assert(air_running&&count==2&&ad[1].data_len==23);memcpy(air_hint,ad[1].data,23);air_updates++;return 0;
}
static int bt_le_adv_stop(void){air_running=false;adv_stops++;return 0;}
static int sys_csrand_get(void *out,size_t n){memset(out,0x5a,n);return 0;}
#include "../protocol/ble_transport.c"
#include <stdio.h>
static int bt_conn_le_create(const bt_addr_le_t *a,const void *b,const void *c,struct bt_conn **out){return -EBUSY;}
int main(void){
 uint8_t own[6]={1,2,3,4,5,0xc0};assert(!r2_ble_group_start(1,1,1,own));
 fake_time=1000000;r2_ble_group_client(true);r2_ble_group_relay(true);r2_ble_client_window(true);r2_ble_poll(fake_time);
 assert(air_running&&(air_hint[2]&64));unsigned starts=air_starts;
 /* Independent source becomes eligible while its connectable ad is live. */
 fake_time+=200000;r2_ble_group_client(false);r2_ble_group_relay(false);r2_ble_poll(fake_time);
 printf("AIR_HINT after_candidate=0x%02x starts=%u updates=%u\n",air_hint[2],air_starts,air_updates);fflush(stdout);
 assert(!(air_hint[2]&64));assert(air_starts==starts);
 fake_time+=200000;r2_ble_group_client(true);r2_ble_group_relay(true);r2_ble_poll(fake_time);assert(air_hint[2]&64);
 unsigned updates=air_updates;fake_time+=200000;r2_ble_poll(fake_time);assert(air_updates==updates);
 adv_update_result=-EAGAIN;fake_time+=200000;r2_ble_group_client(false);r2_ble_poll(fake_time);assert(!advertising&&!air_running&&!r2_ble_stop_error()&&!advertised_discovery_valid);
 adv_update_result=0;fake_time+=200000;r2_ble_poll(fake_time);assert(air_running&&!(air_hint[2]&64)&&!r2_ble_stop_error());
 adv_update_result=-EIO;fake_time+=200000;r2_ble_group_client(true);r2_ble_group_relay(true);r2_ble_poll(fake_time);assert(r2_ble_stop_error()==-EIO);
 puts("PASS actual owner changes discovery hints in both directions, unchanged polling adds no updates, incoming-connection race restarts bounded, real update failure contained");
}

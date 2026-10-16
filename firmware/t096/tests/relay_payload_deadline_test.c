/* Exact production install/expiry branches; fake only controller/clock boundary. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
struct r2_capture {uint64_t epoch,tick;};
struct r2_relay_prepared {uint64_t valid_until_tick,published_until_tick;};
struct r2_radio_scope {uint64_t until_us;};
struct bt_data {unsigned type,data_len;const uint8_t *data;};
#define BT_DATA_MANUFACTURER_DATA 255
static struct {uint64_t origin_until_tick;unsigned tx_corrections;} timing;
static void *pa=&timing;
static unsigned own=1,root=0,burns,closes,withdraws;
static int update_result,error;static uint64_t update_delay;
static uint64_t clock_us,data_until_tick,data_retry_at,retry_at,updated_at;
static unsigned published_correction;
static uint64_t k_uptime_ticks(void){return clock_us;}
static uint64_t k_ticks_to_us_floor64(uint64_t t){return t;}
static bool r2_capture_now(struct r2_capture *p){*p=(struct r2_capture){1,clock_us};return true;}
static bool r2_relay_precision_commit(void *s,const struct r2_relay_prepared *q,struct r2_capture now){
 if(now.tick>=q->valid_until_tick)return false;++burns;return true;
}
static int bt_le_per_adv_set_data(void *p,const struct bt_data *d,unsigned n){assert(p&&n==1&&d->data_len==188);clock_us+=update_delay;return update_result;}
static void fault(int rc){if(rc)error=rc;}
static void pa_close(unsigned reason){assert(reason==3);pa=0;++closes;}
static void r2_relay_precision_downstream_withdraw(void *s){++withdraws;}
#include "relay_payload_under_test.h"
int main(void){
 uint8_t data[190]={0};struct r2_relay_prepared q={200000,200000};
 data_until_tick=100000;clock_us=90000;update_result=-ENOBUFS;
 assert(!install(&q,data,(struct r2_radio_scope){0})&&burns==1&&!error);
 assert(data_until_tick==100000&&pa);expiry_check(99999);assert(pa);
 expiry_check(100000);assert(!pa&&closes==1&&withdraws==1); /* failed update cannot renew old deadline */
 pa=&timing;data_until_tick=100000;update_result=0;
 assert(install(&q,data,(struct r2_radio_scope){0})&&burns==2&&data_until_tick==200000);
 expiry_check(100000);assert(pa&&closes==1);expiry_check(200000);assert(!pa&&closes==2);
 pa=&timing;data_until_tick=100000;clock_us=90000;update_result=-EIO;
 assert(!install(&q,data,(struct r2_radio_scope){0})&&error==-EIO&&data_until_tick==100000&&burns==3);
 clock_us=200000;update_result=0;assert(!install(&q,data,(struct r2_radio_scope){0})&&burns==3);
 clock_us=90000;update_delay=15000;error=0;
 assert(!install(&q,data,(struct r2_radio_scope){0})&&error==-ETIMEDOUT&&burns==4);
 assert(data_until_tick==100000); /* late successful controller return is not continuity proof */
 return 0;
}

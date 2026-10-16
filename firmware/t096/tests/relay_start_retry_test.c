/* Execute the exact production pa_start body with only controller/publication
 * boundaries faked. This is an allocation/retry test, not RF or codec proof. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
struct r2_source_clock {int unused;};
struct r2_capture {uint64_t epoch,tick;};
struct r2_relay_prepared {uint64_t valid_until_tick;};
struct r2_radio_scope {uint64_t until_us;};
struct bt_le_adv_param {unsigned id,sid,options,interval_min,interval_max;};
struct bt_le_per_adv_param {unsigned interval_min,interval_max;};
#define BT_LE_ADV_OPT_NO_2M 1
#define BT_LE_ADV_OPT_EXT_ADV 2
#define BT_LE_ADV_OPT_USE_IDENTITY 4
#define BT_LE_EXT_ADV_START_DEFAULT 0
static unsigned timing,creates,deletes,starts,own=1,root=0;
static uint64_t clock_us;
static uint64_t k_uptime_ticks(void){return clock_us;}
static uint64_t k_ticks_to_us_floor64(uint64_t t){return t;}
static bool r2_capture_now(struct r2_capture *c){*c=(struct r2_capture){1,clock_us};return true;}static uint64_t tx_lifecycle,retry_at;
static bool pa_started,ext_started;static void *pa;static int error;
static uint64_t ready_at=100000,permission_until=200000;
static bool r2_relay_precision_train(unsigned *t,uint64_t epoch){assert(epoch==1);++*t;return true;}
static void fault(int rc){if(rc)error=rc;}
static int bt_le_ext_adv_create(const struct bt_le_adv_param *p,void *cb,void **out){assert(!pa);++creates;*out=&timing;return 0;}
static int bt_le_per_adv_set_param(void *p,const struct bt_le_per_adv_param *q){assert(p&&q->interval_min==50);return 0;}
static bool prepare(const struct r2_source_clock *s,struct r2_capture n,struct r2_radio_scope a,int64_t utc,uint64_t us,bool new_train,struct r2_relay_prepared *q,uint8_t ad[190]){
 q->valid_until_tick=permission_until;return us>=ready_at&&us<permission_until;
}
static bool install(const struct r2_relay_prepared *q,const uint8_t ad[190],struct r2_radio_scope a){if(clock_us>=q->valid_until_tick)return false;++timing;return true;}
static void pa_close(unsigned reason){assert(pa);++deletes;pa=0;}
static int bt_le_per_adv_start(void *p){assert(p);++starts;return 0;}
static int bt_le_ext_adv_start(void *p,int x){assert(p);return 0;}
static uint64_t pa_requested_at;static unsigned phase_failures;
#include "relay_start_under_test.h"
static void poll(uint64_t us){clock_us=us;if(!pa&&us>=retry_at)pa_start(0,(struct r2_capture){1,us},(struct r2_radio_scope){permission_until},0,us);}
int main(void){
 for(uint64_t us=0;us<ready_at;us+=2)poll(us);
 assert(!creates&&!deletes&&!starts&&!tx_lifecycle&&!timing&&!error);
 poll(ready_at);assert(creates==1&&starts==1&&pa_started&&ext_started&&timing==1);
 pa_close(0);pa_started=ext_started=false;
 ready_at=300000;poll(permission_until);poll(UINT64_MAX-10);
 assert(creates==1&&deletes==1&&starts==1&&tx_lifecycle==1&&!error);
 return 0;
}

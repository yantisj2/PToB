/* Actual PA owner with cancellation/termination at hostile public boundaries. */
#include "radio_service_fake.h"
typedef int atomic_t;
static int atomic_get(const atomic_t *p){return *p;}
static bool atomic_cas(atomic_t *p,int old,int value){if(*p!=old)return false;*p=value;return true;}
struct k_thread {int unused;};
#define K_THREAD_STACK_DEFINE(n,s) static char n[s]
#define K_THREAD_STACK_SIZEOF(n) sizeof(n)
#define K_NO_WAIT 0
#define K_MSEC(n) (n)
static uint64_t k_uptime_ticks(void){return 2000;}
static uint64_t k_ticks_to_us_floor64(uint64_t t){return t;}
static void k_sleep(int n){}
static void k_thread_create(struct k_thread *t,char *s,size_t z,void (*f)(void*,void*,void*),void *a,void *b,void *c,int p,int o,int d){}
static void k_thread_name_set(struct k_thread *t,const char *n){}

#include "../src/radio_service.c"
#include <stdio.h>
#include <stdlib.h>
#include "relay_precision.h"
static struct bt_le_per_adv_sync fixture;
static bool owned,armed;
static unsigned mode,deletes;
static void end(void){struct bt_le_per_adv_sync_term_info i={12,&target};owned=false;terminated(&fixture,&i);}
static int bt_le_per_adv_sync_create(const struct bt_le_per_adv_sync_param *p,struct bt_le_per_adv_sync **out){
 owned=true;*out=&fixture;
 if(mode==1)end();
 return 0;
}
static int bt_le_per_adv_sync_delete(struct bt_le_per_adv_sync *object){
 assert(object==&fixture);++deletes;
 if(mode==2&&deletes==1)return 0; /* cancel remains pending */
 if(mode==3){end();return -EINVAL;} /* natural termination wins */
 if(mode==4)return -EIO; /* controller still owns it */
 end();return 0;
}
static struct bt_le_per_adv_sync *bt_le_per_adv_sync_lookup_addr(const bt_addr_le_t *a,uint8_t sid){return owned?&fixture:NULL;}
static unsigned bootstrap_requests,refresh_requests;
void r2_group_admission_refresh_anchor(void){++refresh_requests;}
void r2_group_admission_require_anchor(void){++bootstrap_requests;}
void r2_ble_precision_discovery(bool enabled){}
void r2_ble_sync_acquiring(bool enabled){}
bool r2_ble_initiating(void){return mode==6;}
bool r2_ble_admission_due(uint64_t now){return mode==14;}
bool r2_ble_admission_reserves_scan(uint64_t now){return mode==14&&now%1000000<200000;}
bool r2_capture_now(struct r2_capture *out){*out=(struct r2_capture){16000000,1,R2_CAPTURE_NOW};return true;}
bool r2_capture_radio(uint32_t raw,uint32_t epoch,struct r2_capture *out){*out=(struct r2_capture){raw,epoch,R2_CAPTURE_RADIO};return true;}
static bool queued;static struct r2_radio_record packet;
bool r2_radio_capture_pop(struct r2_radio_record *out){if(!queued)return false;*out=packet;queued=false;return true;}
void r2_radio_capture_stats(struct r2_radio_capture_stats *out){*out=(struct r2_radio_capture_stats){.armed=armed};}
bool r2_ble_client_align(uint64_t now,uint64_t utc){return false;}
bool r2_ble_client_window_open(uint64_t now,uint32_t width){return true;}
uint64_t r2_ble_client_window_start(uint64_t now){return now;}
bool r2_capture_now_at(struct r2_capture *c,uint64_t *at){*at=2000;return r2_capture_now(c);}
int main(void){
 mode=16;struct r2_enrollment roster={.count=2};assert(!r2_radio_service_init(0,&roster));r2_radio_service_activate();
 if(mode==16){
  public_client=true;client_clock=(struct r2_group_client){.passive=true,.source=true,.root=999,
   .boot=9,.source_epoch=3,.publication=7,.at=1000,.until=1501000,.uncertainty_us=40000,.utc_us=1791500000000000ULL};
  client_scope=(struct r2_radio_scope){.boot=9,.source_epoch=3,.publication_epoch=7,
   .root_identity=999,.root=1,.follower=true,.until_us=1501000};
  enrollment.count=2;enrollment.identity[0]=111;enrollment.identity[1]=999;
  active_scope=client_scope;r2_radio_precision_scope(&precision,client_scope,1);
  precision.servo.count=7;follower_frame=12345;client_utc_offset=123456;
  pt_input.roster=enrollment;pt_input.client=client_clock;pt_input.client.passive=false;
  for(unsigned epoch=1;epoch<5;epoch++){
   pt_input.source_ready=epoch%2;pt_input.source_epoch=epoch;
   assert(precision_needed(2000));precision_step(2000);
   assert(public_client&&precision.servo.count==7&&follower_frame==12345);
   assert(active_scope.until_us==1501000&&client_clock.at==1000);
  }
  /* Same authenticated origin moves from synthetic slot1 to admitted slot2. */
  pt_input.roster.count=3;pt_input.roster.identity[1]=555;pt_input.roster.identity[2]=999;
  pt_input.grant=client_scope;pt_input.grant.root=2;pt_input.grant.until_us=1400000;
  precision_step(3000);
  assert(!public_client&&active_scope.root==2&&precision.scope.root==2);
  assert(precision.servo.count==7&&follower_frame==12345&&follower_utc_offset==123456);
  assert(active_scope.until_us==1400000);
  /* Losing local GNSS/control candidacy returns to public reception without
   * changing the same authenticated source clock. */
  pt_input.client.passive=true;pt_input.source_ready=false;pt_input.source_epoch++;
  precision_step(3500);assert(public_client&&precision.scope.root==1&&precision.servo.count==7);
  assert(follower_frame==12345&&client_utc_offset==123456);
  pt_input.client.passive=false;precision_step(3600);assert(!public_client&&precision.servo.count==7);
  /* Actual changed source still retires history; stale grants never run. */
  pt_input.grant.source_epoch++;precision_step(4000);assert(!precision.servo.count&&!follower_frame);
  assert(!precision_needed(1400000));
  puts("Shadow GNSS and pending control admission preserve BLE history; same-origin roster transfer preserves frame/offset, changed source and expiry retire PASS");return 0;
 }
 return 1;
}

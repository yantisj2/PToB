/* Production passive owner recovery and sync close; only controller boundary fake. */
#include "relay_precision.h"
#include "radio_capture.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
static struct r2_relay_precision timing;
static bool window_recovering,scan_started,creating,established,cancelling,cancel_established,found;
static uint32_t window_faults,window_recoveries,window_fault_site;
static uint64_t window_fault_at,window_fault_deadline,window_recovery_until,rx_close_until,last_rx;
static uintptr_t rx_owner;
static int error,lock,target;
typedef int k_spinlock_key_t;
static int k_spin_lock(int *p){return 0;}
static void k_spin_unlock(int *p,int k){}
struct bt_le_per_adv_sync {int value;};
static struct bt_le_per_adv_sync object,*sync_object;
static void *pa;
static bool live,immediate,armed;
static uint64_t now,deferred;
static unsigned deletes;
static int64_t k_uptime_ticks(void){return now;}
static int64_t k_ticks_to_us_floor64(int64_t t){return t;}
static void fault(int rc){if(rc&&!error)error=rc;}
static void discovery(bool enable){}
static void pa_close(unsigned reason){assert(!pa);}
void r2_ble_field_admission_window(bool allow,bool renewal){assert(!allow&&!renewal);}
void r2_ble_client_defer(uint64_t until){if(until>deferred)deferred=until;}
void r2_ble_sync_acquiring(bool pending){}
void r2_radio_capture_stats(struct r2_radio_capture_stats *s){memset(s,0,sizeof(*s));s->armed=armed;}
static int bt_le_per_adv_sync_delete(struct bt_le_per_adv_sync *s){assert(s==&object);++deletes;if(immediate)live=false;return 0;}
static struct bt_le_per_adv_sync *bt_le_per_adv_sync_lookup_addr(int *addr,unsigned sid){assert(sid==12);return live?&object:NULL;}
#include "relay_sync_close_under_test.h"
#include "relay_window_under_test.h"
static struct {bool field_profile;} enrollment;
static bool local_path,terminated_during_create;
static unsigned own,relay,flushed,creates;
static uint64_t sync_until,first_rx_until,rx_lifecycle;
static bool r2_ble_initiating(void){return false;}
int r2_radio_capture_flush(void){return 0;}
static void r2_group_admission_direct_opportunity(uint64_t n){}
struct bt_le_per_adv_sync_param {unsigned sid,skip,timeout;int addr;};
static void bt_addr_le_copy(int *a,int *b){*a=*b;}
static int bt_le_per_adv_sync_create(struct bt_le_per_adv_sync_param *p,struct bt_le_per_adv_sync **out){assert(!live);assert(now>=deferred);++creates;live=true;*out=&object;return 0;}
#include "relay_follower_under_test.h"
static void fresh(void){
 error=0;window_recovering=false;rx_close_until=0;deletes=0;deferred=0;armed=false;
 live=established=true;sync_object=&object;creating=cancelling=false;
 timing.upstream.servo.count=17;timing.upstream.servo.fit_valid=true;
 timing.upstream.servo.output.valid=true;timing.map[0].used=true;
}
int main(void){
 fresh();immediate=true;now=1501000;miss_window(now,1500000,1);
 assert(!error&&!window_recovering&&!sync_pending()&&deletes==1);
 assert(window_faults==1&&window_recoveries==1&&window_fault_site==1);
 assert(window_fault_at==1501000&&window_fault_deadline==1500000&&deferred==2001000);
 assert(!timing.map[0].used&&!timing.upstream.servo.output.valid);
 assert(timing.upstream.servo.count==17&&timing.upstream.servo.fit_valid);
 assert(!recover_window(now));
 now=deferred-1;follower(now,false);assert(!creates&&!sync_pending());
 now=3000000;follower(now,true);assert(creates==1&&sync_pending()&&!error);
 assert(!timing.upstream.servo.output.valid); /* Actual correction still required. */
 fresh();immediate=false;now=3001000;miss_window(now,3000000,2);
 assert(window_recovering&&sync_pending()&&!error);
 now+=1000;assert(recover_window(now)&&window_recovering&&deletes==1);
 live=false;now+=1000;assert(recover_window(now)&&!window_recovering&&!error);
 assert(deferred==now+500000&&!sync_pending());
 fresh();immediate=false;now=5001000;miss_window(now,5000000,3);
 now=window_recovery_until;assert(recover_window(now)&&error==-ETIMEDOUT&&window_recovering);
 fresh();immediate=false;now=8001000;rx_close_until=now+1000;
 miss_window(now,8000000,1);now+=1000;assert(recover_window(now)&&error==-ETIMEDOUT); /* Never reset older drain deadline. */
 fresh();immediate=true;now=9001000;armed=true;miss_window(now,9000000,1);
 assert(window_recovering&&!sync_pending());armed=false;++now;assert(recover_window(now)&&!window_recovering);
 puts("PASS production window recovery: revoke maps/retain history, exact original drain, late close hard fault, capture-owner fence, quiet defer");
}

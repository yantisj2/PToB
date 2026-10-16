#include "ble_service.h"
#include "runtime_profile.h"
#include "group_admission.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define K_FOREVER -1
static int lock,stop_requested;
static bool started=true,transport_stopped,profile_rc,profile_target_rc,profile_pending;
static bool profile_supported=true;static uint8_t profile_source_caps=3;
static bool relay_selected,relay_transition,relay_profile_set,relay_retiring;
static uint32_t profile_changes;
static unsigned profile_phase;
static uint64_t physical_boot=1234,profile_identity=UINT64_C(0x1000000000000001),profile_reset_until,close_deadline;
static uint64_t relay_activation_until,relay_transition_until,direct_check_due;
static struct r2_local_demand local_demand;
static struct r2_network net;
static struct r2_enrollment roster;
static struct r2_service_status status;
static uint64_t now=3000000;
static bool ble_closed,radio_closed,relay_closed,ack;
static unsigned starts,resets,withdraws,activates;
static int start_failure;
static int k_mutex_lock(int *p,int wait){assert(!*p);*p=1;return 0;}
static int k_mutex_unlock(int *p){assert(*p);*p=0;return 0;}
static int atomic_get(int *p){return *p;}
static void atomic_set(int *p,int v){*p=v;}
static void atomic_clear(int *p){*p=0;}
static uint64_t now_us(void){return now;}
void r2_radio_service_withdraw(void){withdraws++;}
void r2_shadow_normal_withdraw(void){withdraws++;}
bool r2_ble_closed(void){return ble_closed;}
bool r2_radio_service_closed(void){return radio_closed;}
bool r2_relay_service_closed(void){return relay_closed;}
int r2_radio_service_reset(uint32_t generation){assert(generation==profile_changes+1);resets++;return ack?0:-EAGAIN;}
int r2_relay_service_profile_reset(uint64_t boot){assert(boot&&relay_closed);return 0;}
void r2_group_admission_reset(void){}
int r2_network_init(struct r2_network *n,uint8_t self,uint8_t count,uint64_t id,uint64_t boot,uint64_t nonce,uint32_t bound,uint64_t at){assert(boot&&nonce&&bound==40000);memset(n,0,sizeof *n);n->identity=id;n->boot=boot;n->count=count;return 0;}
void r2_network_capabilities(struct r2_network *n,uint8_t gm,uint8_t gnss){n->gm_mask=gm;n->gnss_mask=gnss;}
void r2_ble_group_client(bool rc){}
void r2_ble_group_relay(bool rc){}
void r2_radio_service_continuous(bool rc){}
void r2_radio_service_roster(const struct r2_enrollment *r){}
int r2_ble_group_start(uint64_t id,uint64_t group,uint32_t epoch,const uint8_t address[6]){assert(ble_closed&&radio_closed&&relay_closed&&ack);starts++;return start_failure;}
void r2_radio_service_activate(void){activates++;}
#include "group_admission.h"
#include "runtime_profile_under_test.h"
static void fresh(void){memset(&status,0,sizeof status);status.normal=status.active=status.configured=true;status.serving=status.hot_ready=status.standby_authorized=true;status.authority_until=status.role_until=status.service_until=9999999;status.precision.map_valid=status.precision.authorized=true;status.follower_clock_valid=true;stop_requested=0;ble_closed=radio_closed=relay_closed=ack=false;profile_rc=profile_target_rc=profile_pending=false;profile_changes=profile_phase=starts=resets=withdraws=activates=start_failure=0;roster.group.id=1;roster.group.epoch=1;roster.map_bound_us=40000;roster.identity[0]=profile_identity;}
static void drain(void){status.closed=true;ble_closed=radio_closed=relay_closed=true;}
int main(void){
 assert(!r2_profile_default_rc(profile_identity));assert(!r2_profile_default_rc(UINT64_C(0x1000000000000002)));assert(!r2_profile_default_rc(UINT64_C(0x1000000000000003)));assert(!r2_profile_default_rc(UINT64_C(0x1000000000000004)));assert(!r2_profile_default_rc(UINT64_C(0x1000000000000005)));assert(r2_profile_caps(true)==6&&r2_profile_caps(false)==3);
 assert(!r2_profile_relay_root(1,1,10,2));assert(!r2_profile_relay_root(1,0,10,2));assert(!r2_profile_relay_root(1,2,0,2));assert(!r2_profile_relay_root(1,2,10,0));assert(r2_profile_relay_root(1,2,10,2));
 fresh();assert(!r2_service_profile(R2_PROFILE_GM,now/1000)&&!profile_pending&&!withdraws);
 assert(r2_service_profile(R2_PROFILE_RC,0)==-EINVAL);assert(!r2_service_profile(R2_PROFILE_TOGGLE,now/1000));assert(profile_pending&&profile_target_rc&&status.closing&&stop_requested&&!status.serving&&!status.hot_ready&&!status.standby_authorized&&!status.precision.map_valid&&!status.authority_until);
 assert(r2_service_profile(R2_PROFILE_GM,now/1000)==-EBUSY);assert(profile_restart(now)==-EAGAIN&&!starts&&!resets);
 drain();assert(profile_restart(now)==-EAGAIN&&!starts);ack=true;assert(!profile_restart(now));assert(profile_rc&&!profile_pending&&!stop_requested&&status.active&&!status.closed&&starts==1&&activates==1&&net.gm_mask==0&&net.capabilities==6&&!net.local_gm&&net.boot!=physical_boot);
 uint64_t prior=net.boot;
 for(unsigned i=0;i<1000;i++){assert(!r2_service_profile(R2_PROFILE_TOGGLE,now/1000));drain();ack=true;assert(!profile_restart(now));assert(!profile_pending&&net.boot!=prior&&net.boot&&net.capabilities==r2_profile_caps(profile_rc)&&net.gm_mask==(profile_rc?0:1)&&!net.local_gm);prior=net.boot;}
 fresh();assert(!r2_service_profile(1,now/1000));drain();assert(profile_restart(now)==-EAGAIN);assert(profile_restart(now+2000000)==-ETIMEDOUT&&!starts);
 fresh();assert(!r2_service_profile(1,now/1000));status.error=-EIO;drain();assert(profile_restart(now)==-EIO&&!starts);
 puts("PASS production profile request/restart: same-mode idempotence, stale/busy refusal, immediate authority fence, no reuse before real drain/PT ack, clean1001 mode cycles/new scope, original drain plus bounded reset ack, failures cannot restart; defaults derived anew in RAM");
}

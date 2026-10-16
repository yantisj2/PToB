/* Execute the production dispatcher: same-profile restart must drain first. */
#include "zephyr_fake.h"
#include "rc_dynamic_service.h"
#include "rc_shadow.h"
#include <assert.h>
#include <stdio.h>
typedef int atomic_t;
static int atomic_get(const int *p){return *p;}
static void atomic_set(int *p,int v){*p=v;}
static void atomic_clear(int *p){*p=0;}
static uint64_t fake_us=1000000;
int64_t k_uptime_get(void){return fake_us/1000;}
static uint64_t k_uptime_ticks(void){return fake_us;}
static uint64_t k_ticks_to_us_floor64(uint64_t ticks){return ticks;}
static struct r2_service_status gm,rc_state;
static unsigned rc_configures,gm_resumes,target_calls;
static uint64_t applied_parent=UINT64_MAX;
static bool joined=true;
static bool uc_quiet=true;
bool r2_uc_quiescent(void){return uc_quiet;}
int r2_normal_service_configure(const struct r2_identity *id,const struct r2_enrollment *e)
{gm=(struct r2_service_status){.configured=true,.closed=true,.profile_incarnation=id->boot};return 0;}
int r2_dynamic_service_configure(const struct r2_identity *id,const struct r2_enrollment *e)
{assert(joined);rc_configures++;rc_state=(struct r2_service_status){.configured=true,.closed=true,.profile_incarnation=id->boot};return 0;}
int r2_dynamic_service_parent_target(uint64_t parent){applied_parent=parent;++target_calls;return 0;}
void r2_normal_service_clock(const struct r2_clock *c){}
void r2_dynamic_service_clock(const struct r2_clock *c){}
int r2_normal_service_run(uint64_t at){gm.active=gm.normal=true;gm.closed=false;return 0;}
int r2_dynamic_service_run(uint64_t at){rc_state.active=rc_state.normal=true;rc_state.closed=false;joined=false;return 0;}
int r2_normal_service_start(uint32_t s,uint64_t at){return 0;}
int r2_dynamic_service_start(uint32_t s,uint64_t at){return 0;}
int r2_normal_service_preflight(uint32_t s,uint64_t at){return 0;}
int r2_dynamic_service_preflight(uint32_t s,uint64_t at){return 0;}
void r2_normal_service_stop(void){gm.closing=true;}
void r2_dynamic_service_stop(void){rc_state.closing=true;}
int r2_normal_service_close_result(void){return gm.closed?0:-EAGAIN;}
int r2_dynamic_service_close_result(void){return rc_state.closed&&joined?0:-EAGAIN;}
void r2_normal_service_snapshot(struct r2_service_status *out){*out=gm;}
void r2_dynamic_service_snapshot(struct r2_service_status *out){*out=rc_state;}
void r2_normal_service_ui(struct r2_ui_model *m){}
void r2_dynamic_service_ui(struct r2_ui_model *m){}
void r2_normal_service_supervise(void){}
void r2_dynamic_service_supervise(void){}
int r2_normal_service_name(const char *n){return 0;}
int r2_dynamic_service_name(const char *n){return 0;}
uint32_t r2_normal_service_stack_unused(void){return 1024;}
uint32_t r2_dynamic_service_stack_unused(void){return 1024;}
int r2_normal_service_resume_gm(uint64_t incarnation,uint32_t count)
{assert(gm.closed&&!gm.active);gm_resumes++;gm.profile_incarnation=incarnation;gm.profile_pending=true;return 0;}
void r2_dynamic_service_transport_snapshot(uint32_t out[13]){memset(out,0,52);}
void r2_dynamic_service_acquisition_snapshot(uint32_t out[20]){memset(out,0,80);}
void r2_shadow_normal_withdraw(void){}
void r2_dynamic_service_gm_admission_snapshot(uint32_t out[7]){memset(out,0,7*sizeof *out);}
void r2_dynamic_service_connection_snapshot(uint32_t out[20]){memset(out,0,20*sizeof *out);}
#include "../src/rc_profile_service.c"
static void gm_closed(void){gm.closed=true;gm.active=false;}
static void gm_ack(void){gm.profile_pending=false;gm.closed=gm.closing=false;gm.active=true;}
int main(void){
 struct r2_identity id={.silicon="1000000000000002",.boot=1234};
 struct r2_enrollment e={.rc_dynamic=true,.group={.id=1,.epoch=1,.psk={1}},.local_capabilities=3};
 e.rc7_boot_rc=true; /* Public role comes from explicit enrollment. */
 assert(!r2_service_configure(&id,&e));assert(target&&r2_switch_rc_active()&&rc_configures==1&&target_calls==1&&applied_parent==0);
 assert(!enrollment.rc7_exclude_direct_gm&&!test_parent);
 assert(!r2_service_run(k_uptime_get()));assert(!r2_service_restart(k_uptime_get()));
 rc_state.closed=true;rc_state.active=false;joined=true;r2_service_supervise();assert(!pending&&rc_configures==2&&applied_parent==0);
 assert(!r2_service_test_parent(UINT64_C(0x1000000000000003),k_uptime_get()));
 rc_state.closed=true;rc_state.active=false;joined=true;r2_service_supervise();assert(applied_parent==UINT64_C(0x1000000000000003));
 assert(!r2_service_profile(R2_PROFILE_GM,k_uptime_get()));rc_state.closed=true;rc_state.active=false;joined=true;r2_service_supervise();assert(resuming&&pending);gm_ack();r2_service_supervise();assert(!pending&&!test_parent&&!enrollment.rc7_exclude_direct_gm);
 assert(!r2_service_profile(R2_PROFILE_RC,k_uptime_get()));gm_closed();r2_service_supervise();assert(!pending&&r2_switch_rc_active()&&applied_parent==0);
 puts("PASS TC02 auto cold boot/same-profile recovery/RC re-entry; explicit diagnostic pin only, GM completion clears it");
}

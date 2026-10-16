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
static unsigned rc_configures,gm_resumes;
static bool joined=true;
static bool uc_quiet=true;
bool r2_uc_quiescent(void){return uc_quiet;}
int r2_normal_service_configure(const struct r2_identity *id,const struct r2_enrollment *e)
{gm=(struct r2_service_status){.configured=true,.closed=true,.profile_incarnation=id->boot};return 0;}
int r2_dynamic_service_configure(const struct r2_identity *id,const struct r2_enrollment *e)
{assert(joined);rc_configures++;rc_state=(struct r2_service_status){.configured=true,.closed=true,.profile_incarnation=id->boot};return 0;}
int r2_dynamic_service_parent_target(uint64_t parent){return 0;}
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
int main(void)
{
 struct r2_identity id={.silicon="1000000000000001",.boot=1234};
 struct r2_enrollment e={.rc_dynamic=true,.group={.id=1,.epoch=1,.psk={1}},.local_capabilities=3};
 assert(!r2_service_configure(&id,&e));assert(!r2_service_run(k_uptime_get()));
 assert(!r2_service_restart(k_uptime_get()));assert(pending&&!target);
 r2_service_supervise();assert(!gm_resumes&&pending);
 gm_closed();r2_service_supervise();assert(gm_resumes==1&&resuming&&pending);
 r2_service_supervise();assert(pending&&!changes);gm_ack();r2_service_supervise();
 assert(!pending&&changes==1&&incarnation!=id.boot);
 assert(!r2_service_profile(1,k_uptime_get()));gm_closed();r2_service_supervise();
 assert(r2_switch_rc_active()&&changes==2&&rc_configures==1);
 uint64_t before=incarnation;assert(!r2_service_restart(k_uptime_get()));
 rc_state.closed=true;rc_state.active=false;r2_service_supervise();assert(pending&&rc_configures==1);
 joined=true;r2_service_supervise();assert(!pending&&changes==3&&rc_configures==2&&incarnation!=before);
 assert(!r2_service_profile(0,k_uptime_get()));
 rc_state.closed=true;rc_state.active=false;joined=true;uc_quiet=false;
 r2_service_supervise();assert(failure==-EBUSY&&!pending&&gm_resumes==1&&r2_switch_rc_active());
 r2_service_stop();assert(r2_service_restart(k_uptime_get())==-EBUSY);
 puts("PASS actual GM/RC dispatcher: same-profile restart, original drain, RC join, GM acknowledgement, fresh scope and explicit-stop inhibition");
}

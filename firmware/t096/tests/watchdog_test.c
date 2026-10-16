/* Production policy and driver/observer bodies at fake public boundaries. */
#include "zephyr_fake.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#define R2_WATCHDOG 1
#define __noinit
#define __DMB() ((void)0)
#define __DSB() ((void)0)
#define RESET_WATCHDOG 2u
#define RESET_PIN 1u
#define RESET_SOFTWARE 4u
#define RESET_POR 8u
#define RESET_BROWNOUT 16u
#define RESET_CPU_LOCKUP 32u
#define RESET_LOW_POWER_WAKE 64u
#define RESET_DEBUG 128u
#define WDT_FLAG_RESET_SOC 1u
#define R2_SOURCE_SHA "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
#define R2_WATCHDOG_BINDING R2_SOURCE_SHA
typedef int32_t atomic_t;
typedef int32_t atomic_val_t;
static int32_t atomic_get(const atomic_t *p){return *p;}
static int32_t atomic_set(atomic_t *p,int32_t value){int32_t old=*p;*p=value;return old;}
static int32_t atomic_inc(atomic_t *p){return (*p)++;}
struct wdt_timeout_cfg {struct {uint32_t min,max;} window;unsigned flags;};
static unsigned installs,setups,feeds,boot_requests;
static int install_error,setup_error;
static uint64_t fake_ms;
int64_t k_uptime_get(void){return (int64_t)fake_ms;}
static uint32_t k_uptime_get_32(void){return (uint32_t)fake_ms;}
static int wdt_install_timeout(const struct device *d,const struct wdt_timeout_cfg *cfg)
{assert(cfg->window.min==0&&cfg->window.max==1000&&cfg->flags==WDT_FLAG_RESET_SOC);installs++;return install_error?install_error:0;}
static int wdt_setup(const struct device *d,uint8_t options){assert(!options);setups++;return setup_error;}
static int wdt_feed(const struct device *d,int channel){assert(!channel);feeds++;return 0;}
static int hwinfo_clear_reset_cause(void){return 0;}
static int r2_bootloader_request(void);
static int r2_console_reply(const char *,size_t,uint64_t);
static jmp_buf maintenance_exit;
static void test_sleep(int n){assert(n==125);longjmp(maintenance_exit,1);}
#define k_sleep test_sleep
#include "../src/watchdog.c"
#undef k_sleep
static int r2_bootloader_request(void){boot_requests++;return -EIO;}
static char sent[768];
static int r2_console_reply(const char *s,size_t n,uint64_t expiry)
{assert(n<sizeof sent&&expiry==fake_ms+250);memcpy(sent,s,n);sent[n]=0;return 0;}
static struct r2_identity id={.silicon="1000000000000001",.boot=1234};
static void clear_runtime(void)
{
 sequence=report_at=report_rc=report_useful=report_pending=report_generation=report_allowed=report_worker_stalled=0;
 intent=main_at=reset_reason=restart_request=restart_error=retries=silent_since=0;
 policy=(struct r2_wd_policy){0};memset(previous,0,sizeof previous);parent_channel=-1;
}
static void advance(uint32_t now)
{fake_ms=now;r2_watchdog_main_beat();}
static void policy_cases(bool rc)
{
 struct r2_wd_policy p={0};uint32_t span=rc?R2_WD_RC_MS:R2_WD_GM_MS,start=UINT32_MAX-100;
 assert(r2_wd_policy_step(&p,start,true,rc,false,false,0)==R2_WD_NONE);
 for(unsigned retry=1;retry<=3;retry++){
  assert(r2_wd_policy_step(&p,start+span-1,true,rc,false,false,retry-1)==R2_WD_NONE);
  assert(r2_wd_policy_step(&p,start+span,true,rc,false,false,retry-1)==R2_WD_RESTART&&p.retries==retry);
  assert(r2_wd_policy_step(&p,start+span+1,true,rc,false,true,retry-1)==R2_WD_NONE);
  start+=span+2;
  assert(r2_wd_policy_step(&p,start,true,rc,false,false,retry)==R2_WD_NONE&&p.retries==retry);
 }
 assert(r2_wd_policy_step(&p,start+span,true,rc,false,false,3)==R2_WD_RESET);
 if(rc){
  /* One brief map cannot erase the exhausted retry history. Only sustained
   * useful clock during the current attempt may clear it. */
  p.since=start+span;
  for(unsigned ms=0;ms<=R2_WD_RC_HEALTHY_MS;ms+=125)
   assert(r2_wd_policy_step(&p,start+span+ms,true,rc,true,false,3)==R2_WD_NONE);
  assert(!p.retries);
 }else assert(r2_wd_policy_step(&p,start+span,true,rc,true,false,3)==R2_WD_NONE&&!p.retries);
 assert(r2_wd_policy_step(&p,start+span+1,false,rc,false,false,3)==R2_WD_NONE&&!p.armed);
 p=(struct r2_wd_policy){0};r2_wd_policy_step(&p,0,true,rc,false,false,0);
 assert(r2_wd_policy_step(&p,span,true,rc,false,false,0)==R2_WD_RESTART);
 assert(r2_wd_policy_step(&p,span+4249,true,rc,false,true,0)==R2_WD_NONE);
 assert(r2_wd_policy_step(&p,span+4250,true,rc,false,true,0)==R2_WD_RESET);
}
int main(void)
{
 policy_cases(false);policy_cases(true);
 memset((void *)r2_watchdog_capsule,0xa5,sizeof r2_watchdog_capsule);
 assert(!r2_watchdog_boot(&id)&&installs==1&&setups==1);
 unsigned fed=feeds;
 if(!setjmp(maintenance_exit))watchdog_owner(NULL,NULL,NULL);
 assert(feeds==fed+1);
 bool mode;assert(!r2_watchdog_restore_rc(&mode));
 struct r2_service_status s={.normal=true,.active=true,.profile_rc=true};
 advance(100);s.at=100000;r2_watchdog_enable(true);r2_watchdog_observe(&s,true);
 assert(watchdog_step(100));
 /* Frozen true snapshots and arbitrary control packets cannot keep sync. */
 s.follower_clock_valid=s.precision.map_valid=s.precision.authorized=s.precision.correction_age_valid=true;
 s.precision.locked=true;s.precision.correction_age_ms=0;s.service_until=UINT64_MAX;
 advance(200);s.at=200000;r2_watchdog_observe(&s,true);assert(watchdog_step(200));
 advance(1200);r2_watchdog_observe(&s,true);assert(watchdog_step(1200)&&!report_useful);
 advance(300200);s.at=300200000;s.follower_clock_valid=false;r2_watchdog_observe(&s,true);assert(watchdog_step(300200));
 assert(r2_watchdog_take_restart()==1&&policy.recovering);
 assert(!r2_watchdog_take_restart());
 advance(300201);s.profile_pending=true;s.at=300201000;r2_watchdog_observe(&s,true);assert(watchdog_step(300201));
 advance(300202);s.profile_pending=false;s.profile_changes=1;s.at=300202000;
 s.follower_clock_valid=false;r2_watchdog_observe(&s,true);assert(watchdog_step(300202)&&!policy.recovering&&policy.retries==1);
 advance(300203);s.follower_clock_valid=true;s.at=300203000;r2_watchdog_observe(&s,true);
 assert(watchdog_step(300203)&&policy.retries==1);
 for(unsigned ms=300328;ms<=308203;ms+=125){advance(ms);s.at=(uint64_t)ms*1000;r2_watchdog_observe(&s,true);assert(watchdog_step(ms));}
 assert(!policy.retries);
 sequence|=1;advance(308204);assert(watchdog_step(308204));sequence++;
 restart_request=2;r2_watchdog_enable(false);assert(!r2_watchdog_take_restart());
 r2_watchdog_observe(&s,false);assert(watchdog_step(308204)&&!policy.armed);
 assert(!watchdog_step(310204)&&reset_reason==WD_CORE_STALL);
 fed=feeds;if(!setjmp(maintenance_exit))watchdog_owner(NULL,NULL,NULL);assert(feeds==fed);
 /* Intermittent maps and same-profile scopes cannot postpone RC recovery. */
 clear_runtime();assert(!r2_watchdog_boot(&id));r2_watchdog_enable(true);
 s=(struct r2_service_status){.active=true,.profile_rc=true};
 for(unsigned ms=0;ms<=300000;ms+=100){
  advance(ms);s.at=(uint64_t)ms*1000;
  bool brief=ms%240000==0;
  s.follower_clock_valid=s.precision.map_valid=s.precision.authorized=s.precision.correction_age_valid=s.precision.locked=brief;
  s.service_until=s.at+500000;s.profile_changes=ms/60000;
  r2_watchdog_observe(&s,true);assert(watchdog_step(ms));
 }
 assert(r2_watchdog_take_restart()==1);
 /* Fresh maps without PLL lock, or with expired source, are not recovery. */
 s.follower_clock_valid=s.precision.map_valid=s.precision.authorized=s.precision.correction_age_valid=true;
 s.precision.locked=false;r2_watchdog_observe(&s,true);assert(!report_useful);
 s.precision.locked=true;s.relay.held_time=true;r2_watchdog_observe(&s,true);assert(!report_useful);
 clear_runtime();assert(!r2_watchdog_boot(&id));
 /* Role-appropriate activity: a valid serving GM needs fresh associated
  * TX; a mere source candidate, connection or stale correction is not sync. */
 s=(struct r2_service_status){.active=true,.serving=true,.source_eligible=true,
  .precision={.authorized=true,.rms_valid=true}};
 advance(310205);s.at=310205000;r2_watchdog_observe(&s,true);assert(report_useful);
 s.serving=false;r2_watchdog_observe(&s,true);assert(!report_useful);
 s.follower_clock_valid=s.precision.map_valid=s.precision.correction_age_valid=true;
 s.precision.correction_age_ms=2000;r2_watchdog_observe(&s,true);assert(!report_useful);
 s.service_until=UINT64_MAX;s.precision.correction_age_ms=1999;r2_watchdog_observe(&s,true);assert(report_useful);
 /* NO TIME with a responsive owner gets the complete five-minute
  * acquisition period. Heard packets and a connected main do not reset it. */
 clear_runtime();assert(!r2_watchdog_boot(&id));r2_watchdog_enable(true);
 s=(struct r2_service_status){.active=true,.profile_rc=true};
 for(unsigned ms=0;ms<300000;ms+=100){
  advance(ms);s.at=(uint64_t)ms*1000;r2_watchdog_observe(&s,true);
  assert(watchdog_step(ms)&&!r2_watchdog_take_restart()&&!report_worker_stalled);
 }
 advance(300000);s.at=300000000;r2_watchdog_observe(&s,true);
 assert(watchdog_step(300000)&&r2_watchdog_take_restart()==1);
 /* A formerly valid map whose actual lifetime expired is still NO TIME. */
 s.follower_clock_valid=s.precision.map_valid=s.precision.authorized=s.precision.correction_age_valid=true;
 s.service_until=300000000;r2_watchdog_observe(&s,true);assert(!report_useful);
 /* Reproduce the field failure: main progresses but owner time is frozen.
  * No early reset while its original controller operation may still finish. */
 clear_runtime();assert(!r2_watchdog_boot(&id));r2_watchdog_enable(true);
 s=(struct r2_service_status){.active=true,.profile_rc=true,.at=10000000};
 advance(12999);r2_watchdog_observe(&s,true);assert(watchdog_step(12999));
 advance(13000);r2_watchdog_observe(&s,true);
 assert(!watchdog_step(13000)&&reset_reason==WD_RC_WORKER_STALL&&previous_bound(&id));
 assert(r2_watchdog_capsule[4]==WD_RC_WORKER_STALL&&r2_watchdog_capsule[5]==1);
 /* Explicit stop, maintenance and profile transition cannot resurrect RC. */
 for(unsigned inhibit=0;inhibit<3;inhibit++){
  clear_runtime();assert(!r2_watchdog_boot(&id));r2_watchdog_enable(true);advance(13000);
  s.closing=inhibit==0;s.profile_pending=inhibit==1;
  r2_watchdog_observe(&s,inhibit!=2);assert(watchdog_step(13000));
 }
 clear_runtime();assert(!r2_watchdog_boot(&id));advance(10);
 r2_watchdog_fatal(2);assert(!watchdog_step(10)&&reset_reason==WD_FATAL+2&&previous_bound(&id));
 id.reset_cause=RESET_WATCHDOG;clear_runtime();assert(!r2_watchdog_boot(&id));
 char output[768];assert(r2_watchdog_restore_rc(&mode));r2_watchdog_console(output,sizeof output);assert(strstr(sent,"\"parent_ms\":1000"));
 assert(r2_watchdog_reset_kind()==R2_UI_RESET_WD_FAULT);
 /* Torn capsule and wrong silicon/source cannot restore a profile. */
 capsule(WD_NO_SYNC);r2_watchdog_capsule[20]^=1;clear_runtime();assert(!r2_watchdog_boot(&id)&&!r2_watchdog_restore_rc(&mode));
 capsule(WD_NO_SYNC);r2_watchdog_capsule[12]^=1;r2_watchdog_capsule[33]=crc(r2_watchdog_capsule);
 clear_runtime();assert(!r2_watchdog_boot(&id)&&!r2_watchdog_restore_rc(&mode));
 capsule(WD_NO_SYNC);id.silicon[0]='5';clear_runtime();assert(!r2_watchdog_boot(&id)&&!r2_watchdog_restore_rc(&mode));id.silicon[0]='4';
 /* Maintenance refuses insufficient ORIGINAL budget, then uses a real WDT
  * reset and the bound one-shot continuation before hardware rearming. */
 clear_runtime();assert(!r2_watchdog_boot(&id));advance(20);
 assert(r2_watchdog_bootloader(1269)==-ETIMEDOUT&&!reset_reason);
 if(!setjmp(maintenance_exit))r2_watchdog_bootloader(1270);
 assert(reset_reason==WD_UF2&&previous_bound(&id));
 unsigned before=installs;clear_runtime();assert(!r2_watchdog_boot(&id)&&boot_requests==1&&installs==before+1);
 assert(r2_watchdog_reset_kind()==R2_UI_RESET_WD_RECOVERY);
 /* A maintenance deadline fault cannot leave a live main feeding forever
  * after UI/controller shutdown. Preserve its error and restore service. */
 advance(30);r2_watchdog_enable(false);r2_watchdog_restart_result(-ETIMEDOUT);
 assert(!watchdog_step(30)&&reset_reason==WD_RECOVERY_FAILED&&previous_bound(&id));
 assert(r2_watchdog_capsule[32]==(uint32_t)-ETIMEDOUT);
 clear_runtime();assert(!r2_watchdog_boot(&id)&&r2_watchdog_reset_kind()==R2_UI_RESET_WD_RECOVERY);
 clear_runtime();install_error=-ENODEV;assert(r2_watchdog_boot(&id)==-ENODEV);install_error=0;
 puts("PASS production watchdog: GM20/RC5, initial+3 retries, wrap, stale snapshots, cancellation, recovery, core/fatal, CRC bindings, deadline/refusal/UF2 continuation; hardware timing remains UNKNOWN");
}

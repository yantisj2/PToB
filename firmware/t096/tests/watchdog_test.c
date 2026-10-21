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
static void (*on_atomic_get)(const atomic_t *);
static int32_t atomic_get(const atomic_t *p){if(on_atomic_get)on_atomic_get(p);return *p;}
static int32_t atomic_set(atomic_t *p,int32_t value){int32_t old=*p;*p=value;return old;}
static int32_t atomic_inc(atomic_t *p){return (*p)++;}
struct wdt_timeout_cfg {struct {uint32_t min,max;} window;unsigned flags;};
static unsigned installs,setups,feeds,boot_requests;
static int install_error,setup_error;
static uint64_t fake_ms;
static uint32_t fake_sub_us;
static uint64_t k_uptime_ticks(void){return fake_ms*1000+fake_sub_us;}
static uint64_t k_ticks_to_us_floor64(uint64_t ticks){return ticks;}
int64_t k_uptime_get(void){return (int64_t)fake_ms;}
static uint32_t k_uptime_get_32(void){return (uint32_t)fake_ms;}
static int wdt_install_timeout(const struct device *d,const struct wdt_timeout_cfg *cfg)
{assert(cfg->window.min==0&&cfg->window.max==30000&&cfg->flags==WDT_FLAG_RESET_SOC);installs++;return install_error?install_error:0;}
static int wdt_setup(const struct device *d,uint8_t options){assert(!options);setups++;return setup_error;}
static int wdt_feed(const struct device *d,int channel){assert(!channel);feeds++;return 0;}
static int hwinfo_clear_reset_cause(void){return 0;}
static int r2_bootloader_request(void);
static int r2_console_note(const char *,size_t,uint64_t);
static jmp_buf maintenance_exit;
static void test_sleep(int n){assert(n==125);longjmp(maintenance_exit,1);}
#define k_sleep test_sleep
#ifdef R2_LORA
#include "lora_service.h"
static struct r2_gm_coordination lora_coordination;
static unsigned lora_checks;
void r2_lora_coordination(struct r2_gm_coordination *out){*out=lora_coordination;}
void r2_lora_watchdog_check(void){lora_checks++;}
#endif
#include "../src/watchdog.c"
#undef k_sleep
static int r2_bootloader_request(void){boot_requests++;return -EIO;}
static char sent[1536];
static unsigned console_reports;
static int r2_console_note(const char *s,size_t n,uint64_t expiry)
{assert(n<sizeof sent&&expiry==fake_ms+250);memcpy(sent,s,n);sent[n]=0;++console_reports;return 0;}
static struct r2_identity id={.silicon="1000000000000001",.boot=1234};
static void clear_runtime(void)
{
 on_atomic_get=NULL;
 sequence=report_at=report_rc=report_useful=report_pending=report_generation=report_allowed=report_worker_stalled=0;
 intent=main_at=reset_reason=restart_request=restart_error=retries=silent_since=0;
 observations=useful_observations=future_observations=stale_observations=refusal_flags=0;
 policy=(struct r2_wd_policy){0};memset(previous,0,sizeof previous);parent_channel=-1;
#ifdef R2_LORA
 lora_coordination=(struct r2_gm_coordination){0};lora_checks=0;
#endif
}
static bool step_at(uint32_t now){fake_ms=now;return watchdog_step();}
static const atomic_t *preempt_target;
static unsigned preempt_kind;
static struct r2_service_status preempt_service;
static void preempt_main(const atomic_t *read){
 if(read!=preempt_target)return;
 on_atomic_get=NULL;++fake_ms;r2_watchdog_main_beat();
 if(preempt_kind==2){preempt_service.at=fake_ms*1000;preempt_service.service_until=preempt_service.at+500000;r2_watchdog_observe(&preempt_service,true);}
}
static void advance(uint32_t now)
{fake_ms=now;fake_sub_us=0;r2_watchdog_main_beat();}
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
 assert(r2_wd_policy_step(&p,span+R2_WD_RECOVERY_MS-1,true,rc,false,true,0)==R2_WD_NONE);
 assert(r2_wd_policy_step(&p,span+R2_WD_RECOVERY_MS,true,rc,false,true,0)==R2_WD_RESET);
}
static void owner_failure_cases(void)
{
 for(unsigned rc=0;rc<2;rc++)for(unsigned pending=0;pending<2;pending++){
  clear_runtime();r2_watchdog_enable(true);
  struct r2_service_status s={.active=true,.profile_rc=rc,.gm_cold=true,.at=100000,.profile_pending=pending};
  advance(100);r2_watchdog_observe(&s,true);assert(step_at(100));
  advance(100+WD_RC_WORKER_MS-1);r2_watchdog_observe(&s,true);assert(step_at((uint32_t)fake_ms));
  advance(100+WD_RC_WORKER_MS);r2_watchdog_observe(&s,true);
  assert(!step_at((uint32_t)fake_ms)&&reset_reason==WD_RC_WORKER_STALL);
  s.at=fake_ms*1000;s.profile_pending=false;r2_watchdog_observe(&s,true);assert(!watchdog_step());
 }
 /* Fresh snapshots do not hide a failed original drain or terminal error.
  * The original deadline is neither replaced nor extended by observation. */
 for(unsigned rc=0;rc<2;rc++){
  clear_runtime();r2_watchdog_enable(true);
  struct r2_service_status s={.active=true,.profile_rc=rc,.closing=true,.profile_pending=true,.close_deadline=2100000};
  advance(2099);s.at=fake_ms*1000;r2_watchdog_observe(&s,true);assert(step_at(2099));
  advance(2100);s.at=fake_ms*1000;r2_watchdog_observe(&s,true);assert(!step_at(2100));
  assert(s.close_deadline==2100000&&reset_reason==WD_RC_WORKER_STALL);
  clear_runtime();r2_watchdog_enable(true);s.closed=true;s.error=0;
  advance(2200);s.at=fake_ms*1000;r2_watchdog_observe(&s,true);assert(step_at(2200));
  s.error=-EIO;r2_watchdog_observe(&s,true);assert(!step_at(2200));
 }
 /* Live COLD with no packets/source/precision work remains legitimate;
  * completed transitions can retain historical close deadlines. */
 clear_runtime();r2_watchdog_enable(true);
 struct r2_service_status s={.active=true,.gm_cold=true,.close_deadline=1};
 for(unsigned t=100;t<2*R2_WD_GM_MS;t+=125){advance(t);s.at=fake_ms*1000;r2_watchdog_observe(&s,true);assert(step_at(t)&&!reset_reason&&!restart_request);}
 /* Deliberate maintenance remains outside the normal-service observer. */
 clear_runtime();r2_watchdog_enable(true);s=(struct r2_service_status){.closing=true,.error=-EIO};
 for(unsigned t=100;t<2*WD_RC_WORKER_MS;t+=125){advance(t);r2_watchdog_observe(&s,false);assert(step_at(t));}
 puts("PASS all-profile stale CONTROL/pending30s; terminal error/original close expiry latch; late recovery cannot feed; live COLD and completed transitions do not reset");
}
int main(void)
{
 policy_cases(false);policy_cases(true);
 /* Actual producer preemption at the atomic-read boundary. Core and mailbox
  * timestamps may advance after observer wake; strict ages use a later read. */
 clear_runtime();advance(100);preempt_target=&main_at;preempt_kind=1;on_atomic_get=preempt_main;
 assert(step_at(100)&&!reset_reason&&fake_ms==101);
 clear_runtime();assert(!r2_watchdog_boot(&id));r2_watchdog_enable(true);advance(200);
 preempt_service=(struct r2_service_status){.active=true,.profile_rc=true,.at=200000,.service_until=700000,
  .follower_clock_valid=true,.precision={.map_valid=true,.authorized=true,.correction_age_valid=true,.locked=true}};
 r2_watchdog_observe(&preempt_service,true);
 preempt_target=&sequence;preempt_kind=2;on_atomic_get=preempt_main;
 assert(step_at(200)&&!reset_reason&&policy.healthy_seen&&policy.observed_at==201);
 advance(1200);assert(step_at(1200)&&policy.healthy_seen); /* report age999ms */
 advance(1201);assert(step_at(1201)&&!policy.healthy_seen); /* exactly1s */
 /* The same strict30s core bound applies across uint32 uptime wrap. */
 clear_runtime();fake_ms=UINT32_MAX-999;r2_watchdog_main_beat();
 assert(step_at((uint32_t)(UINT32_MAX-999+WD_CORE_MS-1))&&!reset_reason);
 assert(!step_at((uint32_t)(UINT32_MAX-999+WD_CORE_MS))&&reset_reason==WD_CORE_STALL);
 clear_runtime();

 memset((void *)r2_watchdog_capsule,0xa5,sizeof r2_watchdog_capsule);
 assert(!r2_watchdog_boot(&id)&&installs==2&&setups==2);
 unsigned fed=feeds;
 if(!setjmp(maintenance_exit))watchdog_owner(NULL,NULL,NULL);
 assert(feeds==fed+1);
 bool mode;assert(!r2_watchdog_restore_rc(&mode));
 struct r2_service_status s={.normal=true,.active=true,.profile_rc=true};
 advance(100);s.at=100000;r2_watchdog_enable(true);r2_watchdog_observe(&s,true);
 assert(step_at(100));
 /* The owner publishes native-tick microseconds, often later than this
  * millisecond's floor. A causal fresh snapshot is useful without relaxing
  * actual future, original expiry or owner-staleness fences. */
 s.follower_clock_valid=s.precision.map_valid=s.precision.authorized=s.precision.correction_age_valid=s.precision.locked=true;
 s.service_until=100500000;
 fake_sub_us=732;s.at=100701;r2_watchdog_observe(&s,true);assert(report_useful);
 s.at=100733;r2_watchdog_observe(&s,true);assert(!report_useful&&!report_worker_stalled&&refusal_flags==2&&future_observations==1);
 s.at=100732;s.service_until=100732;r2_watchdog_observe(&s,true);assert(!report_useful&&refusal_flags==16);
 s.service_until=100733;r2_watchdog_observe(&s,true);assert(report_useful);
 s.relay.held_time=true;r2_watchdog_observe(&s,true);assert(!report_useful);s.relay.held_time=false;
 s.precision.authorized=false;r2_watchdog_observe(&s,true);assert(!report_useful);s.precision.authorized=true;
 /* Sub-ms observations sustain recovery past a complete RC timeout. */
 clear_runtime();assert(!r2_watchdog_boot(&id));r2_watchdog_enable(true);
 uint32_t end=R2_WD_RC_MS+200;
 for(unsigned ms=100;ms<=end;ms+=125){
  advance(ms);fake_sub_us=732;s.at=(uint64_t)ms*1000+701;s.service_until=s.at+500000;
  r2_watchdog_observe(&s,true);assert(report_useful&&step_at(ms)&&!r2_watchdog_take_restart());
 }
 assert(!policy.retries&&(uint32_t)(end-policy.since)<125);
 advance(end+1000);s.at=(uint64_t)end*1000+701;s.service_until=UINT64_MAX;
 r2_watchdog_observe(&s,true);assert(report_useful); /* 999299us old */
 fake_sub_us=701;r2_watchdog_observe(&s,true);assert(!report_useful); /* exactly 1s */
 advance(end+WD_RC_WORKER_MS);fake_sub_us=701;r2_watchdog_observe(&s,true);assert(report_worker_stalled&&stale_observations==2&&refusal_flags==4);
 clear_runtime();assert(!r2_watchdog_boot(&id));r2_watchdog_enable(true);
 advance(100);s.at=100000;r2_watchdog_observe(&s,true);assert(step_at(100));
 /* Frozen true snapshots and arbitrary control packets cannot keep sync. */
 s.follower_clock_valid=s.precision.map_valid=s.precision.authorized=s.precision.correction_age_valid=true;
 s.precision.locked=true;s.precision.correction_age_ms=0;s.service_until=UINT64_MAX;
 advance(200);s.at=200000;r2_watchdog_observe(&s,true);assert(step_at(200));
 advance(1200);r2_watchdog_observe(&s,true);assert(step_at(1200)&&!report_useful);
 uint32_t lost=200+R2_WD_RC_MS;
 advance(lost);s.at=(uint64_t)lost*1000;s.follower_clock_valid=false;r2_watchdog_observe(&s,true);assert(step_at(lost));
 assert(r2_watchdog_take_restart()==1&&policy.recovering);
 assert(!r2_watchdog_take_restart());
 advance(lost+1);s.profile_pending=true;s.at=(uint64_t)(lost+1)*1000;r2_watchdog_observe(&s,true);assert(step_at(lost+1));
 advance(lost+2);s.profile_pending=false;s.profile_changes=1;s.at=(uint64_t)(lost+2)*1000;
 s.follower_clock_valid=false;r2_watchdog_observe(&s,true);assert(step_at(lost+2)&&!policy.recovering&&policy.retries==1);
 advance(lost+3);s.follower_clock_valid=true;s.at=(uint64_t)(lost+3)*1000;r2_watchdog_observe(&s,true);
 assert(step_at(lost+3)&&policy.retries==1&&policy.since==lost+3);
 for(unsigned ms=lost+128;ms<=lost+R2_WD_RC_HEALTHY_MS+3;ms+=125){advance(ms);s.at=(uint64_t)ms*1000;r2_watchdog_observe(&s,true);assert(step_at(ms));}
 assert(!policy.retries);
 sequence|=1;advance(lost+R2_WD_RC_HEALTHY_MS+4);assert(step_at(lost+R2_WD_RC_HEALTHY_MS+4));sequence++;
 restart_request=2;r2_watchdog_enable(false);assert(!r2_watchdog_take_restart());
 r2_watchdog_observe(&s,false);assert(step_at(lost+R2_WD_RC_HEALTHY_MS+4)&&!policy.armed);
 assert(!step_at(lost+R2_WD_RC_HEALTHY_MS+4+WD_CORE_MS)&&reset_reason==WD_CORE_STALL);
 fed=feeds;if(!setjmp(maintenance_exit))watchdog_owner(NULL,NULL,NULL);assert(feeds==fed);
 /* Intermittent usable maps postpone continuous-loss recovery. They do not
  * need eight perfect seconds; scope changes alone still do not help. */
 clear_runtime();assert(!r2_watchdog_boot(&id));r2_watchdog_enable(true);
 s=(struct r2_service_status){.active=true,.profile_rc=true};
 for(unsigned ms=0;ms<=2*R2_WD_RC_MS;ms+=100){
  advance(ms);s.at=(uint64_t)ms*1000;
  bool brief=ms%240000==0;
  s.follower_clock_valid=s.precision.map_valid=s.precision.authorized=s.precision.correction_age_valid=s.precision.locked=brief;
  s.service_until=s.at+500000;s.profile_changes=ms/60000;
  r2_watchdog_observe(&s,true);assert(step_at(ms)&&!r2_watchdog_take_restart());
 }
 assert(!policy.retries&&!r2_watchdog_take_restart());
 uint32_t last_usable=policy.since;
 s.follower_clock_valid=false;s.precision.locked=false;
 advance(last_usable+R2_WD_RC_MS-1);s.at=fake_ms*1000;
 r2_watchdog_observe(&s,true);assert(step_at((uint32_t)fake_ms)&&!r2_watchdog_take_restart());
 advance(last_usable+R2_WD_RC_MS);s.at=fake_ms*1000;
 r2_watchdog_observe(&s,true);assert(step_at((uint32_t)fake_ms)&&r2_watchdog_take_restart()==1);
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
 /* NO TIME with a responsive owner gets the complete thirty-minute
  * acquisition period. Heard packets and a connected main do not reset it. */
 clear_runtime();assert(!r2_watchdog_boot(&id));r2_watchdog_enable(true);
 s=(struct r2_service_status){.active=true,.profile_rc=true};
 for(unsigned ms=0;ms<R2_WD_RC_MS;ms+=100){
  advance(ms);s.at=(uint64_t)ms*1000;r2_watchdog_observe(&s,true);
  assert(step_at(ms)&&!r2_watchdog_take_restart()&&!report_worker_stalled);
 }
 advance(R2_WD_RC_MS);s.at=(uint64_t)R2_WD_RC_MS*1000;r2_watchdog_observe(&s,true);
 assert(step_at(R2_WD_RC_MS)&&r2_watchdog_take_restart()==1);
 /* A formerly valid map whose actual lifetime expired is still NO TIME. */
 s.follower_clock_valid=s.precision.map_valid=s.precision.authorized=s.precision.correction_age_valid=true;
 s.service_until=(uint64_t)R2_WD_RC_MS*1000;r2_watchdog_observe(&s,true);assert(!report_useful);
 /* Reproduce the field failure: main progresses but owner time is frozen.
  * No early reset while its original controller operation may still finish. */
 clear_runtime();assert(!r2_watchdog_boot(&id));r2_watchdog_enable(true);
 s=(struct r2_service_status){.active=true,.profile_rc=true,.at=10000000};
 advance(10000+WD_RC_WORKER_MS-1);r2_watchdog_observe(&s,true);assert(step_at(10000+WD_RC_WORKER_MS-1));
 advance(10000+WD_RC_WORKER_MS);r2_watchdog_observe(&s,true);
 assert(!step_at(10000+WD_RC_WORKER_MS)&&reset_reason==WD_RC_WORKER_STALL&&previous_bound(&id));
 assert(r2_watchdog_capsule[4]==WD_RC_WORKER_STALL&&r2_watchdog_capsule[5]==1);
 /* Stuck closing/profile transitions remain owned; deliberate maintenance
  * disables normal-service escalation without recreating any owner. */
 for(unsigned inhibit=0;inhibit<3;inhibit++){
  clear_runtime();assert(!r2_watchdog_boot(&id));r2_watchdog_enable(true);advance(10000+WD_RC_WORKER_MS);
  s.closing=inhibit==0;s.profile_pending=inhibit==1;
  r2_watchdog_observe(&s,inhibit!=2);assert(step_at(10000+WD_RC_WORKER_MS)==(inhibit==2));
 }
 clear_runtime();assert(!r2_watchdog_boot(&id));advance(10);
 r2_watchdog_fatal(2);assert(!step_at(10)&&reset_reason==WD_FATAL+2&&previous_bound(&id));
 id.reset_cause=RESET_WATCHDOG;clear_runtime();assert(!r2_watchdog_boot(&id));
 char output[1536];assert(r2_watchdog_restore_rc(&mode));r2_watchdog_console(output,sizeof output);assert(strstr(sent,"\"parent_ms\":30000")&&strstr(sent,"\"observations\":[")&&strstr(sent,"\"refusal_flags\":"));
 atomic_set(&observations,(atomic_val_t)UINT32_MAX);count_observation(&observations);assert((uint32_t)atomic_get(&observations)==UINT32_MAX);
 unsigned reports=console_reports;r2_watchdog_console(output,sizeof output);assert(console_reports==reports);
 fake_ms+=60000;r2_watchdog_console(output,sizeof output);assert(console_reports==reports+1&&!strstr(sent,"\"previous\":[]"));
 atomic_set(&retries,1);r2_watchdog_console(output,sizeof output);assert(console_reports==reports+2);
 assert(r2_watchdog_reset_kind()==R2_UI_RESET_WD_FAULT);
 /* Torn capsule and wrong silicon/source cannot restore a profile. */
 capsule(WD_NO_SYNC);r2_watchdog_capsule[20]^=1;clear_runtime();assert(!r2_watchdog_boot(&id)&&!r2_watchdog_restore_rc(&mode));
 capsule(WD_NO_SYNC);r2_watchdog_capsule[12]^=1;r2_watchdog_capsule[33]=crc(r2_watchdog_capsule);
 clear_runtime();assert(!r2_watchdog_boot(&id)&&!r2_watchdog_restore_rc(&mode));
 capsule(WD_NO_SYNC);id.silicon[0]='5';clear_runtime();assert(!r2_watchdog_boot(&id)&&!r2_watchdog_restore_rc(&mode));id.silicon[0]='4';
 /* Maintenance refuses insufficient ORIGINAL budget, then uses a real WDT
  * reset and the bound one-shot continuation before hardware rearming. */
 clear_runtime();assert(!r2_watchdog_boot(&id));advance(20);
 assert(r2_watchdog_bootloader(269)==-ETIMEDOUT&&!reset_reason);
 if(!setjmp(maintenance_exit))r2_watchdog_bootloader(270);
 assert(reset_reason==WD_UF2&&previous_bound(&id));
 unsigned before=installs;clear_runtime();assert(!r2_watchdog_boot(&id)&&boot_requests==1&&installs==before+1);
 assert(r2_watchdog_reset_kind()==R2_UI_RESET_WD_RECOVERY);
 /* A maintenance deadline fault cannot leave a live main feeding forever
  * after UI/controller shutdown. Preserve its error and restore service. */
 advance(30);r2_watchdog_enable(false);r2_watchdog_restart_result(-ETIMEDOUT);
 assert(!step_at(30)&&reset_reason==WD_RECOVERY_FAILED&&previous_bound(&id));
 assert(r2_watchdog_capsule[32]==(uint32_t)-ETIMEDOUT);
 clear_runtime();assert(!r2_watchdog_boot(&id)&&r2_watchdog_reset_kind()==R2_UI_RESET_WD_RECOVERY);
 clear_runtime();install_error=-ENODEV;assert(r2_watchdog_boot(&id)==-ENODEV);install_error=0;
 /* LoRa uses the physical parent reset and keeps the exact first cause,
  * stage/op, DMA closure and journal outcome in the source-bound capsule. */
 clear_runtime();assert(!r2_watchdog_boot(&id));advance(40);atomic_set(&report_rc,1);
 r2_watchdog_lora(-ETIMEDOUT,0x383,91,22,-EIO);
 assert(!step_at(40)&&reset_reason==WD_LORA_FAULT&&previous_bound(&id));
 assert(r2_watchdog_capsule[8]==(uint32_t)-ETIMEDOUT&&r2_watchdog_capsule[9]==0x383&&
  r2_watchdog_capsule[10]==22&&r2_watchdog_capsule[11]==91&&r2_watchdog_capsule[32]==(uint32_t)-EIO);
 r2_watchdog_lora(-EIO,0,0,31,0);assert(r2_watchdog_capsule[11]==91);
 clear_runtime();assert(!r2_watchdog_boot(&id)&&r2_watchdog_restore_rc(&mode)&&mode&&r2_watchdog_reset_kind()==R2_UI_RESET_WD_FAULT);
#ifdef R2_LORA
 {
 clear_runtime();assert(!r2_watchdog_boot(&id));r2_watchdog_enable(true);
 s=(struct r2_service_status){.active=true,.self=0,.root=255,.identities={10,20}};
 uint32_t end=R2_WD_GM_MS+60000;
 for(uint32_t ms=0;ms<=end;ms+=100){
  advance(ms);s.at=(uint64_t)ms*1000;
  /* Fake original accepted physical pairs every4s, never query-time renewal. */
  lora_coordination=(struct r2_gm_coordination){.root=20,.boot=100,.epoch=1,.until=(uint64_t)(ms/4000*4000+12000)*1000};
  r2_watchdog_observe(&s,true);assert(report_useful==2&&!useful_observations&&refusal_flags);
  assert(step_at(ms)&&!r2_watchdog_take_restart()&&!reset_reason&&!retries);
 }
 assert(lora_checks>12000&&!s.precision.authorized&&!s.follower_clock_valid);
 /* Expired lease stops that activity immediately; ordinary20min NO_SYNC
  * recovery remains available. It does not become qualified timing. */
 lora_coordination.until=(uint64_t)(end+1)*1000;
 for(uint32_t ms=end+1;ms<end+R2_WD_GM_MS;ms+=100){
  advance(ms);s.at=(uint64_t)ms*1000;r2_watchdog_observe(&s,true);
  assert(!report_useful&&step_at(ms)&&!r2_watchdog_take_restart());
 }
 advance(end+R2_WD_GM_MS);s.at=fake_ms*1000;r2_watchdog_observe(&s,true);
 assert(step_at((uint32_t)fake_ms)&&r2_watchdog_take_restart()==1);
 clear_runtime();r2_watchdog_enable(true);advance(100);s.at=100000;
 lora_coordination=(struct r2_gm_coordination){.root=20,.boot=100,.epoch=1,.until=UINT64_MAX};
 s.profile_rc=true;r2_watchdog_observe(&s,true);assert(!report_useful);
 s.profile_rc=false;s.serving=true;r2_watchdog_observe(&s,true);assert(!report_useful);
 s.serving=false;s.root=0;r2_watchdog_observe(&s,true);assert(!report_useful);
 s.root=255;s.at=0;advance(1000);r2_watchdog_observe(&s,true);assert(!report_useful);
 s.at=1000000;r2_watchdog_observe(&s,true);assert(report_useful==2);
 assert(!step_at(1000+WD_CORE_MS)&&reset_reason==WD_CORE_STALL); /* coordination never feeds a frozen main */
 puts("PASS >21min live LoRa-only GM coordination without NO_SYNC or qualified-time claims; expiry restores20min recovery; RC/wrong-root/stale/core guards retained");
 }
#endif
 owner_failure_cases();
 puts("PASS production watchdog: GM20/RC30, intermittent progress vs sustained retry reset, initial+3 retries, wrap, stale snapshots, cancellation, recovery, core/fatal, CRC bindings, deadline/refusal/UF2 continuation; hardware timing remains UNKNOWN");
}

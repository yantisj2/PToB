/* MIT. Jonathan Yantis, 2026. Production watchdog body at synthetic boundaries. */
#include "zephyr_fake.h"
struct r2_identity;
static bool runtime_bound(const struct r2_identity *);
#define previous_bound runtime_bound
#define main historical_watchdog_main
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#include "watchdog_test.c"
#pragma clang diagnostic pop
#undef main
#undef previous_bound
static bool runtime_bound(const struct r2_identity *p){return r2_watchdog_capsule[0]==WD_MAGIC&&r2_watchdog_capsule[33]==crc(r2_watchdog_capsule)&&!memcmp((const void *)&r2_watchdog_capsule[28],p->silicon,16);}
int main(void){
 assert(!r2_watchdog_boot(&id));
 policy_cases(false);policy_cases(true);cleanup_recovery_budget();owner_failure_cases();
 control_capsule_detail_cases();host_detail_cases();clear_runtime();
 bool mode=true;id.reset_cause=RESET_WATCHDOG;
 memset((void *)&uf2_intent,0xa5,sizeof uf2_intent);assert(!r2_watchdog_boot(&id));
 assert(!r2_watchdog_restore_rc(&mode)&&!previous[0]&&!uf2_intent.magic&&!r2_watchdog_capsule[0]);
 report_rc=1;retries=7;r2_watchdog_fatal(4);assert(runtime_bound(&id));
 clear_runtime();assert(!r2_watchdog_boot(&id)&&!r2_watchdog_restore_rc(&mode)&&!previous[0]&&!r2_watchdog_capsule[0]&&!boot_requests);
 fake_ms=100;parent_channel=0;
 if(!setjmp(maintenance_exit)){(void)r2_watchdog_bootloader(1000);assert(false);}
 assert(uf2_intent.magic==WD_MAGIC&&updater_bound(&id)&&sizeof uf2_intent==88);
 clear_runtime();assert(!r2_watchdog_boot(&id)&&boot_requests==1&&!uf2_intent.magic&&!previous[0]&&!r2_watchdog_restore_rc(&mode));
 clear_runtime();assert(!r2_watchdog_boot(&id)&&boot_requests==1); // consumed once
 puts("PASS operational watchdog state cleared; minimal bound one-shot updater intent only");
}

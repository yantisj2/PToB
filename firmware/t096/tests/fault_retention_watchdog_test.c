/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
/* Actual observer/policy/capsule/boot bodies; synthetic reset and hardware API. */
#define main historical_watchdog_main
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#include "watchdog_test.c"
#pragma clang diagnostic pop
#undef main
static void seed(unsigned reason)
{
 id.boot=1234;id.reset_cause=0;clear_runtime();
 memset((void *)r2_watchdog_capsule,0,sizeof r2_watchdog_capsule);
 assert(!r2_watchdog_boot(&id));advance(100);
 atomic_set(&report_rc,1);capsule(reason,0x81234567);
 assert(watchdog_bound(&id));
}
static void reboot(uint32_t reset,uint64_t boot)
{clear_runtime();id.boot=boot;id.reset_cause=reset;assert(!r2_watchdog_boot(&id));}
int main(void)
{
 assert(sizeof r2_watchdog_capsule==136&&sizeof uf2_intent==88);
 /* Native startup binds physical_boot/silicon before any observer capture.
  * Inherited host-detail cases do not initialize that production boundary. */
 clear_runtime();assert(!r2_watchdog_boot(&id));
 policy_cases(false);policy_cases(true);cleanup_recovery_budget();
 owner_failure_cases();control_capsule_detail_cases();host_detail_cases();
 const unsigned reasons[]={WD_RETRIES_EXHAUSTED,WD_CORE_STALL,WD_RECOVERY_FAILED,
   WD_RC_WORKER_STALL,WD_LORA_FAULT,WD_FATAL+4,WD_NO_SYNC};
 const unsigned kinds[]={R2_UI_RESET_WD_PROFILE,R2_UI_RESET_WD_CORE,R2_UI_RESET_WD_RECOVERY,
   R2_UI_RESET_WD_FAULT,R2_UI_RESET_WD_FAULT,R2_UI_RESET_WD_FAULT,R2_UI_RESET_WATCHDOG};
 for(unsigned i=0;i<sizeof reasons/sizeof *reasons;i++){
  seed(reasons[i]);uint32_t original[WD_WORDS];memcpy(original,(const void *)r2_watchdog_capsule,sizeof original);
  reboot(RESET_WATCHDOG,1235);assert(!memcmp(previous,original,sizeof original));
  assert(!r2_watchdog_capsule[0]&&r2_watchdog_reset_kind()==kinds[i]);
  bool mode=true;assert(!r2_watchdog_restore_rc(&mode)&&mode);mode=false;assert(!r2_watchdog_restore_rc(&mode)&&!mode);
  assert(!intent&&!restart_request&&!restart_error&&!retries&&!report_generation&&!report_rc);
  /* Function statics survive the host reset simulation; move the fake console
   * cadence forward and require an actual new publication. */
  fake_ms=UINT64_C(100000)*(i+1);char line[1536];unsigned reports=console_reports;
  r2_watchdog_console(line,sizeof line);assert(console_reports==reports+1);
  assert(strstr(sent,"\"boot\":\"00000000000004d3\"")&&strstr(sent,"\"previous_boot\":\"00000000000004d2\""));
  assert(strstr(sent,previous_reason_name()));
  if(reasons[i]==WD_NO_SYNC)assert(previous[4]==WD_NO_SYNC&&!strcmp(previous_reason_name(),"UNKNOWN")&&r2_watchdog_reset_kind()==R2_UI_RESET_WATCHDOG);
 }
 /* No CRC-valid evidence from an unrelated reset, factory, build, schema or boot. */
 for(unsigned word=0;word<WD_WORDS;word++){
  seed(WD_CORE_STALL);r2_watchdog_capsule[word]^=1;reboot(RESET_WATCHDOG,1235);assert(!previous[0]);
 }
 for(unsigned word=1;word<=3;word++){
  seed(WD_CORE_STALL);r2_watchdog_capsule[word]=0;r2_watchdog_capsule[33]=crc(r2_watchdog_capsule);
  if(word==3)r2_watchdog_capsule[2]=0; /* fully absent prior boot */
  if(word!=1){r2_watchdog_capsule[2]=r2_watchdog_capsule[3]=0;r2_watchdog_capsule[33]=crc(r2_watchdog_capsule);}
  reboot(RESET_WATCHDOG,1235);assert(!previous[0]);
 }
 for(unsigned word=12;word<=31;word++){
  seed(WD_CORE_STALL);r2_watchdog_capsule[word]^=1;r2_watchdog_capsule[33]=crc(r2_watchdog_capsule);
  reboot(RESET_WATCHDOG,1235);assert(!previous[0]);
 }
 const uint32_t resets[]={0,RESET_PIN,RESET_SOFTWARE,RESET_POR,RESET_BROWNOUT,RESET_CPU_LOCKUP,RESET_WATCHDOG|RESET_POR};
 for(unsigned i=0;i<sizeof resets/sizeof *resets;i++){
  seed(WD_CORE_STALL);reboot(resets[i],1235);assert(!previous[0]);
 }
 seed(WD_CORE_STALL);reboot(RESET_WATCHDOG,1234);assert(!previous[0]);
 seed(WD_CORE_STALL);memset((void *)r2_watchdog_capsule,0xa5,sizeof r2_watchdog_capsule);
 reboot(RESET_WATCHDOG,1235);assert(!previous[0]&&r2_watchdog_reset_kind()==R2_UI_RESET_WATCHDOG);
 /* Maximum-width raw integers still fit the existing1536B health line. */
 for(unsigned i=0;i<WD_WORDS;i++)previous[i]=UINT32_MAX;
 previous[0]=WD_MAGIC;physical_boot=UINT64_MAX;
 intent=report_rc=report_useful=retries=reset_reason=refusal_flags=observations=
  useful_observations=future_observations=stale_observations=(atomic_t)UINT32_MAX;
 silent_since=0;reset_flags=reset_kind=UINT32_MAX;fake_ms=UINT32_MAX;
 unsigned reports=console_reports;char line[1536];r2_watchdog_console(line,sizeof line);
 assert(console_reports==reports+1&&strlen(sent)<sizeof line&&
  !strcmp(sent+strlen(sent)-3,"]}\n")&&strstr(sent,"\"previous_cause\":\"UNKNOWN\""));

 size_t largest=strlen(sent);
 const unsigned labels[]={0,WD_NO_SYNC,WD_RETRIES_EXHAUSTED,WD_CORE_STALL,
  WD_RECOVERY_FAILED,WD_RC_WORKER_STALL,WD_LORA_FAULT,WD_FATAL,UINT32_MAX};
 for(unsigned i=0;i<sizeof labels/sizeof *labels;i++)for(unsigned flag=0;flag<2;flag++){
  /* Keep the32-bit age at its maximum while advancing console cadence. */
  previous[4]=labels[i];report_useful=flag?3:0;fake_ms+=UINT64_C(1)<<32;
  reports=console_reports;r2_watchdog_console(line,sizeof line);
  assert(console_reports==reports+1&&strlen(sent)<sizeof line&&
   !strcmp(sent+strlen(sent)-3,"]}\n")&&strstr(sent,previous_reason_name()));
  if(strlen(sent)>largest)largest=strlen(sent);
 }
 printf("PASS maximum-width integers/all cause labels/booleans JSON%zu of%zu bytes\n",largest,sizeof line);

 /* One-shot maintenance remains separate and cannot restore operational state. */
 seed(WD_CORE_STALL);fake_ms=100;parent_channel=0;
 if(!setjmp(maintenance_exit)){(void)r2_watchdog_bootloader(1000);assert(false);}
 assert(updater_bound(&id));unsigned before=boot_requests;reboot(RESET_WATCHDOG,1235);
 assert(boot_requests==before+1&&!uf2_intent.magic&&!intent&&!restart_request);
 reboot(RESET_WATCHDOG,1236);assert(boot_requests==before+1&&!previous[0]);
 puts("PASS bound watchdog prior-boot diagnostics, all reason classes, corruption/build/factory/reset negatives; policy and one-shot maintenance unchanged");
}

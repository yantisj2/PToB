/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#define R2_FAULT_DURABLE 1
#define main historical_watchdog_main
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#include "watchdog_test.c"
#pragma clang diagnostic pop
#undef main
static void reset_volatile(void)
{
 uint32_t keep[WD_WORDS];memcpy(keep,previous,sizeof keep);
 clear_runtime();memcpy(previous,keep,sizeof keep);pending_attributed=false;
}
static void boot(uint64_t boot_id,uint32_t cause)
{reset_volatile();id.boot=boot_id;id.reset_cause=cause;assert(!r2_watchdog_boot(&id));}
static void empty_records(void)
{memset(previous,0,sizeof previous);memset((void *)r2_watchdog_capsule,0,sizeof r2_watchdog_capsule);}
static void first(unsigned reason)
{empty_records();boot(100,0);advance(100);capsule(reason,0x12345);boot(101,RESET_WATCHDOG);}
int main(void)
{
 memset(previous,0xa5,sizeof previous);memset((void *)r2_watchdog_capsule,0xa5,sizeof r2_watchdog_capsule);boot(1,RESET_POR);
 assert(!previous[0]&&!r2_watchdog_pending_record());
 first(WD_CORE_STALL);assert(pending_attributed&&r2_watchdog_reset_kind()==R2_UI_RESET_WD_CORE);
 uint32_t keep[WD_WORDS];memcpy(keep,previous,sizeof keep);assert(r2_watchdog_pending_record());
 boot(102,RESET_WATCHDOG);
 assert(!pending_attributed&&!memcmp(keep,previous,sizeof keep)&&
  r2_watchdog_reset_kind()==R2_UI_RESET_WATCHDOG&&!strcmp(previous_reason_name(),"UNKNOWN"));
 /* First pending remains older; a NEW terminal decision can classify this reset
  * without mislabelling the older raw pending bytes as that new decision. */
 capsule(WD_LORA_FAULT,0);boot(103,RESET_WATCHDOG);
 assert(!pending_attributed&&!memcmp(keep,previous,sizeof keep)&&r2_watchdog_reset_kind()==R2_UI_RESET_WD_FAULT);
 r2_watchdog_pending_saved(previous[33]^1);assert(r2_watchdog_pending_record());
 r2_watchdog_pending_saved(previous[33]);assert(!r2_watchdog_pending_record()&&!previous[0]);
 capsule(WD_RC_WORKER_STALL,0x9876);boot(104,RESET_WATCHDOG);
 assert(pending_attributed&&previous[2]==103&&previous[4]==WD_RC_WORKER_STALL);
 /* Different-image binding stays original pending context, never current cause. */
 ((uint8_t *)&previous[12])[0]='a';previous[33]=crc(previous);memcpy(keep,previous,sizeof keep);
 boot(105,RESET_POR);assert(!pending_attributed&&r2_watchdog_pending_record()&&!memcmp(keep,previous,sizeof keep));
 assert(r2_watchdog_reset_kind()==R2_UI_RESET_POWER&&!strcmp(previous_reason_name(),"UNKNOWN"));
 /* Foreign factory and every torn/corrupted word are rejected. */
 for(unsigned i=0;i<WD_WORDS;i++){
  memcpy(previous,keep,sizeof keep);previous[i]^=1;boot(106+i,RESET_POR);assert(!previous[0]);
 }
 memcpy(previous,keep,sizeof keep);previous[28]^=1;previous[33]=crc(previous);
 boot(200,RESET_POR);assert(!previous[0]);
 memcpy(previous,keep,sizeof keep);((uint8_t *)&previous[12])[0]='z';previous[33]=crc(previous);
 boot(201,RESET_POR);assert(!previous[0]);
 memcpy(previous,keep,sizeof keep);boot(previous[2],RESET_POR);assert(!previous[0]);
 first(WD_NO_SYNC);assert(pending_attributed&&previous[4]==1&&!strcmp(previous_reason_name(),"UNKNOWN"));
 assert(r2_watchdog_reset_kind()==R2_UI_RESET_WATCHDOG);
 /* A bare hardware reset gets a distinct observation of the NEW boot.
  * Prior fault boot/source/PC stay unknown; no software capsule is fabricated. */
 empty_records();boot(300,RESET_WATCHDOG);
 assert(previous[0]==R2_FAULT_RESET_MAGIC&&previous[1]==2&&previous[2]==300&&
  !previous[4]&&previous[5]==RESET_WATCHDOG&&!pending_attributed&&r2_watchdog_pending_record());
 assert(!strcmp(previous_reason_name(),"UNKNOWN"));memcpy(keep,previous,sizeof keep);
 boot(301,RESET_WATCHDOG|RESET_POR);assert(!memcmp(keep,previous,sizeof keep)&&r2_watchdog_reset_kind()==R2_UI_RESET_MULTIPLE);
 r2_watchdog_pending_saved(previous[33]);assert(!previous[0]);
 boot(302,RESET_CPU_LOCKUP);assert(previous[0]==R2_FAULT_RESET_MAGIC&&previous[2]==302&&previous[5]==RESET_CPU_LOCKUP);
 empty_records();boot(303,RESET_POR);assert(!r2_watchdog_pending_record());
 /* Recognized maintenance WDT continuation must not consume a fault slot. */
 advance(100);parent_channel=0;capsule(WD_NO_SYNC,0); /* stale nonterminal context is not maintenance fault evidence */
 if(!setjmp(maintenance_exit)){(void)r2_watchdog_bootloader(1000);assert(false);}
 boot(304,RESET_WATCHDOG);assert(!r2_watchdog_pending_record());
 /* Existing actual policy/CONTROL/mailbox bodies retain their behavior. */
 clear_runtime();assert(!r2_watchdog_boot(&id));policy_cases(false);policy_cases(true);
 cleanup_recovery_budget();owner_failure_cases();control_capsule_detail_cases();host_detail_cases();
 puts("PASS separate WD pending survives RAM resets/build changes, preserves first pending, exact durable ACK, current-vs-older reset attribution, NO_SYNC unknown, torn/factory negatives; policy unchanged");
 return 0;
}

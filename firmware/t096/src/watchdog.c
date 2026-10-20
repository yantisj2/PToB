/* SPDX-License-Identifier: Apache-2.0
 * Out-of-band observer: no service locks, controller calls, flash or USB in
 * the watchdog thread. Main alone requests recovery through existing owners.
 */
#include "watchdog.h"
#include "watchdog_policy.h"
#include "ble_service.h"
#include "build_identity.h"
#ifdef R2_LORA
#include "lora_watchdog.h"
#include "lora_service.h"
#endif
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/drivers/watchdog.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/reboot.h>
#include <cmsis_core.h>
#define WD_PARENT_MS 30000u
#define WD_POLL_MS 125u
#define WD_CORE_MS 30000u
#define WD_RC_WORKER_MS 30000u
_Static_assert(WD_PARENT_MS>=30000u&&WD_CORE_MS>=30000u&&WD_RC_WORKER_MS>=30000u,
               "Watchdogs observe failures, not precision timing gaps");
#define WD_MAGIC UINT32_C(0x52325744)
#define WD_WORDS 34u
enum {WD_NO_SYNC=1,WD_RETRIES_EXHAUSTED,WD_CORE_STALL,WD_RECOVERY_FAILED,WD_RC_WORKER_STALL,WD_LORA_FAULT,
 WD_FATAL=16,WD_UF2=256};
/* CRC/commit, exact source and silicon binding; RAM survival is conditional. */
volatile uint32_t r2_watchdog_capsule[WD_WORDS] __noinit;
static uint32_t previous[WD_WORDS];
static uint64_t physical_boot;
static unsigned reset_kind;
static uint32_t reset_flags;
static char silicon[16];
static const struct device *const parent=DEVICE_DT_GET(DT_NODELABEL(wdt0));
static int parent_channel=-1;
static atomic_t main_at,intent,reset_reason,restart_request,restart_error;
/* Main is the only mailbox writer. All fields are atomic 32-bit values.
 * Odd publication is unavailable; the reader never spins on the producer. */
static atomic_t sequence,report_at,report_rc,report_useful,report_pending,report_generation,report_allowed,report_worker_stalled;
static atomic_t retries,silent_since;
/* Main-owned saturating diagnostic counts; no new journal or console cadence. */
static atomic_t observations,useful_observations,future_observations,stale_observations,refusal_flags;
static void count_observation(atomic_t *counter)
{uint32_t n=(uint32_t)atomic_get(counter);if(n<UINT32_MAX)atomic_set(counter,(atomic_val_t)(n+1));}
static struct r2_wd_policy policy;
K_THREAD_STACK_DEFINE(watchdog_stack,1024);
static struct k_thread watchdog_thread;
static uint32_t crc(const volatile uint32_t *p)
{
 uint32_t c=UINT32_MAX;
 for(unsigned i=1;i<WD_WORDS-1;i++)for(unsigned b=0;b<4;b++){
  c^=(p[i]>>(b*8))&255u;
  for(unsigned bit=0;bit<8;bit++)c=(c>>1)^(UINT32_C(0xedb88320)&-(c&1u));
 }
 return ~c;
}
static void capsule(unsigned reason)
{
 r2_watchdog_capsule[0]=0;r2_watchdog_capsule[1]=1;
 r2_watchdog_capsule[2]=(uint32_t)physical_boot;r2_watchdog_capsule[3]=(uint32_t)(physical_boot>>32);
 r2_watchdog_capsule[4]=reason;r2_watchdog_capsule[5]=(uint32_t)atomic_get(&report_rc);
 r2_watchdog_capsule[6]=(uint32_t)atomic_get(&retries);r2_watchdog_capsule[7]=k_uptime_get_32();
 r2_watchdog_capsule[8]=(uint32_t)atomic_get(&silent_since);r2_watchdog_capsule[9]=(uint32_t)atomic_get(&main_at);
 r2_watchdog_capsule[10]=UINT32_MAX; /* A flash commit is never inferred here. */
 r2_watchdog_capsule[11]=(uint32_t)atomic_get(&report_generation);
 for(unsigned i=0;i<16;i++){
  uint32_t word;memcpy(&word,&R2_WATCHDOG_BINDING[4*i],4);r2_watchdog_capsule[12+i]=word;
 }
 for(unsigned i=0;i<4;i++){
  uint32_t word;memcpy(&word,silicon+4*i,4);r2_watchdog_capsule[28+i]=word;
 }
 r2_watchdog_capsule[32]=(uint32_t)atomic_get(&restart_error);
 r2_watchdog_capsule[33]=crc(r2_watchdog_capsule);
 __DMB();r2_watchdog_capsule[0]=WD_MAGIC;__DSB();
}
static bool previous_bound(const struct r2_identity *id)
{
 return r2_watchdog_capsule[0]==WD_MAGIC&&r2_watchdog_capsule[1]==1&&
  r2_watchdog_capsule[33]==crc(r2_watchdog_capsule)&&
  !memcmp((const void *)&r2_watchdog_capsule[12],R2_WATCHDOG_BINDING,64)&&
  !memcmp((const void *)&r2_watchdog_capsule[28],id->silicon,16);
}
bool r2_watchdog_restore_rc(bool *rc)
{
 if(previous[0]!=WD_MAGIC||previous[4]==WD_UF2||previous[5]>1)return false;
 *rc=previous[5]!=0;return true;
}
void r2_watchdog_main_beat(void){atomic_set(&main_at,(atomic_val_t)k_uptime_get_32());}
void r2_watchdog_enable(bool enabled)
{atomic_set(&intent,enabled);if(!enabled)atomic_set(&restart_request,0);}
void r2_watchdog_observe(const struct r2_service_status *s,bool allowed)
{
 /* Service snapshots use native-tick microseconds. Millisecond-floor uptime
  * can precede a snapshot from the current millisecond and falsely break the
  * sustained-health interval. Keep strict future/staleness/expiry comparisons
  * in the producer's clock domain, without adding permission or slack. */
 uint64_t now=k_ticks_to_us_floor64(k_uptime_ticks());
 bool live=s->active&&!s->closing&&!s->profile_pending&&!s->error&&now>=s->at&&now-s->at<1000000;
 /* GM beat RMS validity comes from fresh associated actual TX captures; it
  * is used only as activity, never as output/UTC precision qualification. */
 bool gm=!s->profile_rc&&s->serving&&s->source_eligible&&s->precision.authorized&&s->precision.rms_valid;
 bool follower=s->follower_clock_valid&&now<s->service_until&&s->precision.map_valid&&s->precision.authorized&&
  s->precision.correction_age_valid&&s->precision.correction_age_ms<2000;
 if(s->profile_rc)follower=follower&&s->precision.locked&&!s->relay.held_time;
 bool coordination=false;
#ifdef R2_LORA
 struct r2_gm_coordination c;r2_lora_coordination(&c);
 uint64_t self=s->self<4?s->identities[s->self]:0;
 coordination=!s->profile_rc&&!s->serving&&self&&r2_gm_coordination_live(&c,self,now)&&
  (s->root>=4||s->identities[s->root]==c.root);
#endif
 uint32_t refused=(!s->active||s->closing||s->profile_pending||s->error)?1u:0u;
 bool future=now<s->at,stale=!future&&now-s->at>=1000000;
 if(future)refused|=2u;
 if(stale)refused|=4u;
 if(!gm){
  if(!s->follower_clock_valid)refused|=8u;
  if(now>=s->service_until)refused|=16u;
  if(!s->precision.map_valid)refused|=32u;
  if(!s->precision.authorized)refused|=64u;
  if(!s->precision.correction_age_valid||s->precision.correction_age_ms>=2000)refused|=128u;
  if(s->profile_rc&&!s->precision.locked)refused|=256u;
  if(s->profile_rc&&s->relay.held_time)refused|=512u;
 }
 count_observation(&observations);
 if(live&&(gm||follower))count_observation(&useful_observations);
 if(future)count_observation(&future_observations);
 if(stale)count_observation(&stale_observations);
 atomic_set(&refusal_flags,(atomic_val_t)refused);
 /* A live main/USB/UI must not feed a frozen RC owner indefinitely. This
  * observation margin does not change any controller's original deadline.
  * Recovery of a stuck controller uses a hardware reset, never a second owner. */
 bool stalled=s->profile_rc&&s->active&&!s->closing&&!s->profile_pending&&
  now>=s->at&&now-s->at>=(uint64_t)WD_RC_WORKER_MS*1000;
 atomic_inc(&sequence);
 atomic_set(&report_at,(atomic_val_t)k_uptime_get_32());atomic_set(&report_rc,s->profile_rc);
 /* Bit1 is a legitimate coordinated operating mode, NOT qualified time.
  * Keep precision refusal flags and qualified-observation counters separate.
  * It refreshes only ordinary NO_SYNC policy; core/owner/LoRa fault observers
  * retain their original paths and deadlines. Reuse the existing atomic. */
 atomic_set(&report_useful,live?((gm||follower)?1:coordination?2:0):0);atomic_set(&report_pending,s->profile_pending);
 atomic_set(&report_generation,(atomic_val_t)s->profile_changes);atomic_set(&report_allowed,allowed);
 atomic_set(&report_worker_stalled,stalled);
 atomic_inc(&sequence);
}
unsigned r2_watchdog_take_restart(void)
{unsigned request=(unsigned)atomic_set(&restart_request,0);return atomic_get(&intent)&&atomic_get(&report_allowed)?request:0;}
void r2_watchdog_restart_result(int result)
{if(result)atomic_set(&restart_error,result);}
void r2_watchdog_fatal(unsigned reason)
{
 /* A crash must never continue feeding, even if fault capture itself fails. */
 atomic_set(&reset_reason,WD_FATAL+reason);capsule(WD_FATAL+reason);
}
void r2_watchdog_lora(int error,uint32_t stage,uint32_t generation,uint32_t closure,int journal_result)
{
 /* Same source/silicon/boot binding and physical parent reset as BLE. Do
  * not overwrite an earlier fatal/reset decision or issue a software reset. */
 if(atomic_get(&reset_reason))return;
 atomic_set(&reset_reason,WD_LORA_FAULT);capsule(WD_LORA_FAULT);
 r2_watchdog_capsule[0]=0;
 r2_watchdog_capsule[8]=(uint32_t)error;r2_watchdog_capsule[9]=stage;
 r2_watchdog_capsule[10]=closure;r2_watchdog_capsule[11]=generation;
 r2_watchdog_capsule[32]=(uint32_t)journal_result;
 r2_watchdog_capsule[33]=crc(r2_watchdog_capsule);
 __DMB();r2_watchdog_capsule[0]=WD_MAGIC;__DSB();
}
static bool watchdog_step(void)
{
 if(atomic_get(&reset_reason))return false;
#ifdef R2_LORA
 r2_lora_watchdog_check();
#endif
 /* Main has higher priority and may publish after this thread wakes.
  * Read each timestamp before its comparison time; a preempting producer
  * must never look2^32ms stale through unsigned age subtraction. */
 uint32_t beat=(uint32_t)atomic_get(&main_at),now=k_uptime_get_32();
 if((uint32_t)(now-beat)>=WD_CORE_MS){
  atomic_set(&reset_reason,WD_CORE_STALL);capsule(WD_CORE_STALL);return false;
 }
 if(atomic_get(&restart_error)){
  atomic_set(&reset_reason,WD_RECOVERY_FAILED);capsule(WD_RECOVERY_FAILED);return false;
 }
 uint32_t seq=(uint32_t)atomic_get(&sequence);
 if(seq&1u)return true;
 bool enabled=atomic_get(&intent)&&atomic_get(&report_allowed);
 bool rc=atomic_get(&report_rc),useful=atomic_get(&report_useful),pending=atomic_get(&report_pending);
 uint32_t generation=(uint32_t)atomic_get(&report_generation),at=(uint32_t)atomic_get(&report_at);
 bool stalled=atomic_get(&report_worker_stalled);
 if(seq!=(uint32_t)atomic_get(&sequence))return true;
 now=k_uptime_get_32(); /* Causal with the accepted mailbox copy. */
 if(enabled&&stalled&&(uint32_t)(now-at)<1000u){
  atomic_set(&reset_reason,WD_RC_WORKER_STALL);capsule(WD_RC_WORKER_STALL);return false;
 }
 useful=useful&&(uint32_t)(now-at)<1000u;
 enum r2_wd_action action=r2_wd_policy_step(&policy,now,enabled,rc,useful,pending,generation);
 atomic_set(&retries,(atomic_val_t)policy.retries);atomic_set(&silent_since,(atomic_val_t)policy.since);
 if(action==R2_WD_RESTART){capsule(WD_NO_SYNC);atomic_set(&restart_request,(atomic_val_t)policy.retries);}
 if(action==R2_WD_RESET){unsigned reason=policy.recovering?WD_RECOVERY_FAILED:WD_RETRIES_EXHAUSTED;atomic_set(&reset_reason,reason);capsule(reason);return false;}
 return true;
}
static void watchdog_owner(void *a,void *b,void *c)
{
 for(;;){
  if(watchdog_step()){
   if(wdt_feed(parent,parent_channel)){atomic_set(&reset_reason,WD_RECOVERY_FAILED);capsule(WD_RECOVERY_FAILED);}
  }
  k_sleep(K_MSEC(WD_POLL_MS));
 }
}
int r2_watchdog_boot(const struct r2_identity *id)
{
 if(!id||id->result)return -EACCES;
 physical_boot=id->boot;memcpy(silicon,id->silicon,16);
 if((id->reset_cause&RESET_WATCHDOG)&&previous_bound(id))
  for(unsigned i=0;i<WD_WORDS;i++)previous[i]=r2_watchdog_capsule[i];
 uint32_t flags=id->reset_cause;reset_flags=flags;
 reset_kind=!flags?R2_UI_RESET_UNKNOWN:(flags&(flags-1))?R2_UI_RESET_MULTIPLE:
  flags==RESET_PIN?R2_UI_RESET_PIN:flags==RESET_SOFTWARE?R2_UI_RESET_SOFTWARE:
  flags==RESET_POR?R2_UI_RESET_POWER:flags==RESET_BROWNOUT?R2_UI_RESET_BROWNOUT:
  flags==RESET_WATCHDOG?R2_UI_RESET_WATCHDOG:flags==RESET_CPU_LOCKUP?R2_UI_RESET_LOCKUP:
  flags==RESET_LOW_POWER_WAKE?R2_UI_RESET_WAKE:flags==RESET_DEBUG?R2_UI_RESET_DEBUG:R2_UI_RESET_UNKNOWN;
 if(flags==RESET_WATCHDOG&&previous[0]==WD_MAGIC){
  unsigned reason=previous[4];
  if(reason==WD_RETRIES_EXHAUSTED)reset_kind=R2_UI_RESET_WD_PROFILE;
  else if(reason==WD_CORE_STALL)reset_kind=R2_UI_RESET_WD_CORE;
  else if(reason==WD_RECOVERY_FAILED)reset_kind=R2_UI_RESET_WD_RECOVERY;
  else if(reason==WD_RC_WORKER_STALL||reason==WD_LORA_FAULT)reset_kind=R2_UI_RESET_WD_FAULT;
  else if(reason>=WD_FATAL&&reason<WD_UF2)reset_kind=R2_UI_RESET_WD_FAULT;
 }
 r2_watchdog_capsule[0]=0;
 int cleared=hwinfo_clear_reset_cause();if(cleared)return cleared;
 /* Watchdog reset clears WDT, unlike a soft reset. A bound one-shot UF2
  * continuation runs BEFORE arming WDT and uses the existing public request.
  * Lost/torn RAM falls back to normal boot; the host still requires real MSC. */
 if(previous[0]==WD_MAGIC&&previous[4]==WD_UF2){
  previous[0]=0;(void)r2_bootloader_request();
  /* A successful public request never returns. If retention/entry fails,
   * resume ordinary startup with a working parent instead of a dark halt. */
  reset_kind=R2_UI_RESET_WD_RECOVERY;
 }
 if(!device_is_ready(parent))return -ENODEV;
 const struct wdt_timeout_cfg timeout={.window={.min=0,.max=WD_PARENT_MS},.flags=WDT_FLAG_RESET_SOC};
 parent_channel=wdt_install_timeout(parent,&timeout);if(parent_channel<0)return parent_channel;
 int result=wdt_setup(parent,0);if(result)return result;
 r2_watchdog_main_beat();
 k_thread_create(&watchdog_thread,watchdog_stack,K_THREAD_STACK_SIZEOF(watchdog_stack),watchdog_owner,NULL,NULL,NULL,4,0,K_NO_WAIT);
 k_thread_name_set(&watchdog_thread,"r2_watchdog");return 0;
}
unsigned r2_watchdog_reset_kind(void){return reset_kind;}
int r2_watchdog_bootloader(uint64_t original_deadline_ms)
{
 if(parent_channel<0)return 0;
 uint64_t now=k_uptime_get();
 /* Commit must still fit the original READY deadline after all owners have
  * drained. Once committed, the fixed30s hardware watchdog performs the reset;
  * it is not shortened for maintenance and no timing authority is retained.
  * Host tools must allow that reset latency after a successful commit. */
 if(now>=original_deadline_ms||original_deadline_ms-now<250u)return -ETIMEDOUT;
 capsule(WD_UF2);atomic_set(&reset_reason,WD_UF2);
 /* No flash or USB here. The independent peripheral performs a true reset. */
 for(;;)k_sleep(K_MSEC(WD_POLL_MS));
}
void r2_watchdog_console(char *line,size_t capacity)
{
 static bool reported;
 static uint32_t signature;
 static uint64_t report_due;
 uint64_t now=(uint64_t)k_uptime_get();
 uint32_t current=(unsigned)atomic_get(&intent)|((unsigned)atomic_get(&report_rc)<<1)|
  ((unsigned)(atomic_get(&report_useful)&2)<<1)|((unsigned)atomic_get(&retries)<<8)|((unsigned)atomic_get(&reset_reason)<<16);
 if(reported&&current==signature&&now<report_due)return;
 int n=snprintf(line,capacity,"R2_WATCHDOG {\"parent_ms\":%u,\"gm_ms\":%u,\"rc_ms\":%u,\"enabled\":%u,\"profile_rc\":%u,\"retries\":%u,\"silent_ms\":%u,\"reset_reason\":%u,\"reset_flags\":%u,\"last_reset\":%u,\"refusal_flags\":%u,\"coordination_activity\":%s,\"observations\":[%u,%u,%u,%u],\"previous\":[",
  WD_PARENT_MS,R2_WD_GM_MS,R2_WD_RC_MS,(unsigned)atomic_get(&intent),(unsigned)atomic_get(&report_rc),
  (unsigned)atomic_get(&retries),(uint32_t)(k_uptime_get_32()-(uint32_t)atomic_get(&silent_since)),(unsigned)atomic_get(&reset_reason),reset_flags,reset_kind,(unsigned)atomic_get(&refusal_flags),(atomic_get(&report_useful)&2)?"true":"false",
  (unsigned)atomic_get(&observations),(unsigned)atomic_get(&useful_observations),
  (unsigned)atomic_get(&future_observations),(unsigned)atomic_get(&stale_observations));
 if(n<0||(size_t)n>=capacity)return;
 for(unsigned i=0;previous[0]==WD_MAGIC&&i<WD_WORDS;i++){
  int added=snprintf(line+n,capacity-(size_t)n,"%s%u",i?",":"",previous[i]);
  if(added<0||(size_t)added>=capacity-(size_t)n)return;
  n+=added;
 }
 if((size_t)n+4>=capacity)return;
 memcpy(line+n,"]}\n",3);n+=3;
 if(!r2_console_note(line,(size_t)n,now+250)){
  signature=current;reported=true;report_due=now+60000;
 }
}

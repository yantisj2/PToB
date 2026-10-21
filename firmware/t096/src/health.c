#include "controller_diag.h"
/* SPDX-License-Identifier: Apache-2.0 */
#include "health.h"
#include "watchdog.h"
#if defined(CONFIG_ARM)
#include "app.h"
/* Dedicated writer watermark, independent of main and its service snapshot. */
extern uint32_t r2_diag_stack_unused(void);
#include "build_identity.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/fatal.h>
#include <zephyr/irq.h>
#include <cmsis_core.h>
#include <zephyr/timing/timing.h>
/* No flash, reboot, allocation, USB or application locks in the fatal hook.
 * Debugger can inspect this symbol while halted. NOINIT survival through a
 * bootloader/reset is conditional; CRC rejection means unavailable, not healthy.
 * Words: magic, format, boot lo/hi, reason, pc, lr, xpsr, CFSR, HFSR,
 * MMFAR, BFAR, MSP, PSP, IPSR, current thread, main beat, main ms,
 * flash op/result/ms, PA op/result/ms, source SHA ASCII (16 words),
 * nine latency words, PSP-frame flag/eight words, active site/cycles, masks, CRC. */
#define FAULT_WORDS 64
#define FAULT_MAGIC 0x52324631u
volatile uint32_t r2_fault_capsule[FAULT_WORDS] __noinit;
static uint32_t previous[FAULT_WORDS];
/* hz, empty-probe min/max cycles, maximum site/cycles, last site/cycles,
 * outermost samples, samples >=100us. Never format or allocate while masked. */
static uint32_t latency[9],active_site,active_cycle;
static bool timing_ready;
void r2_health_lock_enter(unsigned site,unsigned saved_mask)
{
 if(timing_ready&&!saved_mask){active_cycle=(uint32_t)timing_counter_get();active_site=site;}
}
void r2_health_lock_leave(unsigned saved_mask)
{
 if(!timing_ready||saved_mask||!active_site)return;
 uint32_t elapsed=(uint32_t)timing_counter_get()-active_cycle;
 latency[5]=active_site;latency[6]=elapsed;
 if(elapsed>latency[4]){latency[3]=active_site;latency[4]=elapsed;}
 if(latency[7]!=UINT32_MAX)++latency[7];
 if(elapsed>=latency[0]/10000&&latency[8]!=UINT32_MAX)++latency[8];
 active_site=0;
}
static void timing_setup(void)
{
 /* Public Zephyr Cortex-M DWT timing backend; no RADIO/TIMER/PPI ownership.
  * Called once before Bluetooth/USB initialization, never from an ISR. */
 timing_init();timing_start();latency[0]=(uint32_t)timing_freq_get();
 latency[1]=UINT32_MAX;timing_ready=latency[0]!=0;
 for(unsigned i=0;i<32;i++){
  unsigned k=irq_lock();r2_health_lock_enter(0xffff0001u,k);
  r2_health_lock_leave(k);uint32_t n=latency[6];irq_unlock(k);
  if(n<latency[1])latency[1]=n;
  if(n>latency[2])latency[2]=n;
 }
 for(unsigned i=3;i<9;i++)latency[i]=0;
}
static uint64_t boot_id;
static uint32_t beats,beat_ms;
struct operation { uint32_t op; int32_t result; uint32_t ms; };
static struct operation operations[R2_HEALTH_LANES];
static uint64_t report_due;
static uint64_t detail_due;
static uint32_t reported_drops,reported_expired;
static int reported_flash_error,reported_pa_error;
#if defined(R2_WATCHDOG)
static bool watchdog_report_due;
#endif
static char line[1536]; /* console worker is the sole formatter */
static uint32_t crc(const volatile uint32_t *words)
{
 uint32_t value=UINT32_MAX;
 for(unsigned i=1;i<FAULT_WORDS-1;i++)for(unsigned b=0;b<4;b++){
  value^=(words[i]>>(8*b))&255;
  for(unsigned bit=0;bit<8;bit++)value=(value>>1)^(0xedb88320u&-(value&1u));
 }
 return ~value;
}
void r2_health_init(uint64_t boot)
{
 if(r2_fault_capsule[0]==FAULT_MAGIC&&(r2_fault_capsule[1]==2||r2_fault_capsule[1]==3)&&
    r2_fault_capsule[FAULT_WORDS-1]==crc(r2_fault_capsule))
  for(unsigned i=0;i<FAULT_WORDS;i++)previous[i]=r2_fault_capsule[i];
 r2_fault_capsule[0]=0;boot_id=boot;timing_setup();detail_due=0;
}
void r2_health_beat(void)
{
 unsigned key=irq_lock();++beats;beat_ms=k_uptime_get_32();irq_unlock(key);
}
void r2_health_operation(unsigned lane,unsigned operation,int result)
{
 if(lane>=R2_HEALTH_LANES)return;
 unsigned key=irq_lock();operations[lane]=(struct operation){operation,result,k_uptime_get_32()};irq_unlock(key);
}
void r2_health_console(void)
{
 uint64_t now=k_uptime_get();if(now<report_due)return;report_due=now+1000;
 struct r2_io io;r2_console_snapshot(&io);if(!io.dtr)return;
#if defined(R2_WATCHDOG)
 /* Alternate health/watchdog opportunities. Both are now idle-only notes;
  * neither can queue a second diagnostic behind a command response. */
 if(watchdog_report_due){watchdog_report_due=false;r2_watchdog_console(line,sizeof line);return;}
 watchdog_report_due=true;
#endif
 unsigned key=irq_lock();uint32_t beat=beats,at=beat_ms;
 struct operation flash=operations[0],pa=operations[1];uint32_t masked[9];
 for(unsigned i=0;i<9;i++)masked[i]=latency[i];
 irq_unlock(key);
 /* Keep fault/counter changes promptly, without printing a full healthy
  * capsule every other second. The fixed state/event journal remains live. */
 int flash_error=flash.result<0&&flash.result!=-EINPROGRESS?flash.result:0;
 int pa_error=pa.result<0&&pa.result!=-EINPROGRESS?pa.result:0;
 bool changed=io.tx_drops!=reported_drops||io.tx_expired!=reported_expired||
  flash_error!=reported_flash_error||pa_error!=reported_pa_error;
 if(!changed&&now<detail_due)return;
 detail_due=now+60000;reported_drops=io.tx_drops;reported_expired=io.tx_expired;
 reported_flash_error=flash_error;reported_pa_error=pa_error;
 size_t unused=0;int stack_rc=k_thread_stack_space_get(k_current_get(),&unused);
 int n=snprintf(line,sizeof line,
  "R2_HEALTH {\"source\":\"%s\",\"boot\":\"%016llx\",\"ms\":%llu,\"main\":[%u,%u],\"flash\":[%u,%d,%u],\"pa\":[%u,%d,%u],\"console_free\":%u,\"diag_free\":%u,\"tx_drops\":%u,\"tx_expired\":%u,\"irq_mask\":[%u,%u,%u,%u,%u,%u,%u,%u,%u],\"previous_fault\":[",
  R2_SOURCE_SHA,(unsigned long long)boot_id,(unsigned long long)now,beat,at,
  flash.op,flash.result,flash.ms,pa.op,pa.result,pa.ms,
  (unsigned)(stack_rc?UINT32_MAX:unused),r2_diag_stack_unused(),io.tx_drops,io.tx_expired,
  masked[0],masked[1],masked[2],masked[3],masked[4],masked[5],masked[6],masked[7],masked[8]);
 if(n<0||(size_t)n>=sizeof line)return;
 for(unsigned i=0;previous[0]==FAULT_MAGIC&&i<FAULT_WORDS;i++){
  int added=snprintf(line+n,sizeof line-(size_t)n,"%s%u",i?",":"",previous[i]);
  if(added<0||(size_t)added>=sizeof line-(size_t)n)return;
  n+=added;
 }
 if((size_t)n+4>=sizeof line)return;
 memcpy(line+n,"]}\n",3);n+=3;
 (void)r2_console_note(line,(size_t)n,now+250);
}
static inline bool stack_window_valid(uintptr_t start,size_t size,uintptr_t sp)
{
 return !(sp&3)&&start>=0x20000000u&&start<=0x20040000u&&size>=32&&
        size<=0x20040000u-start&&sp>=start&&sp-start<=size-32;
}
void k_sys_fatal_error_handler(unsigned int reason,const struct arch_esf *esf)
{
 unsigned old_mask=arch_irq_lock();r2_fault_capsule[0]=0;
 r2_watchdog_fatal(reason);
 for(unsigned i=1;i<FAULT_WORDS;i++)r2_fault_capsule[i]=0;
 r2_fault_capsule[1]=2;r2_fault_capsule[2]=(uint32_t)boot_id;r2_fault_capsule[3]=boot_id>>32;
 r2_fault_capsule[4]=reason;
 if(esf){r2_fault_capsule[5]=esf->basic.pc;r2_fault_capsule[6]=esf->basic.lr;r2_fault_capsule[7]=esf->basic.xpsr;}
 r2_fault_capsule[8]=SCB->CFSR;r2_fault_capsule[9]=SCB->HFSR;
 r2_fault_capsule[10]=SCB->MMFAR;r2_fault_capsule[11]=SCB->BFAR;
 r2_fault_capsule[12]=__get_MSP();r2_fault_capsule[13]=__get_PSP();r2_fault_capsule[14]=__get_IPSR();
 r2_fault_capsule[15]=(uint32_t)(uintptr_t)k_current_get();r2_fault_capsule[16]=beats;r2_fault_capsule[17]=beat_ms;
 for(unsigned i=0;i<R2_HEALTH_LANES;i++){
  r2_fault_capsule[18+3*i]=operations[i].op;r2_fault_capsule[19+3*i]=(uint32_t)operations[i].result;r2_fault_capsule[20+3*i]=operations[i].ms;
 }
 for(unsigned i=0;i<16;i++){
  const char *s=&R2_SOURCE_SHA[4*i];
  r2_fault_capsule[24+i]=(uint32_t)(uint8_t)s[0]|((uint32_t)(uint8_t)s[1]<<8)|((uint32_t)(uint8_t)s[2]<<16)|((uint32_t)(uint8_t)s[3]<<24);
 }
#ifdef R2_CONTROLLER_DIAGNOSTICS
 /* Explicit canary schema3 trades latency/interrupted-stack details for the
  * frozen prepare queue. Original source/boot/PC/LR/reason and CRC remain. */
 (void)old_mask;
 uint32_t controller[R2_CONTROLLER_DIAG_WORDS];r2_controller_diag_snapshot(controller);
 for(unsigned i=0;i<R2_CONTROLLER_DIAG_WORDS;i++)r2_fault_capsule[40+i]=controller[i];
 r2_fault_capsule[1]=3;
#else
 for(unsigned i=0;i<9;i++)r2_fault_capsule[40+i]=latency[i];
 /* A nested ISR fault reports the ISR frame in esf; retain the interrupted
  * thread's basic frame separately. Validate RAM, alignment and actual stack
  * bounds before dereferencing. No stack walk or arbitrary-address reads. */
 uintptr_t t=(uintptr_t)k_current_get(),sp=__get_PSP();
 if(esf&&(esf->basic.xpsr&511)&&t>=0x20000000u&&t<=0x20040000u-sizeof(struct k_thread)){
  const struct k_thread *thread=(const struct k_thread *)t;
  uintptr_t start=thread->stack_info.start;size_t size=thread->stack_info.size;
  if(stack_window_valid(start,size,sp)){
   const volatile uint32_t *frame=(const volatile uint32_t *)sp;
   for(unsigned i=0;i<8;i++)r2_fault_capsule[50+i]=frame[i];
   r2_fault_capsule[49]=1;
  }
 }
 r2_fault_capsule[58]=active_site;
 if(active_site&&timing_ready)r2_fault_capsule[59]=(uint32_t)timing_counter_get()-active_cycle;
 r2_fault_capsule[60]=old_mask;r2_fault_capsule[61]=__get_PRIMASK();r2_fault_capsule[62]=__get_CONTROL();
 #endif
 r2_fault_capsule[FAULT_WORDS-1]=crc(r2_fault_capsule);__DMB();r2_fault_capsule[0]=FAULT_MAGIC;__DSB();
 k_fatal_halt(reason);CODE_UNREACHABLE;
}
#endif

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
#include <zephyr/kernel.h>
#include <cmsis_core.h>
#include <zephyr/timing/timing.h>
/* No flash, reboot, allocation, USB or application locks in the fatal hook.
 * Debugger can inspect this symbol while halted. NOINIT survival through a
 * bootloader/reset is conditional; CRC rejection means unavailable, not healthy.
 * Words: magic, format, boot lo/hi, reason, pc, lr, xpsr, CFSR, HFSR,
 * MMFAR, BFAR, MSP, PSP, IPSR, current thread, main beat, main ms,
 * flash op/result/ms, PA op/result/ms, source SHA ASCII (16 words),
 * nine latency words, PSP-frame flag/eight words, active site/cycles, masks, CRC.
 * Schema3 replaces40..62 with controller context. Schema4 replaces the same
 * tail with supplied integer registers for hardware memory/bus/usage faults;
 * it never recovers pre-kernel-clear SCB validity or grants role authority. */
#define FAULT_WORDS 64
#define FAULT_MAGIC 0x52324631u
volatile uint32_t r2_fault_capsule[FAULT_WORDS] __noinit;
static uint32_t previous[FAULT_WORDS];
_Static_assert(sizeof r2_fault_capsule==256&&sizeof previous==256,"Fixed fault storage");
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
#if defined(R2_GM7_NATIVE) && defined(CONFIG_EXTRA_EXCEPTION_INFO)
K_KERNEL_STACK_ARRAY_DECLARE(z_interrupt_stacks, CONFIG_MP_MAX_NUM_CPUS,
                             CONFIG_ISR_STACK_SIZE);
/* These bits describe supplied register provenance, never pre-clear SCB state.
 * The pinned Zephyr handler clears CFSR before reaching this application hook. */
enum { FAULT_BASIC = 1, FAULT_CALLEE = 2, FAULT_RETURN = 4,
       FAULT_STACK_UNRELIABLE = 8, FAULT_SCB_POST_CLEAR = 16 };
static bool cpu_fault(unsigned reason,unsigned vector)
{
 return (vector==4&&reason>=K_ERR_ARM_MEM_GENERIC&&reason<=K_ERR_ARM_MEM_FP_LAZY_STATE_PRESERVATION)||
        (vector==5&&reason>=K_ERR_ARM_BUS_GENERIC&&reason<=K_ERR_ARM_BUS_FP_LAZY_STATE_PRESERVATION)||
        (vector==6&&reason>=K_ERR_ARM_USAGE_GENERIC&&reason<=K_ERR_ARM_USAGE_UNDEFINED_INSTRUCTION);
}
static bool fault_return(uint32_t value)
{
 /* Cortex-M4, non-FP basic frame: handler MSP, thread MSP or thread PSP. */
 return value==UINT32_C(0xfffffff1)||value==UINT32_C(0xfffffff9)||value==UINT32_C(0xfffffffd);
}
static bool callee_window(uintptr_t p,uintptr_t entry,uintptr_t start,size_t size)
{
 /* Pinned fault_s.S saves8 bytes then40 bytes. The callee record occupies
  * r4..r11 +PSP (36 bytes); its final word is assembly padding. No stack walk. */
 return !(p&3)&&size>=48&&start<=UINTPTR_MAX-size&&p>=start&&
        p-start<=size-48&&entry>=48&&p==entry-48;
}
static void fault_registers(unsigned reason,const struct arch_esf *esf,unsigned saved_mask)
{
 uint32_t flags=FAULT_SCB_POST_CLEAR;
 if(esf){
  flags|=FAULT_BASIC;
  r2_fault_capsule[41]=esf->basic.r0;r2_fault_capsule[42]=esf->basic.r1;
  r2_fault_capsule[43]=esf->basic.r2;r2_fault_capsule[44]=esf->basic.r3;
  r2_fault_capsule[45]=esf->basic.r12;
  uint32_t ret=esf->extra_info.exc_return;
  uintptr_t entry=esf->extra_info.msp,p=(uintptr_t)esf->extra_info.callee;
  if(fault_return(ret)){
   flags|=FAULT_RETURN;r2_fault_capsule[55]=(uint32_t)entry;r2_fault_capsule[56]=ret;
   if(callee_window(p,entry,(uintptr_t)K_KERNEL_STACK_BUFFER(z_interrupt_stacks[0]),
                    K_KERNEL_STACK_SIZEOF(z_interrupt_stacks[0]))){
    const _callee_saved_t *c=esf->extra_info.callee;
    r2_fault_capsule[46]=c->v1;r2_fault_capsule[47]=c->v2;
    r2_fault_capsule[48]=c->v3;r2_fault_capsule[49]=c->v4;
    r2_fault_capsule[50]=c->v5;r2_fault_capsule[51]=c->v6;
    r2_fault_capsule[52]=c->v7;r2_fault_capsule[53]=c->v8;
    r2_fault_capsule[54]=c->psp;flags|=FAULT_CALLEE;
   }
  }
 }
 if(reason==K_ERR_ARM_MEM_STACKING||reason==K_ERR_ARM_MEM_UNSTACKING||
    reason==K_ERR_ARM_BUS_STACKING||reason==K_ERR_ARM_BUS_UNSTACKING||
    reason==K_ERR_ARM_USAGE_STACK_OVERFLOW)flags|=FAULT_STACK_UNRELIABLE;
 r2_fault_capsule[40]=flags;r2_fault_capsule[57]=saved_mask;
 r2_fault_capsule[58]=__get_PRIMASK();r2_fault_capsule[59]=__get_CONTROL();
 r2_fault_capsule[60]=active_site;
 if(active_site&&timing_ready)r2_fault_capsule[61]=(uint32_t)timing_counter_get()-active_cycle;
 r2_fault_capsule[1]=4;
}
#endif
#if defined(R2_GM7_NATIVE) && defined(CONFIG_EXTRA_EXCEPTION_INFO)
#if defined(CONFIG_FPU) && CONFIG_FPU
#error "Bounded crash capture requires basic integer exception frames (FPU off)"
#endif
#include "crash_record.h"
#include "ptob_device.h"
/* ONE512B retained record; no boot-time second stack dump. Existing256B
 * capsule/previous copy below are unchanged. Normal export never clears it. */
volatile uint32_t r2_crash_record[R2_CRASH_WORDS] __noinit;
_Static_assert(sizeof r2_crash_record==512,"One journal slot only");
static bool crash_handed_off;
K_THREAD_STACK_DECLARE(z_main_stack,CONFIG_MAIN_STACK_SIZE);
K_THREAD_STACK_DECLARE(normal_owner_stack,8192);
K_THREAD_STACK_DECLARE(owner_stack,8192);
K_THREAD_STACK_DECLARE(pt_stack,8192);
K_THREAD_STACK_DECLARE(supervisor_stack,2048);
K_THREAD_STACK_DECLARE(console_stack,2048);
K_THREAD_STACK_DECLARE(platform_stack,2048);
K_THREAD_STACK_DECLARE(gnss_stack,2048);
K_THREAD_STACK_DECLARE(ui_stack,4096);
K_THREAD_STACK_DECLARE(diagnostic_stack,2048);
#if defined(R2_WATCHDOG)
K_THREAD_STACK_DECLARE(watchdog_stack,1024);
#endif
#if defined(R2_LORA)
K_THREAD_STACK_DECLARE(worker_stack,2048);
#endif
struct crash_bound {uintptr_t start;size_t size;uint32_t id;};
#define CRASH_BOUND(s,i) {(uintptr_t)(s)+K_THREAD_STACK_RESERVED,K_THREAD_STACK_SIZEOF(s),i}
/* Literal linker-owned allocations, excluding reserved guard bytes. A pointer
 * into another known stack remains observed-SP evidence, not proof of ownership.
 * Unlisted kernel/Bluetooth stacks are UNKNOWN; never dereference a TCB to guess. */
static const struct crash_bound crash_bounds[]={
 CRASH_BOUND(z_main_stack,1),CRASH_BOUND(normal_owner_stack,2),
 CRASH_BOUND(owner_stack,3),CRASH_BOUND(pt_stack,4),
 CRASH_BOUND(supervisor_stack,5),CRASH_BOUND(console_stack,6),
 CRASH_BOUND(platform_stack,7),CRASH_BOUND(gnss_stack,8),
 CRASH_BOUND(ui_stack,9),CRASH_BOUND(diagnostic_stack,10),
#if defined(R2_WATCHDOG)
 CRASH_BOUND(watchdog_stack,11),
#endif
#if defined(R2_LORA)
 CRASH_BOUND(worker_stack,12),
#endif
};
#undef CRASH_BOUND
static void crash_digest(unsigned word,const char *hex)
{
 for(unsigned i=0;i<8;i++){
  uint32_t v=0;for(unsigned b=0;b<4;b++)
   v|=((r2_crash_nibble(hex[i*8+b*2])<<4)|r2_crash_nibble(hex[i*8+b*2+1]))<<(8*b);
  r2_crash_record[word+i]=v;
 }
}
static void crash_words(unsigned word,uintptr_t start,size_t bytes)
{
 const volatile uint32_t *src=(const volatile uint32_t *)start;
 for(unsigned i=0;i<bytes/4;i++)r2_crash_record[word+i]=src[i];
}
static void crash_capture(unsigned reason,const struct arch_esf *esf,unsigned mask)
{
 /* Fatal-only, IRQs already masked. No locks, driver, allocator, formatting,
  * flash, stack walk, variable-size locals or record-sized automatic arrays. */
 if(r2_health_crash_pending())return; /* preserve first unpersisted crash across another startup fault */
 crash_handed_off=false;r2_crash_record[127]=0;__DMB();
 for(unsigned i=0;i<127;i++)r2_crash_record[i]=0;
 r2_crash_record[0]=R2_CRASH_MAGIC;r2_crash_record[1]=R2_CRASH_SCHEMA;
 r2_crash_record[2]=UINT32_MAX;r2_crash_record[3]=512;
 r2_crash_record[4]=(uint32_t)boot_id;r2_crash_record[5]=boot_id>>32;
 r2_crash_record[6]=r2_crash_hex32(&PTOB_DEVICE_ID[8]);
 r2_crash_record[7]=r2_crash_hex32(PTOB_DEVICE_ID);
 crash_digest(8,R2_SOURCE_SHA);crash_digest(16,R2_WATCHDOG_BINDING);
 uint32_t flags=R2_CRASH_SCB_POST_CLEAR;r2_crash_record[24]=reason;
 r2_crash_record[45]=(uint32_t)(uintptr_t)k_current_get();
 r2_crash_record[46]=__get_IPSR();
 r2_crash_record[47]=SCB->CFSR;r2_crash_record[48]=SCB->HFSR;
 r2_crash_record[49]=SCB->MMFAR;r2_crash_record[50]=SCB->BFAR;
 r2_crash_record[59]=mask;r2_crash_record[60]=__get_CONTROL();
 if(reason==K_ERR_ARM_MEM_STACKING||reason==K_ERR_ARM_MEM_UNSTACKING||
    reason==K_ERR_ARM_BUS_STACKING||reason==K_ERR_ARM_BUS_UNSTACKING||
    reason==K_ERR_ARM_USAGE_STACK_OVERFLOW)flags|=R2_CRASH_STACK_UNRELIABLE;
 if(esf){
  flags|=R2_CRASH_BASIC;
  r2_crash_record[26]=esf->basic.r0;r2_crash_record[27]=esf->basic.r1;
  r2_crash_record[28]=esf->basic.r2;r2_crash_record[29]=esf->basic.r3;
  r2_crash_record[30]=esf->basic.r12;r2_crash_record[31]=esf->basic.lr;
  r2_crash_record[32]=esf->basic.pc;r2_crash_record[33]=esf->basic.xpsr;
  if((__get_IPSR()&511)==3||cpu_fault(reason,__get_IPSR()&511)){
  uintptr_t entry=esf->extra_info.msp,callee=(uintptr_t)esf->extra_info.callee;
  uint32_t ret=esf->extra_info.exc_return;
  if(fault_return(ret)){
   flags|=R2_CRASH_RETURN;r2_crash_record[43]=entry;r2_crash_record[44]=ret;
   if(callee_window(callee,entry,(uintptr_t)K_KERNEL_STACK_BUFFER(z_interrupt_stacks[0]),K_KERNEL_STACK_SIZEOF(z_interrupt_stacks[0]))){
    const _callee_saved_t *c=esf->extra_info.callee;
    r2_crash_record[34]=c->v1;r2_crash_record[35]=c->v2;
    r2_crash_record[36]=c->v3;r2_crash_record[37]=c->v4;
    r2_crash_record[38]=c->v5;r2_crash_record[39]=c->v6;
    r2_crash_record[40]=c->v7;r2_crash_record[41]=c->v8;
    r2_crash_record[42]=c->psp;flags|=R2_CRASH_CALLEE;
   }
   if(!(flags&R2_CRASH_STACK_UNRELIABLE)){
    bool known=false;
    if(flags&R2_CRASH_CALLEE)for(unsigned i=0;i<sizeof crash_bounds/sizeof crash_bounds[0];i++){
     const struct crash_bound *b=&crash_bounds[i];
     size_t n=r2_crash_window(b->start,b->size,r2_crash_record[42],R2_CRASH_THREAD_BYTES);
     if(!n)continue;
     known=true;r2_crash_record[51]=r2_crash_record[42];r2_crash_record[52]=n;r2_crash_record[55]=b->id;
     crash_words(R2_CRASH_THREAD_WORD,r2_crash_record[42],n);flags|=R2_CRASH_THREAD;
     if(b->size-(r2_crash_record[42]-b->start)>n)flags|=R2_CRASH_THREAD_TRUNCATED;
     break;
    }
    if(!known)flags|=R2_CRASH_UNKNOWN_THREAD;
    /* Original entryMSP points at the faulted handler's basic frame for
     * nested exceptions; for threadPSP faults this window may be empty. */
    uintptr_t start=(uintptr_t)K_KERNEL_STACK_BUFFER(z_interrupt_stacks[0]);
    size_t size=K_KERNEL_STACK_SIZEOF(z_interrupt_stacks[0]);
    size_t n=r2_crash_window(start,size,entry,R2_CRASH_ISR_BYTES);
    if(n){r2_crash_record[53]=entry;r2_crash_record[54]=n;crash_words(R2_CRASH_ISR_WORD,entry,n);flags|=R2_CRASH_ISR;if(size-(entry-start)>n)flags|=R2_CRASH_ISR_TRUNCATED;}
   }
  }
 }
 }
 r2_crash_record[25]=flags;r2_crash_record[126]=r2_crash_crc(r2_crash_record);
 __DMB();r2_crash_record[127]=R2_CRASH_COMMIT;__DSB();
}
const volatile uint32_t *r2_health_crash_pending(void)
{return !crash_handed_off&&r2_crash_valid(r2_crash_record,true)&&r2_crash_silicon(r2_crash_record,PTOB_DEVICE_ID)?r2_crash_record:NULL;}
void r2_health_crash_saved(uint32_t crc)
{if(r2_health_crash_pending()&&r2_crash_record[126]==crc)crash_handed_off=true;}
int r2_health_crash_read(unsigned chunk,uint8_t out[64])
{
 if(chunk>=R2_CRASH_DIAG_CHUNKS)return -EINVAL;
 if(!r2_crash_valid(r2_crash_record,true)||!r2_crash_silicon(r2_crash_record,PTOB_DEVICE_ID))return -ENOENT;
 for(unsigned i=0;i<16;i++){
  uint32_t v=r2_crash_record[chunk*16+i];
  for(unsigned b=0;b<4;b++)out[i*4+b]=(uint8_t)(v>>(8*b));
 }
 return 0;
}
#else
const volatile uint32_t *r2_health_crash_pending(void){return NULL;}
void r2_health_crash_saved(uint32_t crc){(void)crc;}
int r2_health_crash_read(unsigned chunk,uint8_t out[64]){(void)chunk;(void)out;return -ENOENT;}
#endif

void r2_health_init(uint64_t boot)
{
 if(r2_fault_capsule[0]==FAULT_MAGIC&&(r2_fault_capsule[1]==2||r2_fault_capsule[1]==3||r2_fault_capsule[1]==4)&&
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
 /* Keep the pending detail through best-effort admission refusal. The
  * existing one-second/alternating watchdog gates bound the next attempt. */
 if(!r2_console_note(line,(size_t)n,now+250)){
  detail_due=now+60000;reported_drops=io.tx_drops;reported_expired=io.tx_expired;
  reported_flash_error=flash_error;reported_pa_error=pa_error;
 }
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
#if defined(R2_GM7_NATIVE) && defined(CONFIG_EXTRA_EXCEPTION_INFO)
 if(cpu_fault(reason,__get_IPSR()&511))fault_registers(reason,esf,old_mask);
 else
#endif
 {
#if defined(R2_CONTROLLER_FAULT_CAPTURE) || defined(R2_CONTROLLER_DIAGNOSTICS)
 /* Schema3 trades latency/interrupted-stack details for the
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
 }
 r2_fault_capsule[FAULT_WORDS-1]=crc(r2_fault_capsule);__DMB();r2_fault_capsule[0]=FAULT_MAGIC;__DSB();
#if defined(R2_GM7_NATIVE) && defined(CONFIG_EXTRA_EXCEPTION_INFO)
 crash_capture(reason,esf,old_mask);
#endif
 k_fatal_halt(reason);CODE_UNREACHABLE;
}
#endif

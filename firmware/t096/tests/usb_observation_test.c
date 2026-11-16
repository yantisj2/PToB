/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Actual observer/boot bodies; synthetic MMIO/reset/interrupt boundaries. */
#define R2_USB_SOURCE_STREAM 1
#define R2_FAULT_DURABLE 1
#include <stdint.h>
#include <string.h>
#include <assert.h>
static uint32_t sample[8];
static unsigned snapshot_reads,health_reads,irq_depth;
void r2_usb_debug_snapshot(uint32_t out[8]){memcpy(out,sample,sizeof sample);++snapshot_reads;}
void r2_usb_health_snapshot(uint32_t out[4]){memcpy(out,sample,16);++health_reads;}
static unsigned irq_lock(void){return irq_depth++;}
static void irq_unlock(unsigned key){assert(irq_depth==key+1);irq_depth=key;}
#define main historical_watchdog_main
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#include "watchdog_test.c"
#pragma clang diagnostic pop
#undef main
static void boot(uint64_t number,uint32_t cause)
{
 uint32_t pending[WD_WORDS];memcpy(pending,previous,sizeof pending);
 clear_runtime();memcpy(previous,pending,sizeof pending);pending_attributed=false;
 usb_health=(struct r2_usb_health){0};id.boot=number;id.reset_cause=cause;
 assert(!r2_watchdog_boot(&id));
}
static void empty(void)
{
 memset(previous,0,sizeof previous);memset((void *)r2_watchdog_capsule,0,sizeof r2_watchdog_capsule);
 memcpy(id.silicon,"123456789abcdef0",17);boot(100,RESET_POR);
 memset(sample,0,sizeof sample);snapshot_reads=health_reads=0;irq_depth=0;
}
static void observe(uint32_t at){fake_ms=at;usb_observation(sample,at);}
static void healthy(void)
{
 sample[0]=0x3c17;sample[1]=9;sample[2]=73;sample[3]=912;
 sample[4]=0;sample[5]=0x1fffff;sample[6]=0x10001;sample[7]=3;
}
static void valid(void)
{
 assert(r2_watchdog_capsule[0]==R2_USB_OBSERVATION_MAGIC&&r2_watchdog_capsule[1]==1);
 assert(r2_watchdog_capsule[2]==100&&!r2_watchdog_capsule[3]);
 assert(r2_watchdog_capsule[33]==crc(r2_watchdog_capsule));
 assert(!memcmp((const void *)(r2_watchdog_capsule+4),sample,sizeof sample));
 assert(!memcmp((const void *)(r2_watchdog_capsule+12),R2_WATCHDOG_BINDING,64));
 assert(!memcmp((const void *)(r2_watchdog_capsule+28),id.silicon,16));
}
static unsigned preempt_reason;
static void capture_preempt(const atomic_t *read)
{
 if(read!=&reset_reason||!irq_depth)return;
 on_atomic_get=NULL;
 if(preempt_reason==WD_NO_SYNC)capsule(WD_NO_SYNC,0);
 else r2_watchdog_fatal(3);
}
int main(int argc,char **argv)
{
 if(argc>1&&!strcmp(argv[1],"require-capture")){
  empty();healthy();advance(100);assert(watchdog_step());
  return r2_watchdog_capsule[0]==R2_USB_OBSERVATION_MAGIC?0:42;
 }
 empty();observe(1);assert(!r2_watchdog_capsule[0]);
 sample[0]=1;observe(2);assert(!r2_watchdog_capsule[0]);
 healthy();observe(100);valid();assert(r2_watchdog_capsule[32]==100&&!reset_reason);
 uint32_t first[WD_WORDS];memcpy(first,(const void *)r2_watchdog_capsule,sizeof first);
 /* Traffic alone does not refresh this last transition or its timestamp. */
 sample[0]^=(1u<<22)|0x1e0;sample[1]++;sample[2]++;sample[3]++;sample[4]=UINT32_MAX;
 observe(200);assert(!memcmp(first,(const void *)r2_watchdog_capsule,sizeof first));
 healthy();sample[0]|=0xc008;sample[3]=0;observe(300);valid();
 assert(!reset_reason&&r2_watchdog_capsule[32]==300); /* legitimate suspend */
 if(argc>1){FILE *f=fopen(argv[1],"wb");assert(f&&fwrite((const void *)r2_watchdog_capsule,1,sizeof first,f)==sizeof first&&!fclose(f));}
 /* The production poll takes one8-word snapshot, never a second MMIO sample.
  * Stable suspend remains observed, not a reset verdict, for ten minutes. */
 snapshot_reads=health_reads=0;
 for(uint32_t at=425;at<=600300;at+=125){advance(at);assert(watchdog_step());}
 assert(snapshot_reads==4800&&!health_reads&&!reset_reason&&r2_watchdog_capsule[32]==300);
 /* Raw IRQ mask and clock/regulator changes matter even if F is unchanged. */
 for(unsigned index=5;index<=7;index++){
  sample[index]^=1;observe(600400+index);valid();assert(r2_watchdog_capsule[32]==600400+index);
 }
 sample[0]=0;observe(600500);valid(); /* observed unplug, no fault */
 assert(!reset_reason);
 /* A real context always wins, including a concurrent fatal/NO_SYNC writer. */
 const unsigned reasons[]={WD_NO_SYNC,WD_LORA_FAULT,WD_USB_FAULT,WD_FATAL+3};
 for(unsigned i=0;i<sizeof reasons/sizeof *reasons;i++){
  empty();healthy();observe(100);
  if(reasons[i]==WD_NO_SYNC)capsule(WD_NO_SYNC,0);
  else if(reasons[i]==WD_LORA_FAULT)r2_watchdog_lora(-EIO,1,2,3,4);
  else if(reasons[i]==WD_USB_FAULT)transport_fault(WD_USB_FAULT,-ETIMEDOUT,1,2,3,4);
  else r2_watchdog_fatal(3);
  memcpy(first,(const void *)r2_watchdog_capsule,sizeof first);
  sample[0]^=0xc008;observe(200);
  assert(!memcmp(first,(const void *)r2_watchdog_capsule,sizeof first)&&first[0]==WD_MAGIC&&first[4]==reasons[i]);
 }
 for(unsigned kind=0;kind<2;kind++){
  empty();healthy();preempt_reason=kind?WD_FATAL+3:WD_NO_SYNC;on_atomic_get=capture_preempt;
  observe(100);on_atomic_get=NULL;
  assert(r2_watchdog_capsule[0]==WD_MAGIC&&r2_watchdog_capsule[4]==preempt_reason&&watchdog_bound(&id));
 }
 /* Ordinary reset retains the observation as nonfault original context.
  * First unsaved pending observation survives later ordinary resets. */
 const uint32_t resets[]={RESET_PIN,RESET_SOFTWARE,RESET_POR,RESET_BROWNOUT,RESET_DEBUG,0};
 for(unsigned i=0;i<sizeof resets/sizeof *resets;i++){
  empty();healthy();observe(100);memcpy(first,(const void *)r2_watchdog_capsule,sizeof first);
  boot(101,resets[i]);assert(!memcmp(previous,first,sizeof first)&&!pending_attributed&&!r2_watchdog_capsule[0]);
  assert(r2_watchdog_pending_record()&&!strcmp(previous_reason_name(),"USB_OBSERVATION"));
  healthy();sample[0]|=0xc008;observe(300);boot(102,RESET_PIN);
  assert(!memcmp(previous,first,sizeof first));
  r2_watchdog_pending_saved(previous[33]^1);assert(r2_watchdog_pending_record());
  r2_watchdog_pending_saved(previous[33]);assert(!r2_watchdog_pending_record());
 }
 /* Every torn word, foreign factory, invalid binding and same-boot record
  * is rejected. Loss of retained RAM cannot fabricate healthy evidence. */
 for(unsigned word=0;word<WD_WORDS;word++){
  empty();healthy();observe(100);r2_watchdog_capsule[word]^=1;boot(101,RESET_PIN);
  assert(!previous[0]);
 }
 for(unsigned which=0;which<3;which++){
  empty();healthy();observe(100);
  if(which==0)r2_watchdog_capsule[28]^=1;
  if(which==1)((char *)(void *)(r2_watchdog_capsule+12))[0]='z';
  if(which==2)r2_watchdog_capsule[2]=101;
  r2_watchdog_capsule[33]=crc(r2_watchdog_capsule);boot(101,RESET_PIN);assert(!previous[0]);
 }
 /* Full-journal stand-in: unsaved observation remains in previous. A new
  * valid terminal fault must replace it; an old real pending fault wins. */
 for(unsigned i=1;i<sizeof reasons/sizeof *reasons;i++){
  empty();healthy();observe(100);boot(101,RESET_PIN);
  capsule(reasons[i],0);boot(102,RESET_PIN);
  assert(previous[0]==WD_MAGIC&&previous[4]==reasons[i]&&!pending_attributed&&r2_watchdog_reset_kind()==R2_UI_RESET_PIN);
  memcpy(first,previous,sizeof first);healthy();observe(300);boot(103,RESET_PIN);
  assert(!memcmp(previous,first,sizeof first));
 }
 for(unsigned which=0;which<2;which++){
  empty();healthy();observe(100);boot(101,RESET_PIN);
  healthy();observe(200);boot(102,which?RESET_CPU_LOCKUP:RESET_WATCHDOG);
  assert(previous[0]==R2_FAULT_RESET_MAGIC&&!previous[4]&&previous[5]==(which?RESET_CPU_LOCKUP:RESET_WATCHDOG));
 }
 empty();boot(101,RESET_CPU_LOCKUP);memcpy(first,previous,sizeof first);
 healthy();observe(300);boot(102,RESET_PIN);assert(!memcmp(first,previous,sizeof first));
 empty();healthy();observe(100);boot(101,RESET_PIN);memcpy(first,previous,sizeof first);
 uf2_intent.magic=0;memcpy((void *)uf2_intent.binding,R2_WATCHDOG_BINDING,64);
 memcpy((void *)uf2_intent.silicon,id.silicon,16);uf2_intent.checksum=uf2_crc();uf2_intent.magic=WD_MAGIC;
 boot(102,RESET_WATCHDOG);assert(!memcmp(previous,first,sizeof first)); /* maintenance is not a fault */
 /* Power loss before successful append can simply remove the observation. */
 empty();healthy();observe(100);memset((void *)r2_watchdog_capsule,0,sizeof first);boot(101,RESET_POR);
 assert(!r2_watchdog_pending_record());
 puts("PASS actual USB observation: last masked/control/clock transition,10min legitimate suspend,no runtime reset,one snapshot,CRC/identity,terminal/NO_SYNC priority,corruption,first-pending/ACK and bare-reset precedence");
 return 0;
}

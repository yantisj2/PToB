/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * The sole fault flash writer: synchronous, one startup, then permanently sealed.
 * No radio owner, work item, timer, NV erase, settings or provisioning calls. */
#include "fault_boot.h"
#include "fault_store.h"
#include "diagnostic.h"
#include "health.h"
#include "watchdog.h"
#include "build_identity.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
_Static_assert(R2_DIAG_BYTES==R2_FAULT_STORE_BYTES&&R2_DIAG_SLOTS==R2_FAULT_STORE_SLOTS,"Exact historical diagnostic allocation");
static const struct device *const flash=DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));
/* Pinned Zephyr ticker backend scalar reader; never enqueue or bypass its driver. */
extern bool nrf_flash_sync_is_required(void);
struct fault_boot_state {
 int cpu_result,watchdog_result;
 unsigned cpu_slot,watchdog_slot;
 bool attempted,write_open;
};
static struct fault_boot_state fault_boot_state;
_Static_assert(sizeof fault_boot_state<=24,"Bounded boot-only fault metadata");
static int read_flash(void *context,unsigned offset,void *out,size_t n)
{
 (void)context;
 if(offset>R2_DIAG_BYTES||n>R2_DIAG_BYTES-offset||n>64)return -EACCES;
 return flash_read(flash,R2_DIAG_START+offset,out,n);
}
static int write_flash(void *context,unsigned offset,const void *data,size_t n)
{
 (void)context;
 if(!fault_boot_state.write_open||nrf_flash_sync_is_required()||offset>R2_DIAG_BYTES||n>R2_DIAG_BYTES-offset||
    !((offset%512==0&&n==508)||(offset%512==508&&n==4)))return -EACCES;
 r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_WRITE,-EINPROGRESS);
 int result=flash_write(flash,R2_DIAG_START+offset,data,n);
 r2_health_operation(R2_HEALTH_FLASH,R2_HEALTH_WRITE,result);return result;
}
static uint64_t now(void *context){(void)context;return(uint64_t)k_uptime_get();}
static const struct r2_fault_io io={NULL,read_flash,write_flash,now};
static void watchdog_package(uint32_t p[128],const volatile uint32_t *raw,const struct r2_identity *id)
{
 memset(p,0,512);p[0]=R2_FAULT_WD_MAGIC;p[1]=R2_FAULT_WD_SCHEMA;p[2]=UINT32_MAX;p[3]=512;
 p[4]=raw[2];p[5]=raw[3];p[6]=r2_crash_hex32(id->silicon+8);p[7]=r2_crash_hex32(id->silicon);
 /* The old capsule lacks a source SHA. Never substitute this collecting image. */
 for(unsigned i=0;i<8;i++)for(unsigned b=0;b<4;b++){
  unsigned at=i*8+b*2;const volatile uint8_t *binding=(const volatile uint8_t *)(raw+12);
  p[16+i]|=((r2_crash_nibble((char)binding[at])<<4)|r2_crash_nibble((char)binding[at+1]))<<(8*b);
 }
 bool observed=raw[0]==R2_FAULT_RESET_MAGIC;
 unsigned reason=raw[4];p[24]=reason;
 p[25]=!observed&&((reason>=2&&reason<=6)||(reason>=16&&reason<256))?R2_FAULT_WD_TERMINAL:0;
 p[26]=observed?R2_FAULT_CONTEXT_HARDWARE_RESET:R2_FAULT_CONTEXT_SOFTWARE_WD;
 p[27]=observed?raw[5]:0; /* Raw mixed flags stay ambiguous; no CPU frame. */
 for(unsigned i=0;i<34;i++)p[64+i]=raw[i];
 p[126]=r2_crash_crc(p);p[127]=R2_CRASH_COMMIT;
}
void r2_fault_boot(const struct r2_identity *id,uint32_t package[128])
{
 if(fault_boot_state.attempted)return; /* no second write window, even after failure */
 fault_boot_state.attempted=true;fault_boot_state.write_open=false;
 fault_boot_state.cpu_result=fault_boot_state.watchdog_result=-ENOENT;
 fault_boot_state.cpu_slot=fault_boot_state.watchdog_slot=R2_FAULT_STORE_SLOTS;
 uint64_t begin=now(NULL);
 if(!id||id->result||!package||begin>UINT64_MAX-500){
  fault_boot_state.cpu_result=fault_boot_state.watchdog_result=-EACCES;return;
 }
 uint64_t deadline=begin+500;
 const volatile uint32_t *cpu=r2_health_crash_pending();
 const volatile uint32_t *watchdog=r2_watchdog_pending_record();
 if(!cpu&&!watchdog)return; /* Normal fault-free boot never opens the journal. */
 if(!device_is_ready(flash)){
  fault_boot_state.cpu_result=fault_boot_state.watchdog_result=-ENODEV;return;
 }
 if(nrf_flash_sync_is_required()){
  fault_boot_state.cpu_result=fault_boot_state.watchdog_result=-EBUSY;return;
 }
 fault_boot_state.write_open=true;
 if(cpu){
  uint32_t retained_crc=cpu[126];
  for(unsigned i=0;i<128;i++)package[i]=cpu[i];
  fault_boot_state.cpu_result=r2_fault_append(&io,package,deadline,&fault_boot_state.cpu_slot);
  if(!fault_boot_state.cpu_result)r2_health_crash_saved(retained_crc);
 }
 if(watchdog){
  uint32_t retained_crc=watchdog[33];watchdog_package(package,watchdog,id);
  fault_boot_state.watchdog_result=r2_fault_append(&io,package,deadline,&fault_boot_state.watchdog_slot);
  if(!fault_boot_state.watchdog_result)r2_watchdog_pending_saved(retained_crc);
 }
 fault_boot_state.write_open=false;
}
int r2_fault_cpu_result(void){return fault_boot_state.cpu_result;}
int r2_fault_watchdog_result(void){return fault_boot_state.watchdog_result;}
unsigned r2_fault_cpu_slot(void){return fault_boot_state.cpu_slot;}
unsigned r2_fault_watchdog_slot(void){return fault_boot_state.watchdog_slot;}
int r2_fault_flash_read(unsigned chunk,uint8_t out[64])
{
 if(chunk>=128||!out)return -EINVAL;
 if(!fault_boot_state.attempted||fault_boot_state.write_open)return -EAGAIN;
 if(!device_is_ready(flash))return -ENODEV;
 return read_flash(NULL,chunk*64,out,64);
}

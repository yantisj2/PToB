/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#define R2_FAULT_DURABLE 1
#define R2_WATCHDOG 1
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "fault_store.h"
struct r2_identity {int result;uint64_t boot;char silicon[17];};
struct device {int ignored;};
static struct device fake_device;
#define DEVICE_DT_GET(x) (&fake_device)
#define DT_CHOSEN(x) 0
#define R2_DIAG_BYTES 8192u
#define R2_DIAG_SLOTS 16u
#define R2_DIAG_START 0xe1000u
#define R2_HEALTH_FLASH 0
#define R2_HEALTH_WRITE 1
static bool ready=true,cpu_pending,wd_pending,ticker_running;
bool nrf_flash_sync_is_required(void){return ticker_running;}
static uint32_t cpu_record[128],wd_record[34];
_Alignas(4) static uint8_t nv[8192];
static uint64_t clock_ms;
static unsigned reads,writes,acked_cpu,acked_wd,read_cost;
static int write_failure;
static bool device_is_ready(const struct device *d){assert(d==&fake_device);return ready;}
static int64_t k_uptime_get(void){return(int64_t)clock_ms;}
static int flash_read(const struct device *d,unsigned at,void *p,size_t n)
{assert(d==&fake_device&&at>=R2_DIAG_START&&at+n<=R2_DIAG_START+8192&&n<=64);reads++;clock_ms+=read_cost;memcpy(p,nv+(at-R2_DIAG_START),n);return 0;}
static int flash_write(const struct device *d,unsigned at,const void *p,size_t n)
{
 assert(d==&fake_device&&at>=R2_DIAG_START&&at+n<=R2_DIAG_START+8192);
 writes++;if(write_failure)return write_failure;
 for(unsigned i=0;i<n;i++){uint8_t v=((const uint8_t *)p)[i];assert((nv[at-R2_DIAG_START+i]&v)==v);nv[at-R2_DIAG_START+i]=v;}return 0;
}
static void r2_health_operation(unsigned lane,unsigned operation,int result){assert(lane==0&&operation==1);(void)result;}
static const volatile uint32_t *r2_health_crash_pending(void){return cpu_pending?cpu_record:NULL;}
static void r2_health_crash_saved(uint32_t crc){assert(cpu_pending&&crc==cpu_record[126]);acked_cpu++;cpu_pending=false;}
static const volatile uint32_t *r2_watchdog_pending_record(void){return wd_pending?wd_record:NULL;}
static void r2_watchdog_pending_saved(uint32_t crc){assert(wd_pending&&crc==wd_record[33]);acked_wd++;wd_pending=false;}
#include "../src/fault_boot.c"
static uint32_t wd_crc(const uint32_t *p)
{
 uint32_t c=UINT32_MAX;
 for(unsigned i=1;i<33;i++)for(unsigned b=0;b<4;b++){c^=(p[i]>>(8*b))&255;for(unsigned k=0;k<8;k++)c=(c>>1)^(0xedb88320u&-(c&1));}
 return ~c;
}
static void reset(bool blank)
{
 fault_boot_state=(struct fault_boot_state){0};clock_ms=reads=writes=acked_cpu=acked_wd=read_cost=0;write_failure=0;ready=true;ticker_running=false;cpu_pending=wd_pending=false;
 if(blank)memset(nv,255,sizeof nv);
}
static void pending_cpu(void)
{
 memset(cpu_record,0,sizeof cpu_record);cpu_record[0]=R2_CRASH_MAGIC;cpu_record[1]=7;cpu_record[2]=UINT32_MAX;cpu_record[3]=512;
 cpu_record[4]=1;cpu_record[6]=0x9abcdef0;cpu_record[7]=0x12345678;cpu_record[25]=R2_CRASH_SCB_POST_CLEAR;
 cpu_record[126]=r2_crash_crc(cpu_record);cpu_record[127]=R2_CRASH_COMMIT;cpu_pending=true;
}
static void pending_wd(unsigned reason)
{
 memset(wd_record,0,sizeof wd_record);wd_record[0]=0x52325744;wd_record[1]=1;wd_record[2]=1;wd_record[4]=reason;
 memcpy(wd_record+12,"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",64);
 memcpy(wd_record+28,"123456789abcdef0",16);wd_record[33]=wd_crc(wd_record);wd_pending=true;
}
int main(int argc,char **argv)
{
 struct r2_identity id={.boot=2,.silicon="123456789abcdef0"};uint32_t package[128];uint8_t out[64];
 reset(true);r2_fault_boot(&id,package);assert(!reads&&!writes&&r2_fault_cpu_result()==-ENOENT&&r2_fault_watchdog_result()==-ENOENT);
 pending_cpu();r2_fault_boot(&id,package);assert(!writes&&cpu_pending); /* sealed, no runtime retry */
 assert(write_flash(NULL,0,package,508)==-EACCES&&!writes);
 reset(true);pending_cpu();pending_wd(5);r2_fault_boot(&id,package);
 assert(!cpu_pending&&!wd_pending&&acked_cpu==1&&acked_wd==1&&writes==4&&!fault_boot_state.write_open);
 assert(r2_fault_cpu_slot()==0&&r2_fault_watchdog_slot()==1);
 assert(r2_fault_record_valid((const uint32_t *)nv,false)&&r2_fault_wd_valid((const uint32_t *)(nv+512),false));
 if(argc>=2){FILE *f=fopen(argv[1],"wb");assert(f&&fwrite(nv+512,1,512,f)==512&&!fclose(f));}
 unsigned before=writes;r2_fault_boot(&id,package);assert(writes==before);
 assert(write_flash(NULL,508,package,4)==-EACCES&&writes==before);
 for(unsigned i=0;i<128;i++)assert(!r2_fault_flash_read(i,out)&&!memcmp(out,nv+i*64,64));
 assert(r2_fault_flash_read(128,out)==-EINVAL&&r2_fault_flash_read(UINT32_MAX,out)==-EINVAL);
 /* Repeated boot with the same still-pending RAM deduplicates full bytes. */
 reset(false);pending_cpu();pending_wd(5);r2_fault_boot(&id,package);assert(!writes&&acked_cpu==1&&acked_wd==1);
 reset(true);memset(nv,0xa5,sizeof nv);pending_cpu();pending_wd(5);r2_fault_boot(&id,package);
 assert(r2_fault_cpu_result()==-ENOSPC&&r2_fault_watchdog_result()==-ENOSPC&&!writes&&cpu_pending&&wd_pending&&!acked_cpu&&!acked_wd);
 reset(true);pending_cpu();pending_wd(5);write_failure=-EIO;r2_fault_boot(&id,package);
 assert(cpu_pending&&wd_pending&&!acked_cpu&&!acked_wd&&!fault_boot_state.write_open);
 /* Shared500ms includes both pending records; CPU succeeds, WD runs out. */
 reset(true);pending_cpu();pending_wd(1);read_cost=2;r2_fault_boot(&id,package);
 assert(!cpu_pending&&acked_cpu==1&&wd_pending&&!acked_wd&&r2_fault_watchdog_result()==-ETIMEDOUT&&!fault_boot_state.write_open);
 reset(true);ticker_running=true;pending_cpu();pending_wd(5);r2_fault_boot(&id,package);
 assert(!reads&&!writes&&cpu_pending&&wd_pending&&!acked_cpu&&!acked_wd&&r2_fault_cpu_result()==-EBUSY&&!fault_boot_state.write_open);
 ticker_running=false;r2_fault_boot(&id,package);assert(!writes); /* no runtime reopen */
 reset(true);pending_wd(0);wd_record[0]=R2_FAULT_RESET_MAGIC;wd_record[1]=2;wd_record[2]=2;wd_record[5]=16;wd_record[33]=wd_crc(wd_record);
 r2_fault_boot(&id,package);assert(acked_wd==1&&!wd_pending&&r2_fault_wd_valid((const uint32_t *)nv,false));
 assert(((const uint32_t *)nv)[26]==R2_FAULT_CONTEXT_HARDWARE_RESET&&((const uint32_t *)nv)[27]==16);
 if(argc>=3){FILE *f=fopen(argv[2],"wb");assert(f&&fwrite(nv,1,512,f)==512&&!fclose(f));}
 reset(true);ready=false;pending_cpu();r2_fault_boot(&id,package);assert(cpu_pending&&!acked_cpu&&!writes&&r2_fault_cpu_result()==-ENODEV);
 reset(true);id.result=-EIO;pending_cpu();r2_fault_boot(&id,package);assert(cpu_pending&&!writes&&!fault_boot_state.write_open&&r2_fault_cpu_result()==-EACCES);
 puts("PASS actual sealed boot owner: CPU+WD verified ACK/dedup, fault-free zero journal IO, no runtime writes/retry, exact128 read chunks, full/torn failure retention and shared500ms");
 return 0;
}

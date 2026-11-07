/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Included driver bodies retain their original Apache-2.0 authorship/terms.
 * Dispatcher/branch proof only; NVMC, semaphore and ticker state are modeled. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <errno.h>
#include <stdio.h>
struct device {int unused;};
static bool ticker_started,sem_free=true;
static unsigned direct_calls,sync_calls,locks,unlocks;
static bool ticker_is_initialized(unsigned id){assert(!id);return ticker_started;}
#define DT_REG_ADDR(x) 0u
#define SOC_NV_FLASH_NODE 0
#define LOG_ERR(...) ((void)0)
#define SYNC_LOCK() do {assert(sem_free);sem_free=false;locks++;}while(0)
#define SYNC_UNLOCK() do {assert(!sem_free);sem_free=true;unlocks++;}while(0)
static bool is_regular_addr_valid(off_t at,size_t n){return at>=0&&(uint64_t)at+n<=1024*1024;}
static bool is_uicr_addr_valid(off_t at,size_t n){(void)at;(void)n;return false;}
static bool is_aligned_32(uint32_t at){return !(at&3);}
static int write(off_t at,const void *data,size_t n)
{assert(at>=0xe1000&&at+(off_t)n<=0xe3000&&data&&n);direct_calls++;return 0;}
static int write_synchronously(off_t at,const void *data,size_t n)
{(void)at;(void)data;(void)n;sync_calls++;return -ECANCELED;}
#include "fault_boot_driver_actual.inc"
int main(void)
{
 struct device d={0};uint32_t p[128]={0};
 assert(!nrf_flash_sync_is_required());
 assert(!flash_nrf_write(&d,0xe1000,p,508));
 assert(!flash_nrf_write(&d,0xe11fc,p+127,4));
 assert(direct_calls==2&&!sync_calls&&locks==2&&unlocks==2&&sem_free);
 assert(flash_nrf_write(&d,0xe1001,p,508)==-EINVAL);
 assert(flash_nrf_write(&d,0xe1000,p,507)==-EINVAL&&locks==2);
 ticker_started=true;assert(nrf_flash_sync_is_required());
 assert(flash_nrf_write(&d,0xe1000,p,508)==-ECANCELED&&sync_calls==1&&direct_calls==2);
 puts("PASS exact pinned stock flash dispatcher: pre-ticker direct NVMC branch, alignment guards and initialized-ticker contrast; owner must reject active ticker");
 return 0;
}

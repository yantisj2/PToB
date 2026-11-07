/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "crash_record.h"
#define R2_FAULT_STORE_BYTES 8192u
#define R2_FAULT_STORE_SLOTS 16u
#define R2_FAULT_WD_MAGIC UINT32_C(0x57444631)
#define R2_FAULT_WD_SCHEMA 1u
#define R2_FAULT_RESET_MAGIC UINT32_C(0x52325253)
#define R2_FAULT_CONTEXT_SOFTWARE_WD 1u
#define R2_FAULT_CONTEXT_HARDWARE_RESET 2u
#define R2_FAULT_WD_WORD 64u
#define R2_FAULT_WD_TERMINAL 1u
/* Source SHA is explicitly unknown for the old136B watchdog capsule.
 * Header16..23 retains its binary exact build binding; raw136B is64..97.
 * This record is watchdog CONTEXT, never a CPU exception/register frame. */
struct r2_fault_io {
 void *context;
 int (*read)(void *,unsigned,void *,size_t);
 int (*write)(void *,unsigned,const void *,size_t);
 uint64_t (*now)(void *);
};
bool r2_fault_wd_valid(const volatile uint32_t *record,bool retained);
bool r2_fault_record_valid(const volatile uint32_t *record,bool retained);
/* Startup-only transaction engine. Caller owns the existing512B package.
 * No allocation, erase, retry, thread, policy restore or runtime writer.
 * One shared absolute deadline must cover all CPU+WD attempts in a boot.
 * Returns0 only after exact full-record durable verification/deduplication. */
int r2_fault_append(const struct r2_fault_io *io,uint32_t package[128],
                    uint64_t deadline,unsigned *stored_slot);

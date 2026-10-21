/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#define R2_LORA_FAULT_WORDS 16u
#define R2_LORA_FAULT_MAGIC UINT32_C(0x4c525731)
#define R2_LORA_WORKER_MS 30000u
enum r2_lora_stage {R2_LORA_STAGE_IDLE=0,R2_LORA_STAGE_START,
 R2_LORA_STAGE_BUSY_BEFORE,R2_LORA_STAGE_DMA,R2_LORA_STAGE_BUSY_AFTER,
 R2_LORA_STAGE_TAKE,R2_LORA_STAGE_REARM,R2_LORA_STAGE_STOP,
 R2_LORA_STAGE_LATE_LAUNCH,R2_LORA_STAGE_PRECOMMIT};
/* The journal scanner finishes before the LoRa worker starts. Its 512B
 * scratch then has this single lifetime. No owner/stack reserve is borrowed.
 * All shared publication and first-fault capture is protected by IRQ fences;
 * flash, CRC, waiting and service locks never run in those fences. */
struct r2_lora_retained_fault {
 uint32_t record[R2_LORA_FAULT_WORDS];
 uint64_t boot;
 uint32_t terminal;
};
struct r2_lora_watchdog_state {
 uint32_t record[R2_LORA_FAULT_WORDS];
 uint64_t journal_generation;
 uint32_t serial,submitted,saved,coalesced,stage,stage_at,beat,monitor,
  closure,irq_status,terminal,fault_at,reset_requested,launch_us,launch_ms;
 int journal_result;
 /* Dedicated GPIO NSS belongs to the LoRa owner, never the SPI context.
  * Precommit is bounded separately; a callback proves the real command end.
  * These fields use the existing startup-only512B scan reserve. */
 uint64_t launch_finish;
 uint32_t launch_budget,precommit_us,precommit_ms,command_commit_us,command_committed;
 struct r2_lora_retained_fault retained;
 /* BLE-owned current actual origin, not a LoRa grant. Unused scan tail. */
 struct {uint64_t boot;uint32_t epoch;bool active;} origin;
};
union r2_diag_scratch {
 uint32_t scan[128];
 struct r2_lora_watchdog_state lora;
 /* Diagnostic canary only; tail never overlaps live LoRa state. */
 struct {uint8_t owner[sizeof(struct r2_lora_watchdog_state)];uint32_t words[24];} controller;
};
extern union r2_diag_scratch r2_diag_scratch;
_Static_assert(sizeof(union r2_diag_scratch)==512,"Existing diagnostic scan reserve");
void r2_lora_watchdog_check(void); /* Out-of-band WDT observer; no flash/SPI. */
void r2_lora_watchdog_poll(void); /* Main alone requests the journal/reset. */

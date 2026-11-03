/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define R2_LORA_FAULT_WORDS 16u
#define R2_LORA_FAULT_MAGIC UINT32_C(0x4c525731)
#define R2_LORA_WORKER_MS 30000u
enum r2_lora_stage {R2_LORA_STAGE_IDLE=0,R2_LORA_STAGE_START,
 R2_LORA_STAGE_BUSY_BEFORE,R2_LORA_STAGE_DMA,R2_LORA_STAGE_BUSY_AFTER,
 R2_LORA_STAGE_TAKE,R2_LORA_STAGE_REARM,R2_LORA_STAGE_STOP,
 R2_LORA_STAGE_LATE_LAUNCH,R2_LORA_STAGE_PRECOMMIT};
/* Fixed512B current-boot RAM fault/observer scratch. No journal scanner or
 * flash writer is active. No owner/stack reserve is borrowed.
 * All shared publication and first-fault capture is protected by IRQ fences;
 * flash, CRC, waiting and service locks never run in those fences. */
struct r2_lora_retained_fault {
 uint32_t record[R2_LORA_FAULT_WORDS];
 uint64_t boot;
 uint32_t terminal;
};
struct r2_lora_attempt {
 uint64_t target_us,committed_us;uint32_t valid,cursor,serial,
  prepare_us,commit_us,prepare_max_us,commit_max_us;
 int32_t postcommit_lead_us,first_due_late_us;
 uint32_t source_room_us,source_phase_us,source_revision,prepared_sequence;
};
struct r2_lora_store_timing {uint32_t sequence,body_us,verify_us,commit_us;uint8_t phase;};
struct r2_lora_watchdog_state {
 uint32_t record[R2_LORA_FAULT_WORDS];
 uint64_t journal_generation;
 uint32_t serial,submitted,saved,coalesced,stage,stage_at,beat,monitor,
  closure,irq_status,terminal,fault_at,reset_requested,launch_us,launch_ms;
 int journal_result;
 /* Dedicated GPIO NSS belongs to the LoRa owner, never the SPI context.
  * Precommit is bounded separately; a callback proves the real command end.
  * These fields use the fixed512B RAM reserve. */
 uint64_t launch_finish;
 uint32_t launch_budget,precommit_us,precommit_ms,command_commit_us,command_committed;
 struct r2_lora_retained_fault retained;
 /* BLE-owned current actual origin, not a LoRa grant. Unused RAM tail. */
 struct {uint64_t boot;uint32_t epoch;bool active;} origin;
 /* Original drain deadline and actual observations; a late safe closure
  * fails that app-drain attempt but need not reset unrelated BLE owners. */
 uint64_t drain_deadline_us,drain_promoted_us,drain_poll_us,drain_ack_us;
 uint32_t drain_late_count,drain_late;
 /* Passive TX attribution occupies only the unused RAM tail. Valid bits:
  * prepare1, committed2, first due check4, fresh source8, command attempted16,
  * reservation refused32.
  * Every reservation attempt clears stale source values before RAM precharge. */
 struct r2_lora_attempt attempt;
#ifdef R2_LORA_PASSIVE_SLOW
 /* Slow has no fast TX-lab arm or historical NV timing writer. Reuse that
  * existing40B tail for32B last/max due counters; preserve the controller
  * observer's original416B boundary and the fixed512B reserve. */
 uint32_t tx_due_us[2][4];
 /* Startup attempts and duplicate-bound fences, current boot only. */
 uint32_t startup_counts[2];
#else
 /* Diagnostic-only finite arm state; still inside the fixed512B RAM. */
 struct {uint64_t armed_us;uint32_t new_charge_us;bool used;} diagnostic;
 struct r2_lora_store_timing store;
#endif
};
union r2_diag_scratch {
 uint32_t scan[128];
 struct r2_lora_watchdog_state lora;
 /* Fault capture / optional verbose observer; tail never overlaps live LoRa state. */
 struct {uint8_t owner[sizeof(struct r2_lora_watchdog_state)];uint32_t words[24];} controller;
};
extern union r2_diag_scratch r2_diag_scratch;
_Static_assert(sizeof(union r2_diag_scratch)==512,"Fixed current-boot diagnostic reserve");
_Static_assert(sizeof(struct r2_lora_watchdog_state)==416,"Preserve LoRa/controller scratch boundary");
_Static_assert(offsetof(union r2_diag_scratch,controller.words)==416,"Preserve24word controller observer");
#ifdef R2_LORA_PASSIVE_SLOW
_Static_assert(offsetof(struct r2_lora_watchdog_state,tx_due_us)==376,"Reuse only unused slow scratch tail");
#endif
void r2_lora_watchdog_check(void); /* Out-of-band WDT observer; no flash/SPI. */
void r2_lora_watchdog_poll(void); /* Main alone requests RAM evidence/reset. */

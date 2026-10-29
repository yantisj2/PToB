/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "ble_service.h"
#define R2_DIAG_BYTES 8192u
#define R2_DIAG_PAGE_BYTES 4096u
#define R2_DIAG_RECORD_BYTES 512u
#define R2_DIAG_SLOTS 16u
#define R2_DIAG_SAMPLE_COUNT 3u
#define R2_DIAG_COMMAND_COUNT 3u
#ifdef CONFIG_BOARD_NRF52_BSIM
#define R2_DIAG_START 0x70000u /* nRF52833 simulation has512KiB, isolated fixture region */
#else
#define R2_DIAG_START 0xe1000u
#endif
#define R2_DIAG_BANKS 2u
/* Best-effort event journal in bank0. Fresh-image cleanup before controller
 * startup; same-image record retained until a new event. Bank1 untouched.
 * Sixteen512B committed records append within two4KiB pages. Each retains
 * pre/event/post snapshots and three recent state-changing commands.
 * Routine flash logging stops after boot; only selected events cause a write. */
int r2_diag_init(const struct r2_identity *id);
int r2_diag_arm(uint64_t original_arrival_ms,uint32_t seconds);
int r2_diag_mark(void);
void r2_diag_quiesce(void);
void r2_diag_sample(const struct r2_service_status *s,const struct r2_clock_view *clock);
void r2_diag_command(uint32_t request,unsigned operation,int result,uint64_t arrival);
/* UCS1 namespace in the existing three-command context; main-thread only. */
void r2_diag_uc_trace(const uint32_t trace[3]);
void r2_diag_watchdog(unsigned retry,int error);
int r2_diag_lora(const uint32_t record[16],bool terminal,uint64_t *token);
int r2_diag_record_result(uint64_t token);
void r2_diag_finish(int error);
void r2_diag_poll(void);
int r2_diag_state(void);
/* Separate optional boot-crash persistence result; does not gate GM startup. */
int r2_diag_crash_result(void);
int r2_diag_read(unsigned bank,unsigned chunk,uint8_t out[64]);

bool r2_diag_busy(void);
/* Reset/power-off gate: an idle failed writer is not a durable record. */
bool r2_diag_saved(void);
bool r2_diag_maintenance_safe(void);
bool r2_diag_shutdown_ready(void);

void r2_diag_cancel_arm(int error);

struct r2_diag_stats {uint32_t saved,write_bytes,page_erases,coalesced,erase_deferred;};
void r2_diag_stats(struct r2_diag_stats *out);

uint32_t r2_diag_stack_unused(void);

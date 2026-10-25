/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
/* Existing timing journal R2_TIMING_GM_CONTROL detail:0x7000|stage<<8|kind.
 * Endpoints are raw16MHz capture values, not host time. Sequence/deadline
 * records are full u64 split low32/high32, associated by peer/kind/boot. */
enum r2_gm7_journal_stage {
  R2_GM7_J_RX=1, R2_GM7_J_TX, R2_GM7_J_SEQUENCE,
  R2_GM7_J_EVENT_DEADLINE, R2_GM7_J_MERGE, R2_GM7_J_SOURCE
};
#define R2_GM7_JOURNAL(stage,kind) (0x7000u | ((stage)<<8) | (kind))
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
int r2_gm7_diagnostic_format(char *, size_t, unsigned page, const char *source,
                             uint64_t physical_boot, bool runtime_gm);
/* Bounded asynchronous owner request. EINPROGRESS means queued; gm00 reports
 * actual validation result. The request cannot survive scope/lifecycle change. */
int r2_gm7_request_yield(void);

/* Initial GM boot only. Read published capture readiness; never requests HF,
 * advances capture ownership or treats fix/dwell as controller readiness. */
int r2_gm7_start_ready(void);
void r2_gm7_start_failed(int error);

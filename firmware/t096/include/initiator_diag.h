/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 * Passive counters only. No controller operation, recovery or authority.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
enum r2_init_counter {
 R2_INIT_PREPARE, R2_INIT_RX, R2_INIT_MATCH, R2_INIT_REQUEST,
 R2_INIT_WINDOW, R2_INIT_BUFFER, R2_INIT_ABORT,
 R2_INIT_AUX_NO_CONTEXT, R2_INIT_AUX_RSP_OK, R2_INIT_AUX_RSP_NO_DONE,
 R2_INIT_AUX_RSP_BAD_CRC, R2_INIT_AUX_RSP_INVALID, R2_INIT_COUNTERS
};
struct r2_init_diag {
 uint32_t count[R2_INIT_COUNTERS];
 uint8_t address[6],type,channel;
 uint8_t rx_free,rx_low,rx_needed;
};
/* Serialized native LLL writer; thread snapshots exclude its IRQ. */
void r2_init_diag_count(unsigned counter);
void r2_init_diag_starved(unsigned available,unsigned needed);
void r2_init_diag_target(const uint8_t address[6],unsigned type,unsigned channel);
void r2_init_diag_snapshot(struct r2_init_diag *out);
void r2_init_diag_emit(uint64_t peer,int result,const struct r2_init_diag *before);
void r2_init_diag_aux_rsp(bool done,bool crc,bool valid);
/* Main-only bounded JSON append of the last completed attempt. Survives
 * journal overwrite, never controller/session/precision authority. */
int r2_init_diag_append(char *,size_t,int length);

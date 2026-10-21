/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#ifndef PTOB_CONTROLLER_SNAPSHOT_H
#define PTOB_CONTROLLER_SNAPSHOT_H
#include <stdint.h>
struct ptob_controller_snapshot {
    uint16_t sampled_handle, lll_handle;
    uint16_t local_prt, remote_prt, prt_reload;
    uint16_t supervision_expire, connect_expire;
    uint8_t handle_in_range, handle_matches, head_present;
    uint8_t local_state, remote_state, reason_final;
    uint8_t head_proc, head_state, head_done;
};
/* Caller holds irq_lock continuously across host handle/state and this call.
 * UP + no zero-latency writers only. No pointer survives the call.
 * 'matches' describes current handle, not original app incarnation. */
void ptob_controller_snapshot_locked(uint16_t handle,
                                    struct ptob_controller_snapshot *out);
#endif

/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#ifndef PTOB_CONTROLLER_SNAPSHOT_H
#define PTOB_CONTROLLER_SNAPSHOT_H
#include <stdint.h>
struct ptob_ticker_snapshot {
    uint32_t ticks_periodic;
    uint16_t lazy_current;
    uint8_t valid, context_matches, req, ack, job_guard, worker_trigger;
};
struct ptob_controller_snapshot {
    uint16_t sampled_handle, lll_handle;
    uint16_t local_prt, remote_prt, prt_reload;
    uint16_t supervision_expire, connect_expire;
    uint8_t handle_in_range, handle_matches, head_present;
    uint8_t local_state, remote_state, reason_final;
    uint8_t head_proc, head_state, head_done;
    uint32_t access_address;
    uint16_t event_counter, latency_event;
    uint8_t role, ull_ref, latency_cancel, update_mark, disable_mark;
    struct ptob_ticker_snapshot ticker;
};
/* Caller holds irq_lock continuously across host handle/state and this call.
 * UP + no zero-latency writers only. No pointer survives the call.
 * 'matches' describes current handle, not original app incarnation. */
void ptob_controller_snapshot_locked(uint16_t handle,
                                    struct ptob_controller_snapshot *out);
/* Private build-generated diagnostic symbol; caller holds the same IRQ mask.
 * Context equality describes this instant, never a connection generation. */
void ptob_ticker_snapshot_locked(uint8_t instance, uint8_t ticker_id,
                                const void *context, struct ptob_ticker_snapshot *out);
void ptob_latency_cancel_fault(uint32_t status, void *param);
#define PTOB_LATENCY_CANCEL_FAULT_MARKER 0xC1u
#endif

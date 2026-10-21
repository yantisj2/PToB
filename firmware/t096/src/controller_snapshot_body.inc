/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
void ptob_controller_snapshot_locked(uint16_t handle,
                                    struct ptob_controller_snapshot *out)
{
    *out = (struct ptob_controller_snapshot){ .sampled_handle = handle,
                                           .lll_handle = UINT16_MAX };
    if (handle >= CONFIG_BT_MAX_CONN) {
        return;
    }
    struct ll_conn *conn = ll_conn_get(handle);
    out->handle_in_range = 1;
    out->lll_handle = conn->lll.handle;
    out->handle_matches = conn->lll.handle == handle;
    out->access_address = (uint32_t)conn->lll.access_addr[0] |
        ((uint32_t)conn->lll.access_addr[1] << 8) |
        ((uint32_t)conn->lll.access_addr[2] << 16) |
        ((uint32_t)conn->lll.access_addr[3] << 24);
    out->role = conn->lll.role;
    out->event_counter = conn->lll.event_counter;
    out->latency_event = conn->lll.latency_event;
    out->ull_ref = conn->ull.ref;
    out->latency_cancel = conn->periph.latency_cancel;
    out->update_mark = conn == ull_update_mark_get();
    out->disable_mark = conn == ull_disable_mark_get();
    ptob_ticker_snapshot_locked(TICKER_INSTANCE_ID_CTLR,
                               TICKER_ID_CONN_BASE + handle, conn, &out->ticker);
    out->local_state = conn->llcp.local.state;
    out->remote_state = conn->llcp.remote.state;
    out->local_prt = conn->llcp.local.prt_expire;
    out->remote_prt = conn->llcp.remote.prt_expire;
    out->prt_reload = conn->llcp.prt_reload;
    out->reason_final = conn->llcp_terminate.reason_final;
    out->supervision_expire = conn->supervision_expire;
    out->connect_expire = conn->connect_expire;
    /* Inactive slot scalars may be stale. Never follow its context pointer. */
    if (!out->handle_matches) {
        return;
    }
    struct proc_ctx *ctx = (struct proc_ctx *)
        sys_slist_peek_head(&conn->llcp.local.pend_proc_list);
    if (ctx) {
        out->head_present = 1;
        out->head_proc = ctx->proc;
        out->head_state = ctx->state;
        out->head_done = ctx->done;
    }
}

/* Failure-only callback boundary. Validate by bounded pool-address equality
 * before dereferencing param. A current valid/reused slot is not an incarnation.
 * The pinned ticker JOB executes in the ULL ISR; PA writers are thread-only.
 * No thread PA writer can resume between this unlock and the original assert. */
void ptob_latency_cancel_fault(uint32_t status, void *param)
{
    unsigned key = irq_lock();
    uint32_t slot = 15, flags = 0, handle = UINT16_MAX;
    for (unsigned i = 0; i < CONFIG_BT_MAX_CONN; ++i) {
        struct ll_conn *conn = ll_conn_get(i);
        if (param != conn) {
            continue;
        }
        slot = i;
        handle = conn->lll.handle;
        flags = 1u | ((conn == ull_update_mark_get()) << 1) |
                ((conn == ull_disable_mark_get()) << 2) |
                ((!!conn->periph.latency_cancel) << 3);
        break;
    }
    uint32_t op = (PTOB_LATENCY_CANCEL_FAULT_MARKER << 24) |
                  (slot << 20) | (flags << 16) | handle;
    /* memcpy preserves every raw status bit, including values above INT_MAX. */
    int32_t raw;
    memcpy(&raw, &status, sizeof raw);
    r2_health_operation(R2_HEALTH_PA, op, raw);
    irq_unlock(key);
}

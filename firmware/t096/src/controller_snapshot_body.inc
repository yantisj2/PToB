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

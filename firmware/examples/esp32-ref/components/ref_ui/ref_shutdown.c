// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ref_shutdown.h"

void ref_shutdown_request(RefShutdown *s, bool power_off, uint64_t now)
{
    if (!s->requested) {
        s->requested = true;
        if (now > UINT64_MAX - 2000) { s->fault = true; s->stage = REF_SHUTDOWN_FAULT; return; }
        s->deadline_ms = now + 2000;
        s->stage = REF_SHUTDOWN_DRAIN;
    }
    if (power_off) s->power_off = true;
}

RefShutdownStage ref_shutdown_poll(RefShutdown *s, uint64_t now, bool done,
                                    uint64_t completed, bool failed, bool released)
{
    if (!s->requested) return REF_SHUTDOWN_RUN;
    if (failed || (done && completed > s->deadline_ms) || (!done && now >= s->deadline_ms)) s->fault = true;
    if (s->fault) s->stage = REF_SHUTDOWN_FAULT;
    else if (!done) s->stage = REF_SHUTDOWN_DRAIN;
    else if (!s->power_off) s->stage = REF_SHUTDOWN_READY;
    else s->stage = released ? REF_SHUTDOWN_SLEEP : REF_SHUTDOWN_RELEASE;
    return s->stage;
}

void ref_shutdown_board_failed(RefShutdown *s)
{ s->fault = s->board_failure = true; s->stage = REF_SHUTDOWN_FAULT; }

const char *ref_shutdown_stage_name(RefShutdownStage stage)
{
    switch (stage) {
        case REF_SHUTDOWN_RUN: return "RUN";
        case REF_SHUTDOWN_DRAIN: return "DRAIN";
        case REF_SHUTDOWN_READY: return "MAINTENANCE_READY";
        case REF_SHUTDOWN_RELEASE: return "WAIT_RELEASE";
        case REF_SHUTDOWN_SLEEP: return "ENTER_SLEEP";
        default: return "FAULT";
    }
}

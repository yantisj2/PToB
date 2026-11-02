/* Copyright 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "role_announcement.h"
#include "rc_search.h"
#include "rc_stream_quality.h"
#include <string.h>
/* The native owner must finish its bounded search and longest parent trial.
 * This outer cap never extends a source/map/admission/output permission. */
#define MHAT_CONTEXT_RECOVERY_MS ((RC_SEARCH_LIMIT_US+RC_STREAM_TRIAL_US)/1000+30000)
#define MHAT_ROLE_ROWS 8u
struct mhat_role_row {
    struct r2_role_record report;
    uint64_t group, received_us;
};
struct mhat_role_monitor {
    struct mhat_role_row row[MHAT_ROLE_ROWS];
    uint32_t accepted, replayed, expired, capacity_refused;
};
/* Authenticated descriptive reports only; never an input to mesh selection,
 * PPS permission or timing. Keep per-incarnation replay fences across epochs. */
static inline bool mhat_role_observe(struct mhat_role_monitor *m,uint64_t group,
        const struct r2_role_record *r,uint64_t at,uint64_t now){
    if(!group||!r->root||!r->root_boot||!r->sequence||!r->valid_us||
       r->valid_us>R2_ROLE_MAX_VALID_US||now<at||now-at>=r->valid_us){
        ++m->expired;return false;
    }
    unsigned slot=MHAT_ROLE_ROWS;bool found=false;
    for(unsigned i=0;i<MHAT_ROLE_ROWS;i++){
        const struct mhat_role_row *p=&m->row[i];
        if(p->group==group&&p->report.root==r->root&&p->report.root_boot==r->root_boot){
            slot=i;found=true;break;
        }
        if(!p->group&&slot==MHAT_ROLE_ROWS)slot=i;
    }
    /* Never evict a replay fence. A full diagnostic cache refuses unseen
     * incarnations until reboot; normal timing admission remains independent. */
    if(!found&&slot==MHAT_ROLE_ROWS){++m->capacity_refused;return false;}
    struct mhat_role_row *p=&m->row[slot];
    if(found&&(r->sequence<=p->report.sequence||r->created_us<p->report.created_us)){
        ++m->replayed;return false;
    }
    *p=(struct mhat_role_row){.group=group,.received_us=at,.report=*r};
    ++m->accepted;return true;
}

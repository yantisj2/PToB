// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ref_join.h"

static const uint64_t AGE = REF_MS_TICKS(250);

bool ref_join_original(RefJoin *j, uint32_t pub, uint16_t event,
                       uint8_t channel, uint64_t tick, RefPrediction prediction)
{
    if (!pub || channel > 36) return false;
    RefOriginal *free_row = NULL;
    for (unsigned i = 0; i < 16; ++i) {
        RefOriginal *r = &j->rows[i];
        if (r->occupied && tick >= r->tick && tick - r->tick > AGE) r->occupied = false;
        if (r->occupied && r->publication == pub && r->event == event) { ++j->duplicate; return false; }
        if (!r->occupied && !free_row) free_row = r;
    }
    if (!free_row) { ++j->full; return false; }
    *free_row = (RefOriginal){tick, pub, event, channel, true, false, prediction};
    return true;
}

bool ref_join_correction(RefJoin *j, const RefCorrection *c, uint64_t now, RefOriginal *out)
{
    RefOriginal *selected = NULL;
    for (unsigned i = 0; i < 16; ++i) {
        RefOriginal *r = &j->rows[i];
        if (r->occupied && r->publication == c->publication && r->event == c->event &&
            r->channel == c->channel && now >= r->tick && now - r->tick <= AGE) {
            if (selected || r->consumed) { ++j->duplicate; return false; }
            selected = r;
        }
    }
    if (!selected) { ++j->missing; return false; }
    *out = *selected;
    selected->consumed = true; // Retain the tombstone until the age window ends.
    return true;
}

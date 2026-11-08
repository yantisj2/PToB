// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "screen.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    static const struct {
        const char *name;
        PtobRxState state;
        bool info, off, released, fault;
    } frames[] = {
        {"bootstrap", PTOB_BOOTSTRAPPING, false, false, false, false},
        {"search", PTOB_SEARCHING, false, false, false, false},
        {"sync", PTOB_SYNCING, false, false, false, false},
        {"acquire", PTOB_ACQUIRING, false, false, false, false},
        {"clock", PTOB_TIME_VALID, false, false, false, false},
        {"info", PTOB_TIME_VALID, true, false, false, false},
        {"lost", PTOB_TIME_LOST, true, false, false, false},
        {"off", PTOB_TIME_VALID, true, true, false, false},
        {"drain", PTOB_TIME_LOST, false, true, true, false},
        {"off-failed", PTOB_FAULT, false, true, true, true},
        {"stopped", PTOB_STOPPED, false, false, false, false},
        {"fault", PTOB_FAULT, false, false, false, false},
    };
    for (unsigned i = 0; i < sizeof frames / sizeof frames[0]; ++i) {
        const ReceiverScreen model = {.receiver = {.state = frames[i].state,
            .utc_ns = INT64_C(1794056712000000000), .rate_ppb = -1370, .pairs = 638,
            .misses = 4, .drops = 0, .posix_adjusting = true},
            .alias = "REF-02", .source = "T096 GM", .tier = 0,
            .info = frames[i].info, .power_off = frames[i].off,
            .released = frames[i].released, .power_fault = frames[i].fault};
        uint8_t fb[1024];
        receiver_screen(&model, fb);
        char path[1024];
        const int n = snprintf(path, sizeof path, "%s/%02u-%s.fb", argv[1], i, frames[i].name);
        if (n < 0 || (size_t)n >= sizeof path) return 1;
        FILE *file = fopen(path, "wb");
        if (!file) return 1;
        const size_t bytes = fwrite(fb, 1, sizeof fb, file);
        if (fclose(file) || bytes != sizeof fb) return 1;
    }
}

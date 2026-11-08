// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "screen.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    static const struct {
        const char *name;
        PtobRxState state;
        bool had_utc, info, off, released, fault;
    } frames[] = {
        {.name = "bootstrap", .state = PTOB_BOOTSTRAPPING},
        {.name = "waiting", .state = PTOB_SEARCHING},
        {.name = "sync", .state = PTOB_SYNCING},
        {.name = "acquire", .state = PTOB_ACQUIRING},
        {.name = "cold-retry", .state = PTOB_TIME_LOST},
        {.name = "clock", .state = PTOB_TIME_VALID, .had_utc = true},
        {.name = "info", .state = PTOB_TIME_VALID, .had_utc = true, .info = true},
        {.name = "lost", .state = PTOB_TIME_LOST, .had_utc = true, .info = true},
        {.name = "hot-retry", .state = PTOB_SEARCHING, .had_utc = true},
        {.name = "off", .state = PTOB_TIME_VALID, .off = true},
        {.name = "drain", .state = PTOB_TIME_LOST, .off = true, .released = true},
        {.name = "off-failed", .state = PTOB_FAULT, .off = true, .released = true, .fault = true},
        {.name = "stopped", .state = PTOB_STOPPED},
        {.name = "fault", .state = PTOB_FAULT},
    };
    for (unsigned i = 0; i < sizeof frames / sizeof frames[0]; ++i) {
        const ReceiverScreen model = {.receiver = {.state = frames[i].state,
            .utc_ns = INT64_C(1794056712000000000), .rate_ppb = -1370, .pairs = 638,
            .misses = 4, .drops = 0, .posix_adjusting = true},
            .alias = "REF-02", .source = "T096 GM", .tier = 0,
            .had_utc = frames[i].had_utc, .info = frames[i].info, .power_off = frames[i].off,
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

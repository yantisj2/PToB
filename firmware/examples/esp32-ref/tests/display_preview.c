// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ref_display.h"
#include <stdio.h>

typedef struct {
    const char *name, *state;
    bool available, leader, details, stopping, released, fault, edge_case;
} Screen;

static const Screen SCREENS[] = {
    {.name="reference", .state="REFERENCE", .available=true, .leader=true},
    {.name="search", .state="SEARCHING"},
    {.name="join", .state="AUTHENTICATE"},
    {.name="acquire", .state="ACQUIRING", .available=true},
    {.name="tracking", .state="TRACKING", .available=true},
    {.name="lock", .state="LOOP LOCK", .available=true},
    {.name="info", .state="LOOP LOCK", .available=true, .details=true},
    {.name="lost", .state="SIGNAL LOST", .details=true},
    {.name="off", .state="POWER OFF", .stopping=true},
    {.name="drain", .state="POWER OFF", .stopping=true, .released=true},
    {.name="off-failed", .state="OFF FAILED", .stopping=true, .fault=true},
    {.name="fault", .state="RADIO FAULT", .details=true, .fault=true},
    {.name="maintenance", .state="RADIO OFF"},
    {.name="reference-info", .state="REFERENCE", .available=true, .leader=true, .details=true},
    {.name="wide-info", .state="TRACKING", .available=true, .details=true, .edge_case=true},
    {.name="wide-clock", .state="TRACKING", .available=true, .edge_case=true},
};

int main(int argc, char **argv)
{
    if (argc != 2) return 1;
    for (unsigned i = 0; i < sizeof(SCREENS) / sizeof(SCREENS[0]); ++i) {
        const Screen *s = &SCREENS[i];
        RefDisplayModel m = {.available=s->available, .leader=s->leader, .details=s->details,
            .stopping=s->stopping, .released=s->released, .fault=s->fault,
            .admitted=s->available, .relative_ns=123456789000, .phase_ns=s->leader ? 0 : -230,
            .rate_ppb=3100, .pairs=600, .missing=11, .rms_ns=174, .rms_known=true};
        snprintf(m.state, sizeof(m.state), "%s", s->state);
        snprintf(m.device, sizeof(m.device), "%s", s->leader ? "C-3" : "C-4");
        snprintf(m.peer, sizeof(m.peer), "C-3");
        if (s->edge_case) {
            snprintf(m.device, sizeof(m.device), "BENCH_CLOCK_WITH_NAME");
            snprintf(m.peer, sizeof(m.peer), "LAB_PEER_WITH_LONG_NAME");
            m.relative_ns = UINT64_MAX;
            m.phase_ns = INT64_MIN;
            m.rate_ppb = INT32_MIN;
            m.pairs = UINT32_MAX;
            m.missing = 12345;
            m.rms_known = false;
        }
        uint8_t framebuffer[1024];
        ref_display_render(&m, framebuffer);
        char path[1024];
        const int length = snprintf(path, sizeof(path), "%s/display-%02u-%s.bin", argv[1], i, s->name);
        if (length < 0 || (size_t)length >= sizeof(path)) return 1;
        FILE *file = fopen(path, "wb");
        if (!file) return 1;
        const bool written = fwrite(framebuffer, 1, sizeof(framebuffer), file) == sizeof(framebuffer);
        if (fclose(file) || !written) return 1;
    }
    return 0;
}

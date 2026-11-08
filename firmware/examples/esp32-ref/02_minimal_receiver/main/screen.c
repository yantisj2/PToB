// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "screen.h"
#include "ref_display.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

enum { LEFT = 3, TITLE = 19, ROW_A = 36, ROW_B = 45, ACTIONS = 56 };

static void line(uint8_t *fb, unsigned y, const char *value)
{
    ref_fb_text(fb, LEFT, y, value, 1);
}

static void name(char out[8], const char *input)
{
    const size_t len = strlen(input);
    snprintf(out, 8, "%.7s", input);
    if (len > 7) out[6] = '>';
}

static const char *profile(unsigned tier)
{
    switch (tier) {
    case 1: return "REFINED";
    case 2: return "FINE DESC";
    default: return "BASIC";
    }
}

static void info(const ReceiverScreen *s, uint8_t *fb)
{
    char value[24];
    line(fb, 19, "PAIRS");
    snprintf(value, sizeof value, "%" PRIu32, s->receiver.pairs);
    ref_fb_right(fb, 19, value);
    line(fb, 28, "RATE");
    snprintf(value, sizeof value, "%+" PRId32 " PPB", s->receiver.rate_ppb);
    ref_fb_right(fb, 28, value);
    line(fb, 37, "MISS / DROP");
    uint32_t misses = s->receiver.misses, drops = s->receiver.drops;
    if (misses > 999 || drops > 999) snprintf(value, sizeof value, "SEE USB");
    else snprintf(value, sizeof value, "%" PRIu32 "/%" PRIu32, misses, drops);
    ref_fb_right(fb, 37, value);
    line(fb, 46, "RF ERROR UNKNOWN");
}

void receiver_screen(const ReceiverScreen *s, uint8_t fb[1024])
{
    memset(fb, 0, 1024);
    const bool valid = s->receiver.state == PTOB_TIME_VALID && !s->power_off;
    const bool details = valid && s->info;
    char alias[8];
    name(alias, s->alias);
    line(fb, 1, details ? "BLE RX INFO" : "BLE TIME RX");
    ref_fb_right(fb, 1, "W12");
    line(fb, 9, alias);
    ref_fb_right(fb, 9, details ? "TIME VALID" : profile(s->tier));

    if (s->power_off) {
        ref_fb_text(fb, LEFT, TITLE, s->power_fault ? "OFF FAILED" : "POWER OFF", 2);
        line(fb, ROW_A, s->power_fault ? "CHECK USB STATUS" :
            s->released ? "STOPPING RADIO" : "RELEASE THE BUTTON");
        line(fb, ROW_B, s->power_fault ? "RESET TO TRY AGAIN" : "DISPLAY WILL GO OFF");
        return;
    }
    if (details) {
        info(s, fb);
        line(fb, ACTIONS, "TAP TIME");
    } else if (valid) {
        const unsigned sec = (unsigned)((uint64_t)s->receiver.utc_ns / 1000000000 % 86400);
        char time[9], source[8];
        snprintf(time, sizeof time, "%02u:%02u:%02u", sec / 3600, sec / 60 % 60, sec % 60);
        ref_fb_text(fb, LEFT, TITLE, time, 2);
        ref_fb_right(fb, TITLE + 6, "UTC");
        name(source, s->source);
        line(fb, ROW_A, "SOURCE");
        ref_fb_right(fb, ROW_A, source);
        line(fb, ROW_B, "SYSTEM CLOCK");
        ref_fb_right(fb, ROW_B, s->receiver.posix_adjusting ? "BLE" : "APP");
        line(fb, ACTIONS, "TAP INFO");
    } else {
        const char *title = "FAULT", *a = "TIME UNAVAILABLE", *b = "CHECK USB STATUS";
        switch (s->receiver.state) {
        case PTOB_BOOTSTRAPPING: title = "JOINING"; a = "AUTHENTICATING GM"; b = "GETTING UTC"; break;
        case PTOB_SEARCHING: title = "SEARCHING"; a = "LOOKING FOR GM"; b = "PASSIVE BLE SCAN"; break;
        case PTOB_SYNCING: title = "SYNCING"; a = "FINDING BLE TRAIN"; b = "WAIT FOR PACKETS"; break;
        case PTOB_ACQUIRING: title = "ACQUIRING"; a = "MATCHING PACKETS"; b = "WAIT FOR FOLLOW-UP"; break;
        case PTOB_TIME_LOST: title = "TIME LOST"; b = "CHECK THE GM"; break;
        case PTOB_STOPPED: title = "RADIO OFF"; a = "RECEIVER STOPPED"; break;
        default: break;
        }
        ref_fb_text(fb, LEFT, TITLE, title, 2);
        line(fb, ROW_A, a);
        line(fb, ROW_B, b);
    }
    ref_fb_right(fb, ACTIONS, "HOLD 4S OFF");
}

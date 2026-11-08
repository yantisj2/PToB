// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ref_display.h"
#include "ref_timebase.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

// Independently drawn 5x7 uppercase teaching font. Each row is a five-bit mask,
// leftmost pixel in bit 4. Draw at integer scales on the OLED's native grid.
typedef struct { char letter; uint8_t rows[7]; } Glyph;
static const Glyph FONT[] = {
 {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
 {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
 {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
 {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
 {'I',{14,4,4,4,4,4,14}}, {'J',{7,2,2,2,18,18,12}},
 {'K',{17,18,20,24,20,18,17}}, {'L',{16,16,16,16,16,16,31}},
 {'M',{17,27,21,21,17,17,17}}, {'N',{17,25,25,21,19,19,17}},
 {'O',{14,17,17,17,17,17,14}}, {'P',{30,17,17,30,16,16,16}},
 {'Q',{14,17,17,17,21,18,13}}, {'R',{30,17,17,30,20,18,17}},
 {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
 {'U',{17,17,17,17,17,17,14}}, {'V',{17,17,17,17,17,10,4}},
 {'W',{17,17,17,21,21,27,17}}, {'X',{17,17,10,4,10,17,17}},
 {'Y',{17,17,10,4,4,4,4}}, {'Z',{31,1,2,4,8,16,31}},
 {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
 {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
 {'4',{2,6,10,18,31,2,2}}, {'5',{31,16,16,30,1,1,30}},
 {'6',{14,16,16,30,17,17,14}}, {'7',{31,1,2,4,8,8,8}},
 {'8',{14,17,17,14,17,17,14}}, {'9',{14,17,17,15,1,1,14}},
 {'-',{0,0,0,31,0,0,0}}, {'+',{0,4,4,31,4,4,0}},
 {'.',{0,0,0,0,0,6,6}}, {':',{0,6,6,0,6,6,0}},
 {'/',{1,1,2,4,8,16,16}}, {'>',{16,8,4,2,4,8,16}},
 {'_',{0,0,0,0,0,0,31}}, {' ',{0}}
};

enum { LEFT = 3, RIGHT = 125, STATE_ROW = 19, FIRST_ROW = 36,
       SECOND_ROW = 45, FOOTER_ROW = 56 };

static void pixel(uint8_t *fb, unsigned x, unsigned y)
{ if (x < 128 && y < 64) fb[(y / 8) * 128 + x] |= (uint8_t)(1u << (y % 8)); }

void ref_fb_text(uint8_t *fb, unsigned x, unsigned y, const char *s, unsigned scale)
{
    for (; *s && x + 5 * scale <= RIGHT; ++s, x += 6 * scale) {
        char letter = *s >= 'a' && *s <= 'z' ? (char)(*s - 'a' + 'A') : *s;
        const Glyph *glyph = &FONT[sizeof(FONT) / sizeof(FONT[0]) - 1];
        for (size_t i = 0; i < sizeof(FONT) / sizeof(FONT[0]); ++i)
            if (FONT[i].letter == letter) { glyph = &FONT[i]; break; }
        for (unsigned row = 0; row < 7; ++row) for (unsigned col = 0; col < 5; ++col)
            if (glyph->rows[row] & (1u << (4 - col)))
                for (unsigned dy = 0; dy < scale; ++dy) for (unsigned dx = 0; dx < scale; ++dx)
                    pixel(fb, x + col * scale + dx, y + row * scale + dy);
    }
}

void ref_fb_right(uint8_t *fb, unsigned y, const char *s)
{
    const size_t length = strlen(s);
    if (length && length <= 20) ref_fb_text(fb, RIGHT - (unsigned)(6 * length - 1), y, s, 1);
}

static void name_for_display(char *out, size_t size, const char *name)
{
    const size_t length = strlen(name);
    const size_t count = length < size ? length : size - 1;
    memcpy(out, name, count);
    if (length > count) out[count - 1] = '>';
    out[count] = '\0';
}

static void compact_count(char out[6], uint32_t count)
{
    const char *suffix = "";
    if (count >= 1000000000) { count /= 1000000000; suffix = "G"; }
    else if (count >= 1000000) { count /= 1000000; suffix = "M"; }
    else if (count >= 1000) { count /= 1000; suffix = "K"; }
    snprintf(out, 6, "%" PRIu32 "%s", count, suffix);
}

static const char *state_label(const char *state)
{
    if (!strcmp(state, "AUTHENTICATE")) return "JOINING";
    if (!strcmp(state, "SIGNAL LOST")) return "TIME LOST";
    if (!strcmp(state, "START FAILED")) return "START FAIL";
    if (!strcmp(state, "TIMING FAULT") || !strcmp(state, "RADIO FAULT")) return "FAULT";
    return state;
}

static const char *clock_rate(void)
{
    return REF_CLOCK_HZ == 80000000 ? "80 MHZ" : "20 MHZ";
}

static void detail_rows(uint8_t *fb, const RefDisplayModel *m)
{
    if (m->leader) {
        ref_fb_text(fb, LEFT, 19, "COUNTER", 1);
        ref_fb_right(fb, 19, clock_rate());
        ref_fb_text(fb, LEFT, 28, "ONE TICK", 1);
        ref_fb_right(fb, 28, REF_CLOCK_HZ == 80000000 ? "12.5 NS" : "50 NS");
        ref_fb_text(fb, LEFT, 37, "LEADS THE BLE TRAIN", 1);
        ref_fb_right(fb, 46, "RF UNKNOWN");
        return;
    }
    char value[24], pairs[6], missed[6], peer[6];
    ref_fb_text(fb, LEFT, 19, "RATE", 1);
    snprintf(value, sizeof(value), "%+.3f PPM", (double)m->rate_ppb / 1000);
    ref_fb_right(fb, 19, value);
    ref_fb_text(fb, LEFT, 28, "BLE RMS", 1);
    snprintf(value, sizeof(value), "%" PRIu32 " NS", m->rms_ns);
    ref_fb_right(fb, 28, m->rms_known ? value : "--");
    ref_fb_text(fb, LEFT, 37, "PAIR/MISS", 1);
    compact_count(pairs, m->pairs);
    compact_count(missed, m->missing);
    snprintf(value, sizeof(value), "%s/%s", pairs, missed);
    ref_fb_right(fb, 37, value);
    name_for_display(peer, sizeof(peer), m->peer[0] ? m->peer : "--");
    snprintf(value, sizeof(value), "PEER %s", peer);
    ref_fb_text(fb, LEFT, 46, value, 1);
    ref_fb_right(fb, 46, "RF UNKNOWN");
}

static void clock_rows(uint8_t *fb, const RefDisplayModel *m)
{
    char value[24];
    const uint64_t seconds = m->relative_ns / UINT64_C(1000000000);
    if (seconds >= 100000) snprintf(value, sizeof(value), "%" PRIu64 " H", seconds / 3600);
    else snprintf(value, sizeof(value), "%" PRIu64 ".%03" PRIu64 " S", seconds,
                  (m->relative_ns / UINT64_C(1000000)) % 1000);
    ref_fb_text(fb, LEFT, FIRST_ROW, "REL TIME", 1);
    ref_fb_right(fb, FIRST_ROW, value);
    if (m->leader) {
        ref_fb_text(fb, LEFT, SECOND_ROW, "LOCAL FREE RUN", 1);
    } else {
        ref_fb_text(fb, LEFT, SECOND_ROW, "PHASE LEFT", 1);
        if (m->phase_ns >= -999999 && m->phase_ns <= 999999) {
            snprintf(value, sizeof(value), "%+" PRId64 " NS", m->phase_ns);
            ref_fb_right(fb, SECOND_ROW, value);
        } else ref_fb_right(fb, SECOND_ROW, "SEE USB");
    }
}

void ref_display_render(const RefDisplayModel *m, uint8_t fb[1024])
{
    memset(fb, 0, 1024);
    const bool details = m->details && m->available && !m->fault && !m->stopping;
    const char *state = state_label(m->state);
    char alias[8];
    name_for_display(alias, sizeof(alias), m->device);
    ref_fb_text(fb, LEFT, 1, details ? "SOFTPLL INFO" : "SOFTPLL", 1);
    ref_fb_right(fb, 1, m->leader ? "LEADER" : "FOLLOWER");
    ref_fb_text(fb, LEFT, 9, alias, 1);
    ref_fb_right(fb, 9, details ? state : clock_rate());

    if (m->stopping) {
        ref_fb_text(fb, LEFT, STATE_ROW, m->fault ? "OFF FAILED" : "POWER OFF", 2);
        const char *action = m->fault ? "CHECK USB STATUS" :
            m->released ? "STOPPING RADIO" : "RELEASE THE BUTTON";
        ref_fb_text(fb, LEFT, FIRST_ROW, action, 1);
        ref_fb_text(fb, LEFT, SECOND_ROW, m->fault ? "RESET TO TRY AGAIN" : "DISPLAY WILL GO OFF", 1);
        return;
    }
    if (details) {
        detail_rows(fb, m);
        ref_fb_text(fb, LEFT, FOOTER_ROW, "TAP CLOCK", 1);
    } else {
        ref_fb_text(fb, LEFT, STATE_ROW, state, 2);
        if (m->available && !m->fault) {
            clock_rows(fb, m);
            ref_fb_text(fb, LEFT, FOOTER_ROW, "TAP INFO", 1);
        } else if (m->fault || !strcmp(state, "RADIO OFF")) {
            ref_fb_text(fb, LEFT, FIRST_ROW, m->fault ? "TIME UNAVAILABLE" : "READY FOR UPDATE", 1);
            ref_fb_text(fb, LEFT, SECOND_ROW, "CHECK USB STATUS", 1);
        } else if (!strcmp(state, "JOINING")) {
            ref_fb_text(fb, LEFT, FIRST_ROW, "CHECKING BLE KEY", 1);
            ref_fb_text(fb, LEFT, SECOND_ROW, "WAIT FOR SESSION", 1);
        } else if (!strcmp(state, "TIME LOST")) {
            ref_fb_text(fb, LEFT, FIRST_ROW, "TIME UNAVAILABLE", 1);
            ref_fb_text(fb, LEFT, SECOND_ROW, "CHECK THE LEADER", 1);
        } else if (m->admitted) {
            ref_fb_text(fb, LEFT, FIRST_ROW, "WAITING FOR PAIRS", 1);
        } else {
            ref_fb_text(fb, LEFT, FIRST_ROW, "LOOKING FOR LEADER", 1);
            ref_fb_text(fb, LEFT, SECOND_ROW, "CHECK LEADER POWER", 1);
        }
    }
    ref_fb_right(fb, FOOTER_ROW, "HOLD 4S OFF");
}

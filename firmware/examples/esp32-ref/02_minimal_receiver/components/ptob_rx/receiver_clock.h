// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef PTOB_RX_RECEIVER_CLOCK_H
#define PTOB_RX_RECEIVER_CLOCK_H
#include "direct_wire.h"
#include "ref_clock.h"

typedef struct {
    uint64_t root, boot, utc_ns, at_us, session_until_us;
    uint32_t epoch, train, uncertainty_us;
} PtobAdmission;

typedef enum { PTOB_RX_BASIC, PTOB_RX_REFINED, PTOB_RX_FINE } PtobTier;
typedef struct { uint64_t tick; uint16_t event; uint8_t channel; } PtobCapture;
typedef struct {
    PtobCapture capture;
    uint64_t created_ns, expires_ns, utc_offset_ns;
    uint32_t publication;
} PtobOriginal;
typedef struct { uint64_t tick, utc_ns; } PtobPoint;
enum { PTOB_FINE_FIT_POINTS = 32 };
typedef struct {
    RefClock clock;
    PtobAdmission admission;
    PtobOriginal originals[5]; // Five slots cover the 250 ms pair deadline at 16 Hz.
    PtobPoint points[PTOB_FINE_FIT_POINTS];
    uint64_t last_tick, last_reference_ns, coarse_ns, coarse_at_us, permission_us;
    uint32_t capture_epoch, pairs, missed;
    uint8_t original_next, point_next, point_count;
    PtobTier tier;
} PtobReceiverClock;

void ptob_clock_begin(PtobReceiverClock *rx, PtobTier tier, const PtobAdmission *admission);
void ptob_clock_refresh(PtobReceiverClock *rx, const PtobAdmission *admission);
bool ptob_clock_expire(PtobReceiverClock *rx, uint64_t now_us);
bool ptob_clock_packet(PtobReceiverClock *rx, const PtobFrame *frame, PtobCapture capture,
                       uint64_t now_tick, uint64_t now_us);
bool ptob_clock_output(const PtobReceiverClock *rx, uint64_t tick, uint64_t phase_source_id,
                       int32_t phase_adjust_ns, RefClock *out, RefClockReading *reading);
#endif

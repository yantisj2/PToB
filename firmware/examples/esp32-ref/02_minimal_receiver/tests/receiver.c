// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "receiver_clock.h"
#include "ref_timebase.h"
#include "fixture.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

bool ptob_rx_hmac(const uint8_t key[32], const uint8_t *data, size_t len, uint8_t mac[32])
{
    if (memcmp(key, TEST_KEY, sizeof TEST_KEY)) return false;
    for (unsigned i = 0; i < sizeof MACS / sizeof MACS[0]; ++i) {
        if (len == MACS[i].len && !memcmp(data, MACS[i].input, len)) {
            memcpy(mac, MACS[i].mac, 32);
            return true;
        }
    }
    return false; // Reject unknown input, including mutations of a signed record.
}

static void wire(void)
{
    PtobTrust trust = {.epoch = TEST_EPOCH};
    memcpy(trust.psk, TEST_KEY, sizeof trust.psk);
    PtobLocator gm;
    assert(ptob_locator(&trust, LOCATOR, sizeof LOCATOR, ADDRESS, &gm));
    assert(gm.mesh == TEST_MESH && gm.root == TEST_ROOT);
    uint8_t other[sizeof ADDRESS];
    memcpy(other, ADDRESS, sizeof other);
    other[0] ^= 1;
    assert(!ptob_locator(&trust, LOCATOR, sizeof LOCATOR, other, &gm));
    assert(!ptob_locator(&trust, LOCATOR, sizeof LOCATOR - 1, ADDRESS, &gm));
    PtobFrame frame;
    assert(ptob_gr3_read(&trust, &gm, FRAME, sizeof FRAME, &frame));
    assert(frame.boot == 9 && frame.epoch == 1 && frame.train == 3 && frame.capture_epoch == 2);
    assert(frame.publication == 64 && frame.count == 1);
    assert(frame.correction[0].publication == 63 && frame.correction[0].event == UINT16_MAX);
    uint8_t corrupt[sizeof FRAME];
    memcpy(corrupt, FRAME, sizeof corrupt);
    corrupt[145] ^= 1;
    assert(!ptob_gr3_read(&trust, &gm, corrupt, sizeof corrupt, &frame));
    assert(!ptob_gr3_read(&trust, &gm, FRAME, sizeof FRAME - 1, &frame));
}

static void join_wire(void)
{
    PtobTrust trust = {.epoch = TEST_EPOCH};
    memcpy(trust.psk, TEST_KEY, sizeof trust.psk);
    const PtobLocator gm = {.mesh = TEST_MESH, .root = TEST_ROOT};
    PtobJoin exchange = {.client = 19, .boot = 21, .nonce = 33};
    PtobGmAnchor anchor;
    uint8_t sent[PTOB_JOIN_BYTES];
    assert(!ptob_join_challenge(&trust, &gm, &exchange, CHALLENGE));
    assert(ptob_join_send(&trust, &gm, &exchange, 3000000, sent));
    assert(!memcmp(sent, REQUEST, sizeof sent));
    assert(ptob_join_challenge(&trust, &gm, &exchange, CHALLENGE));
    assert(!ptob_join_challenge(&trust, &gm, &exchange, CHALLENGE));
    assert(!ptob_join_accept(&trust, &gm, &exchange, ACCEPT, 3100000, &anchor));
    assert(ptob_join_send(&trust, &gm, &exchange, 3050000, sent));
    assert(!memcmp(sent, PROOF, sizeof sent));
    assert(!ptob_join_accept(&trust, &gm, &exchange, OLD_ACCEPT, 3100000, &anchor));
    assert(!ptob_join_accept(&trust, &gm, &exchange, EXPIRING_ACCEPT, 3100000, &anchor));
    assert(!ptob_join_accept(&trust, &gm, &exchange, ACCEPT, 3300000, &anchor));
    assert(ptob_join_accept(&trust, &gm, &exchange, ACCEPT, 3100000, &anchor));
    assert(anchor.boot == 9 && anchor.epoch == 1 && anchor.train == 3);
    assert(anchor.utc_ns == UINT64_C(1794000000025000000) && anchor.uncertainty_us == 65164);
    assert(anchor.bootstrap_until_us == 4209836);
}

static void freshness(void)
{
    const uint64_t offset = UINT64_C(1794000000000000000);
    const PtobAdmission admission = {.root = 17, .boot = 9, .utc_ns = offset + 1000000000,
        .at_us = 3000000, .session_until_us = 8000000,
        .epoch = 1, .train = 3, .uncertainty_us = 50000};
    PtobFrame frame = {.created_ns = 1100000000, .expires_ns = 2500000000,
        .utc_offset_ns = offset, .boot = 9, .epoch = 1, .train = 3};
    const PtobCapture capture = {REF_MS_TICKS(3000), 1, 17};
    PtobReceiverClock clock;
    ptob_clock_begin(&clock, PTOB_RX_FINE, &admission);
    assert(!ptob_clock_packet(&clock, &frame, capture, capture.tick, 3000000)); // Future.
    assert(ptob_clock_packet(&clock, &frame, capture, capture.tick, 3200000));
    assert(!ptob_clock_expire(&clock, 4100000)); // An unpaired report grants no permission.

    // A delayed estimate cannot extend expiry past the independent JOIN bound.
    clock.last_reference_ns = 1200000000;
    clock.coarse_at_us = 4200000;
    clock.coarse_ns = offset + 1900000000;
    assert(ptob_clock_packet(&clock, &frame, capture, capture.tick, 4200000));
    assert(!ptob_clock_packet(&clock, &frame, capture, capture.tick, 4500000));
    assert(!ptob_clock_packet(&clock, &frame, capture, capture.tick, admission.session_until_us));
}

static void train(PtobTier tier, uint64_t acquire_delay_us)
{
    const uint64_t utc_offset = UINT64_C(1794000000000000000);
    const uint64_t relative = UINT64_C(1000000000);
    const uint64_t at = 5000000;
    const PtobAdmission admission = {.root = 8, .boot = 9, .utc_ns = utc_offset + relative,
        .at_us = at, .session_until_us = at + 4000000,
        .epoch = 1, .train = 3, .uncertainty_us = 30000};
    PtobReceiverClock clock;
    ptob_clock_begin(&clock, tier, &admission);

    for (unsigned i = 0; i < 32; ++i) {
        const uint64_t local = REF_MS_TICKS(5000) + i * UINT64_C(62500) * REF_TICKS_PER_US;
        const uint64_t remote = relative + acquire_delay_us * 1000 + i * UINT64_C(62500000);
        const uint16_t event = (uint16_t)(UINT16_MAX + i);
        PtobFrame frame = {.created_ns = remote > 100000000 ? remote - 100000000 : 1,
            .expires_ns = remote + 1300000000, .utc_offset_ns = utc_offset,
            .boot = 9, .epoch = 1, .train = 3, .publication = i + 10, .capture_epoch = 2};
        if (i) {
            frame.count = 1;
            frame.correction[0] = (PtobCorrection){.publication = i + 9,
                .event = (uint16_t)(event - 1), .channel = 17,
                .ticks = (remote - 62500000) * 2 / 125};
        }
        // One channel mismatch must withhold that update even with good MACs.
        if (i == 3) frame.correction[0].channel = 18;
        const uint64_t available = local + REF_MS_TICKS(2);
        const uint64_t now = at + acquire_delay_us + i * UINT64_C(62500) + 2000;
        const PtobCapture capture = {local, event, 17};
        assert(ptob_clock_packet(&clock, &frame, capture, available, now));
        assert(clock.pairs == (i < 3 ? i : i - 1));
        if (i) {
            const uint32_t pairs = clock.pairs;
            const uint64_t expiry = clock.permission_us;
            assert(!ptob_clock_packet(&clock, &frame, capture, available, expiry));
            frame.capture_epoch = 3;
            assert(!ptob_clock_packet(&clock, &frame, capture, available, now));
            assert(clock.pairs == pairs && clock.permission_us == expiry);
        }
    }
    RefClockReading read;
    assert(ref_clock_read(&clock.clock, clock.clock.anchor_tick, &read));
    assert(read.time_ns > (int64_t)utc_offset);
    assert(!ref_clock_read(&clock.clock, clock.clock.valid_until_tick, &read));
    const int64_t raw = read.time_ns;
    const uint64_t original_permission = clock.permission_us;
    const uint64_t original_reference = clock.last_reference_ns;
    PtobAdmission refresh = admission;
    refresh.at_us += 2000000;
    refresh.utc_ns += 2000000000;
    refresh.session_until_us += 2000000;
    ptob_clock_refresh(&clock, &refresh);
    assert(clock.admission.at_us == refresh.at_us);
    assert(clock.permission_us == original_permission && clock.last_reference_ns == original_reference);
    RefClock output;
    assert(ptob_clock_output(&clock, clock.clock.anchor_tick, admission.root + 1, 2500, &output, &read));
    assert(read.time_ns == raw);
    assert(ptob_clock_output(&clock, clock.clock.anchor_tick, admission.root, 2500, &output, &read));
    assert(read.time_ns == raw + 2500 && output.valid_until_tick == clock.clock.valid_until_tick);
    assert(ptob_clock_output(&clock, clock.clock.anchor_tick, admission.root, -2500, &output, &read));
    assert(read.time_ns == raw - 2500);
    assert(!ptob_clock_output(&clock, clock.clock.valid_until_tick, admission.root, 2500, &output, &read));
}

static void fine_recovery(void)
{
    const uint64_t offset = UINT64_C(1794000000000000000);
    const uint64_t at = 5000000;
    const PtobAdmission admission = {.root = 8, .boot = 9, .utc_ns = offset + 1000000000,
        .at_us = at, .session_until_us = at + 10000000,
        .epoch = 1, .train = 3, .uncertainty_us = 30000};
    PtobReceiverClock clock;
    ptob_clock_begin(&clock, PTOB_RX_FINE, &admission);
    const uint32_t gaps_ms[] = {0, 63, 438, 501, 1600, 1663, 2038, 2101, 7100, 7163};
    for (unsigned i = 0; i < sizeof gaps_ms / sizeof gaps_ms[0]; ++i) {
        const uint64_t local = REF_MS_TICKS(5000 + gaps_ms[i]);
        const uint64_t remote = UINT64_C(1000000000) + gaps_ms[i] * UINT64_C(1000000);
        const uint64_t now = at + gaps_ms[i] * UINT64_C(1000) + 2000;
        PtobFrame frame = {.created_ns = remote - 100000000, .expires_ns = remote + 900000000,
            .utc_offset_ns = offset, .boot = 9, .epoch = 1, .train = 3,
            .publication = i + 10, .capture_epoch = 2};
        if (i) {
            const uint64_t prior = UINT64_C(1000000000) + gaps_ms[i - 1] * UINT64_C(1000000);
            frame.count = 1;
            frame.correction[0] = (PtobCorrection){.publication = i + 9, .event = i - 1,
                .channel = 17, .ticks = prior * 2 / 125};
        }
        if (i == 4 || i == 8) {
            const uint64_t last = clock.last_reference_ns;
            assert(ptob_clock_expire(&clock, now));
            RefClockReading read;
            RefClock output;
            assert(!ptob_clock_output(&clock, local, admission.root, 2500, &output, &read));
            assert(!clock.pairs && !clock.permission_us && clock.last_reference_ns == last);
        }
        assert(ptob_clock_packet(&clock, &frame, (PtobCapture){local, i, 17}, local + REF_MS_TICKS(2), now));
        if (i == 3) assert(clock.pairs == 2 && clock.point_count == 2);
        if (i == 4 || i == 8) assert(!clock.pairs && !clock.permission_us);
        if (i == 7) assert(clock.pairs == 2 && clock.point_count == 4);
        if (i == 9) {
            RefClockReading read;
            RefClock output;
            assert(clock.pairs == 1 && clock.point_count == 1);
            assert(!ptob_clock_output(&clock, local + REF_MS_TICKS(2), admission.root, 2500, &output, &read));
        }
    }
}

int main(void)
{
    wire();
    join_wire();
    freshness();
    train(PTOB_RX_BASIC, 0);
    train(PTOB_RX_REFINED, 0);
    // First capture lands after any original JOIN grant could still be live.
    train(PTOB_RX_FINE, 1800000);
    fine_recovery();
    puts("PASS JOIN5 freshness, GR3 authentication and matching, Fine recovery, and scoped calibration");
}

// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "radio_math.h"
#include "ref_timebase.h"
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#define REF_IRAM IRAM_ATTR
#else
#define REF_IRAM
#endif

bool REF_IRAM ref_capture32_advance(RefCaptureCounter *counter, uint32_t raw, uint64_t now_us,
                                   uint64_t *tick)
{
    if (counter->failed) return false;
    if (counter->started) {
        const uint32_t delta = raw - (uint32_t)counter->tick;
        // At 80 MHz the half-wrap time is 26.8435456 seconds.
        if (now_us < counter->last_read_us || now_us - counter->last_read_us > 26843545 ||
            delta >= UINT32_C(0x80000000) || counter->tick > UINT64_MAX - delta) {
            counter->failed = true;
            return false;
        }
        counter->tick += delta;
    } else {
        counter->tick = raw;
        counter->started = true;
    }
    counter->last_read_us = now_us;
    *tick = counter->tick;
    return true;
}

bool ref_counter_health(RefCounterHealth *h, uint64_t tick, uint64_t coarse)
{
    if (h->failed) return false;
    if (h->started) {
        if (tick < h->tick || coarse < h->coarse_us) { h->failed = true; return false; }
        const uint64_t elapsed = coarse - h->coarse_us;
        if (elapsed < 1000) return true;
        const uint64_t advance = tick - h->tick;
        // Loose liveness/rate bounds contain a stopped or wrongly configured
        // timer. They do not estimate oscillator frequency or certify accuracy.
        const uint64_t margin = 100 * REF_TICKS_PER_US;
        if (elapsed > 1000000 || advance > elapsed * (REF_TICKS_PER_US * 11 / 10) + margin ||
            advance + margin < elapsed * (REF_TICKS_PER_US * 9 / 10)) {
            h->failed = true; return false;
        }
    }
    h->started = true; h->tick = tick; h->coarse_us = coarse;
    return true;
}

// Pinned S3 BLE clock: 28-bit half-slots, each 312.5 us. The fine clock uses
// 500 ns units. This conversion changes coordinates, not physical accuracy.
static bool REF_IRAM bridge_ok(const RefRadioBridge *b)
{ return b->before && b->after >= b->before && b->after - b->before <= 20 * REF_TICKS_PER_US &&
         b->slots <= 0x0fffffff && b->half_us <= 624; }

static int64_t REF_IRAM slot_delta(uint32_t a, uint32_t b)
{
    const uint32_t d = (a - b) & UINT32_C(0x0fffffff);
    return d & UINT32_C(0x08000000) ? (int64_t)d - INT64_C(0x10000000) : d;
}

static bool REF_IRAM translate(uint64_t base, int64_t delta, uint64_t *out)
{
    if ((delta < 0 && base <= (uint64_t)-delta) ||
        (delta >= 0 && base > UINT64_MAX - (uint64_t)delta)) return false;
    *out = delta < 0 ? base - (uint64_t)-delta : base + (uint64_t)delta;
    return true;
}

bool REF_IRAM ref_radio_rx_tick(const RefRadioBridge *b, uint32_t coarse, uint16_t fine,
                        unsigned extra_us, uint64_t *tick)
{
    if (!bridge_ok(b) || coarse > 0x0fffffff || (fine & 1023) > 624 || extra_us > 127) return false;
    const int64_t d = slot_delta(coarse, b->slots) * 625 + 624 - (fine & 1023) - b->half_us;
    if (d < -500000 || d > 500000) return false;
    const int64_t delta = d * (REF_TICKS_PER_US / 2) - (int64_t)extra_us * REF_TICKS_PER_US;
    return translate(b->before + (b->after - b->before) / 2, delta, tick);
}

bool REF_IRAM ref_radio_tx_tick(const RefRadioBridge *b, uint32_t cookie,
                        uint64_t available, uint64_t *tick)
{
    if (!bridge_ok(b) || cookie > 0x0fffffff) return false;
    const int64_t slots = slot_delta(cookie, b->slots);
    if (slots < -1600 || slots > 1600) return false;
    // The cookie denotes programmed packet START. LE1M nominal ADDRESS follows
    // its eight preamble and 32 access-address bits by 40 us.
    const int64_t half_us = slots * 625 - b->half_us + 80;
    const int64_t delta = half_us * (REF_TICKS_PER_US / 2);
    uint64_t value;
    if (!translate(b->before + (b->after - b->before) / 2, delta, &value) ||
        value > available || available - value > REF_MS_TICKS(250)) return false;
    *tick = value;
    return true;
}

static uint16_t REF_IRAM permute(uint16_t value)
{
    value = (uint16_t)(((value & 0xaaaa) >> 1) | ((value & 0x5555) << 1));
    value = (uint16_t)(((value & 0xcccc) >> 2) | ((value & 0x3333) << 2));
    return (uint16_t)(((value & 0xf0f0) >> 4) | ((value & 0x0f0f) << 4));
}

bool REF_IRAM ref_radio_channel(uint32_t access_address, const uint16_t map[3],
                        uint16_t event, uint8_t *channel)
{
    if (!access_address || (map[2] & 0xffe0)) return false;
    unsigned count = 0;
    for (unsigned c = 0; c < 37; ++c) if (map[c / 16] & (1u << (c % 16))) ++count;
    if (count < 2) return false;
    const uint16_t id = (uint16_t)(access_address ^ (access_address >> 16));
    uint16_t prn = (uint16_t)(event ^ id);
    for (unsigned i = 0; i < 3; ++i) prn = (uint16_t)(17u * permute(prn) + id);
    prn ^= id;
    unsigned selected = prn % 37;
    if (!(map[selected / 16] & (1u << (selected % 16)))) {
        unsigned rank = ((uint32_t)count * prn) >> 16;
        for (selected = 0; selected < 37; ++selected)
            if (map[selected / 16] & (1u << (selected % 16))) { if (rank == 0) break; --rank; }
    }
    *channel = (uint8_t)selected;
    return true;
}

// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "receiver_clock.h"
#include "ref_timebase.h"

enum { FINE_FIT_MS = 4000, SHORT_FIT_GAP_MS = 250, PAIR_MAX_AGE_MS = 250 };

static uint64_t joined_utc_upper(const PtobAdmission *admission, uint64_t now_us)
{
    const uint64_t elapsed = now_us - admission->at_us;
    // Keep the causal JOIN sample independent of the moving pair estimate.
    // Local elapsed time is enclosed with a conservative 2,000 ppm allowance.
    return admission->utc_ns + (elapsed + (elapsed + 499) / 500 + admission->uncertainty_us) * 1000;
}

void ptob_clock_begin(PtobReceiverClock *rx, PtobTier tier, const PtobAdmission *admission)
{
    // JOIN gives us a date and replay scope. Only a fresh GR3 pair grants
    // clock permission, using the original and follow-up's signed expiry.
    *rx = (PtobReceiverClock){.admission = *admission, .coarse_ns = admission->utc_ns,
        .coarse_at_us = admission->at_us, .tier = tier};
}

void ptob_clock_refresh(PtobReceiverClock *rx, const PtobAdmission *admission)
{
    // Caller has authenticated the same root, boot, epoch and train. Update
    // the replay bound for future packets. Leave the clock and its expiry.
    rx->admission = *admission;
}

bool ptob_clock_expire(PtobReceiverClock *rx, uint64_t now_us)
{
    if (!rx->permission_us || now_us < rx->permission_us) return false;
    // A fresh pair must earn a new deadline. Keep the original identities,
    // replay watermark and fit history while the authenticated train stays open.
    rx->permission_us = 0;
    rx->clock = (RefClock){0};
    rx->pairs = 0;
    return true;
}

static uint64_t distance(uint64_t a, uint64_t b) { return a > b ? a - b : b - a; }

static unsigned fit_capacity(PtobTier tier)
{
    if (tier == PTOB_RX_BASIC) return 2;
    return tier == PTOB_RX_FINE ? PTOB_FINE_FIT_POINTS : 12;
}

static uint64_t fit_limit_ns(PtobTier tier)
{
    return tier == PTOB_RX_FINE ? FINE_FIT_MS * UINT64_C(1000000) : UINT64_C(1500000000);
}

static unsigned oldest_point(const PtobReceiverClock *rx, unsigned capacity)
{
    return (rx->point_next + capacity - rx->point_count) % capacity;
}

static int32_t estimate_rate(const PtobReceiverClock *rx, PtobPoint newest, unsigned capacity)
{
    if (!rx->point_count) return 0;
    const PtobPoint base = rx->points[oldest_point(rx, capacity)];
    uint64_t nominal;
    if (!ref_ticks_to_ns(newest.tick - base.tick, REF_CLOCK_HZ, &nominal) ||
        nominal < 50000000 || nominal > fit_limit_ns(rx->tier)) return 0;
    const uint64_t remote = newest.utc_ns - base.utc_ns;
    if (distance(remote, nominal) > 1000000) return 0;
    const int64_t rate = ((int64_t)remote - (int64_t)nominal) * 1000000000 / (int64_t)nominal;
    if (rate > REF_CLOCK_MAX_RATE_PPB) return REF_CLOCK_MAX_RATE_PPB;
    if (rate < -REF_CLOCK_MAX_RATE_PPB) return -REF_CLOCK_MAX_RATE_PPB;
    return (int32_t)rate;
}

static uint64_t projected(PtobPoint point, uint64_t now, int32_t rate, uint64_t limit_ns)
{
    // Calls only project admitted points within the short fitting window.
    uint64_t ns;
    if (now < point.tick || !ref_ticks_to_ns(now - point.tick, REF_CLOCK_HZ, &ns) ||
        ns > limit_ns) return 0;
    const uint64_t adjustment = ns * (uint64_t)(rate < 0 ? -rate : rate) / 1000000000;
    return rate < 0 ? point.utc_ns + ns - adjustment : point.utc_ns + ns + adjustment;
}

static bool steer(PtobReceiverClock *rx, PtobPoint newest, uint64_t now_tick, uint64_t until_tick)
{
    const unsigned capacity = fit_capacity(rx->tier);
    const uint64_t limit_ns = fit_limit_ns(rx->tier);
    if (rx->tier == PTOB_RX_FINE) {
        // Sparse but valid anchor pairs can still constrain the oscillator rate.
        while (rx->point_count && newest.tick - rx->points[oldest_point(rx, capacity)].tick > REF_MS_TICKS(FINE_FIT_MS))
            --rx->point_count;
    } else if (rx->point_count && newest.tick - rx->last_tick > REF_MS_TICKS(SHORT_FIT_GAP_MS)) {
        rx->point_count = rx->point_next = 0;
    }

    const int32_t rate = estimate_rate(rx, newest, capacity);
    const uint64_t center = projected(newest, now_tick, rate, limit_ns);
    if (!center) return false;
    int64_t sum = 0;
    unsigned count = 1;
    if (rx->tier != PTOB_RX_BASIC) {
        for (unsigned i = 0; i < rx->point_count; ++i) {
            const unsigned slot = (oldest_point(rx, capacity) + i) % capacity;
            const uint64_t value = projected(rx->points[slot], now_tick, rate, limit_ns);
            if (value && distance(value, center) <= 100000) {
                sum += (int64_t)value - (int64_t)center;
                ++count;
            }
        }
    }
    const int64_t target = (int64_t)center + sum / count;
    RefClockReading previous;
    if (ref_clock_read(&rx->clock, now_tick, &previous)) {
        const int64_t phase = target - previous.time_ns;
        if (phase < -100000 || phase > 100000 ||
            !ref_clock_adjust(&rx->clock, now_tick, rate, phase, REF_CLOCK_MAX_SLEW_PPB))
            return false;
        rx->clock.valid_until_tick = until_tick;
    } else if (!ref_clock_begin(&rx->clock, REF_CLOCK_HZ, now_tick, target, until_tick) ||
               !ref_clock_adjust(&rx->clock, now_tick, rate, 0, 0)) return false;
    rx->points[rx->point_next] = newest;
    rx->point_next = (rx->point_next + 1) % capacity;
    if (rx->point_count < capacity) ++rx->point_count;
    return true;
}

static bool match(PtobReceiverClock *rx, const PtobFrame *frame, const PtobCorrection *c,
                  uint64_t now_tick, uint64_t now_us, uint64_t coarse_ns, uint64_t guard_ns)
{
    PtobOriginal *original = NULL;
    for (unsigned i = 0; i < 5; ++i) {
        PtobOriginal *o = &rx->originals[i];
        if (o->publication == c->publication && o->capture.event == c->event &&
            o->capture.channel == c->channel && o->capture.tick > rx->last_tick &&
            o->capture.tick <= now_tick && now_tick - o->capture.tick < REF_MS_TICKS(PAIR_MAX_AGE_MS)) {
            if (original) return false; // An ambiguous physical identity cannot train.
            original = o;
        }
    }
    if (!original || original->utc_offset_ns != frame->utc_offset_ns) return false;

    // The GM uses a 16 MHz ADDRESS latch. Convert 62.5 ns ticks once.
    const uint64_t reference = c->ticks * 125 / 2;
    const uint64_t expires = original->expires_ns < frame->expires_ns ?
        original->expires_ns : frame->expires_ns;
    if (reference <= rx->last_reference_ns || reference < original->created_ns ||
        reference >= expires) return false;
    const PtobPoint point = {original->capture.tick, reference + original->utc_offset_ns};
    uint64_t age;
    if (!ref_ticks_to_ns(now_tick - point.tick, REF_CLOCK_HZ, &age) ||
        distance(coarse_ns, point.utc_ns + age) > guard_ns ||
        expires - reference <= age + guard_ns) return false;
    const uint64_t upper = joined_utc_upper(&rx->admission, now_us);
    const uint64_t utc_expiry = expires + original->utc_offset_ns;
    if (utc_expiry <= upper) return false;
    uint64_t life_us = (expires - reference - age - guard_ns) / 1000;
    const uint64_t joined_life_us = (utc_expiry - upper) / 1002;
    if (life_us > joined_life_us) life_us = joined_life_us;
    if (life_us > rx->admission.session_until_us - now_us)
        life_us = rx->admission.session_until_us - now_us;
    if (!life_us || !steer(rx, point, now_tick, now_tick + life_us * REF_TICKS_PER_US)) return false;

    rx->last_tick = point.tick;
    rx->last_reference_ns = reference;
    original->publication = 0;
    rx->coarse_at_us = now_us;
    rx->coarse_ns = point.utc_ns + age;
    rx->permission_us = now_us + life_us;
    ++rx->pairs;
    return true;
}

bool ptob_clock_packet(PtobReceiverClock *rx, const PtobFrame *frame, PtobCapture capture,
                       uint64_t now_tick, uint64_t now_us)
{
    if (now_us < rx->admission.at_us || now_us < rx->coarse_at_us ||
        (rx->permission_us && now_us >= rx->permission_us) ||
        now_us >= rx->admission.session_until_us ||
        frame->boot != rx->admission.boot || frame->epoch != rx->admission.epoch ||
        frame->train != rx->admission.train ||
        capture.tick > now_tick || now_tick - capture.tick >= REF_MS_TICKS(PAIR_MAX_AGE_MS) ||
        (rx->capture_epoch && rx->capture_epoch != frame->capture_epoch)) return false;
    const uint64_t elapsed = now_us - rx->coarse_at_us;
    const uint64_t coarse_ns = rx->coarse_ns + elapsed * 1000;
    // Conservative replay and expiry enclosure. It is not an RF accuracy estimate.
    const uint64_t guard_ns = (rx->admission.uncertainty_us + 100 + elapsed / 500) * 1000;
    const uint64_t joined_upper = joined_utc_upper(&rx->admission, now_us);
    if (frame->created_ns + frame->utc_offset_ns > coarse_ns + guard_ns ||
        frame->created_ns + frame->utc_offset_ns > joined_upper ||
        frame->expires_ns + frame->utc_offset_ns <= coarse_ns + guard_ns ||
        frame->expires_ns + frame->utc_offset_ns <= joined_upper) return false;
    rx->capture_epoch = frame->capture_epoch;
    for (unsigned i = 0; i < frame->count; ++i)
        (void)match(rx, frame, &frame->correction[i], now_tick, now_us, coarse_ns, guard_ns);

    PtobOriginal *next = &rx->originals[rx->original_next];
    if (next->publication && next->capture.tick > rx->last_tick) ++rx->missed;
    *next = (PtobOriginal){capture, frame->created_ns, frame->expires_ns,
        frame->utc_offset_ns, frame->publication};
    rx->original_next = (rx->original_next + 1) % 5;
    return true;
}

bool ptob_clock_output(const PtobReceiverClock *rx, uint64_t tick, uint64_t phase_source_id,
                       int32_t phase_adjust_ns, RefClock *out, RefClockReading *reading)
{
    // Two matched pairs are required each time publication starts or returns.
    if (rx->pairs < 2) return false;
    RefClock model = rx->clock;
    if (phase_source_id && phase_source_id == rx->admission.root) {
        // Calibrate the delivered copy after matching and fitting. Its signed
        // deadline, rate and the receiver's replay checks stay in raw coordinates.
        const int64_t offset = phase_adjust_ns;
        if ((offset > 0 && model.anchor_ns > INT64_MAX - offset) ||
            (offset < 0 && model.anchor_ns < INT64_MIN - offset)) return false;
        model.anchor_ns += offset;
    }
    if (!ref_clock_read(&model, tick, reading)) return false;
    *out = model;
    return true;
}

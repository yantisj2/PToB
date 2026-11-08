// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ptob_rx.h"
#include "rx_port.h"
#include "counter.h"
#include "ref_timebase.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/sha256.h"
#include "nimble/nimble_port.h"
#include <stdatomic.h>
#include <string.h>
#include <sys/time.h>

#ifndef PTOB_TIER
#define PTOB_TIER 0
#endif
enum { QUEUE_DEPTH = 6, STACK_BYTES = 4096, MAX_PHASE_ADJUST_NS = 50000,
       GM_SESSION_US = 360000000 };
typedef struct {
    PtobLocator gm;
    PtobCapture capture;
    uint8_t bytes[PTOB_GR3_BYTES];
    uint32_t generation;
} Report;
PtobTrust ptob_rx_trust;
static PtobRxConfig options;
static QueueHandle_t reports;
static SemaphoreHandle_t mutex;
static StaticQueue_t queue_object;
static StaticSemaphore_t mutex_object;
static StaticTask_t worker_object;
static StackType_t stack[STACK_BYTES / sizeof(StackType_t)];
static _Alignas(4) uint8_t queue_storage[QUEUE_DEPTH * sizeof(Report)];
static struct ble_npl_event stop_event;
static atomic_bool stopping, worker_stopped, reacquire;
static atomic_uint drops;
static bool started, counter_available, adjustment_pending, wall_initialized;
static uint32_t generation = 1;
static PtobGmAnchor gm_anchor;
static uint64_t session_until_us;
static bool have_pair;
static RefClock delivered;
static PtobRxStatus status = {.state = PTOB_SEARCHING};
static uint64_t next_wall_us, last_report_us;

static uint64_t local_us(void) { return (uint64_t)esp_timer_get_time(); }
static void lock(void) { xSemaphoreTake(mutex, portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(mutex); }

// The JOIN grant covers acquisition. Once an advancing pair arrives, the
// receiver clock checks each original's signed expiry inside this session.
static bool anchor_live(uint64_t now)
{
    return gm_anchor.boot && now < session_until_us &&
        (have_pair || now < gm_anchor.bootstrap_until_us);
}

bool ptob_rx_hmac(const uint8_t key[32], const uint8_t *data, size_t len, uint8_t mac[32])
{
    // Fixed-size SHA-256 HMAC for the 32-byte PToB key, without heap allocation.
    uint8_t pad[64], inner[32];
    memset(pad, 0x36, sizeof pad);
    for (unsigned i = 0; i < 32; ++i) pad[i] ^= key[i];
    mbedtls_sha256_context sha;
    mbedtls_sha256_init(&sha);
    bool ok = !mbedtls_sha256_starts(&sha, 0) && !mbedtls_sha256_update(&sha, pad, sizeof pad) &&
        !mbedtls_sha256_update(&sha, data, len) && !mbedtls_sha256_finish(&sha, inner);
    for (unsigned i = 0; i < sizeof pad; ++i) pad[i] ^= 0x36 ^ 0x5c;
    ok = ok && !mbedtls_sha256_starts(&sha, 0) && !mbedtls_sha256_update(&sha, pad, sizeof pad) &&
        !mbedtls_sha256_update(&sha, inner, sizeof inner) && !mbedtls_sha256_finish(&sha, mac);
    mbedtls_sha256_free(&sha);
    return ok;
}

// All wall-clock writes, counter reads by the worker, and lifetime changes
// share this mutex. There are no BLE SDK calls under it.
static void clear_delivery(PtobRxState state)
{
    if (adjustment_pending) {
        const struct timeval zero = {0};
        if (adjtime(&zero, NULL)) state = PTOB_FAULT;
    }
    adjustment_pending = false;
    delivered = (RefClock){0};
    status = (PtobRxStatus){.state = state};
}

// Source changes retire the transport scope as well as the delivered clock.
static void withdraw(PtobRxState state)
{
    clear_delivery(state);
    last_report_us = 0;
    ++generation;
}

static void expire_delivery(PtobReceiverClock *clock, uint32_t scope, uint64_t now)
{
    if (scope == generation && ptob_clock_expire(clock, now))
        clear_delivery(PTOB_TIME_LOST);
}

static void write_wall(uint64_t now)
{
    if (!options.discipline_posix || now < next_wall_us) return;
    next_wall_us = now + 250000;
    RefClockReading reading;
    uint64_t before, after;
    if (!wall_initialized && options.step_on_first_lock) {
        if (!ref_counter_port_now(&before) || !ref_clock_read(&delivered, before, &reading)) return;
        const int64_t target = reading.time_ns / 1000;
        const struct timeval first = {.tv_sec = target / 1000000, .tv_usec = target % 1000000};
        if (settimeofday(&first, NULL)) { withdraw(PTOB_FAULT); return; }
        wall_initialized = true;
        return;
    }

    struct timeval current, pending;
    if (adjtime(NULL, &pending)) { withdraw(PTOB_FAULT); return; }
    if (pending.tv_sec || pending.tv_usec || !ref_counter_port_now(&before)) return;
    if (gettimeofday(&current, NULL)) { withdraw(PTOB_FAULT); return; }
    if (!ref_counter_port_now(&after) || after < before ||
        after - before > (PTOB_TIER == 0 ? 20 : 6) * REF_TICKS_PER_US ||
        !ref_clock_read(&delivered, before + (after - before) / 2, &reading)) return;

    const int64_t delta = reading.time_ns / 1000 - ((int64_t)current.tv_sec * 1000000 + current.tv_usec);
    if (delta < -250000 || delta > 250000) return; // A larger step needs explicit initialization.
    if (delta >= -1 && delta <= 1) { wall_initialized = true; return; }
    const struct timeval correction = {.tv_sec = delta / 1000000, .tv_usec = delta % 1000000};
    if (adjtime(&correction, NULL)) { withdraw(PTOB_FAULT); return; }
    adjustment_pending = true;
    wall_initialized = true;
}

static bool same_source(const PtobReceiverClock *clock, uint64_t root, const PtobFrame *f)
{
    return clock->admission.root == root && clock->admission.boot == f->boot && clock->admission.epoch == f->epoch &&
        clock->admission.train == f->train &&
        (!clock->capture_epoch || clock->capture_epoch == f->capture_epoch);
}

static void consume(PtobReceiverClock *clock, const Report *r, uint32_t *scope)
{
    if (*scope != r->generation) {
        *clock = (PtobReceiverClock){0};
        *scope = r->generation;
    }
    PtobFrame frame;
    if (!ptob_gr3_read(&ptob_rx_trust, &r->gm, r->bytes, sizeof r->bytes, &frame)) return;

    const uint64_t now = local_us();
    uint64_t tick;
    lock();
    const PtobGmAnchor anchor = gm_anchor;
    const uint64_t session_end = session_until_us;
    expire_delivery(clock, *scope, now);
    const bool have_tick = !atomic_load(&stopping) && status.state != PTOB_FAULT &&
        generation == *scope && anchor_live(now) && counter_available && ref_counter_port_now(&tick);
    unlock();
    if (!have_tick) return;

    if (frame.boot != anchor.boot || frame.epoch != anchor.epoch || frame.train != anchor.train ||
        (clock->admission.root && !same_source(clock, r->gm.root, &frame))) {
        lock();
        if (generation == *scope && status.state != PTOB_FAULT && !atomic_load(&stopping)) {
            atomic_store(&reacquire, true);
            withdraw(PTOB_TIME_LOST);
        }
        unlock();
        return;
    }
    if (!clock->admission.root) {
        const PtobAdmission admission = {.root = r->gm.root, .boot = anchor.boot, .utc_ns = anchor.utc_ns,
            .at_us = anchor.received_us, .session_until_us = session_end,
            .bootstrap_until_us = anchor.bootstrap_until_us, .epoch = anchor.epoch,
            .train = anchor.train, .uncertainty_us = anchor.uncertainty_us};
        ptob_clock_begin(clock, PTOB_TIER, &admission);
    }
    if (!ptob_clock_packet(clock, &frame, r->capture, tick, now)) return;

    lock();
    if (generation == *scope && !atomic_load(&stopping)) {
        have_pair = clock->last_reference_ns != 0;
        last_report_us = now;
        RefClockReading reading;
        if (ptob_clock_output(clock, tick, options.phase_source_id, options.phase_adjust_ns, &delivered, &reading)) {
            status = (PtobRxStatus){.state = PTOB_TIME_VALID, .source_id = r->gm.root,
                .phase_ns = reading.phase_remaining_ns, .rate_ppb = clock->clock.rate_ppb,
                .pairs = clock->pairs, .misses = clock->missed};
            write_wall(now);
        }
    }
    unlock();
}

static void worker(void *unused)
{
    (void)unused;
    PtobReceiverClock clock = {0};
    uint32_t scope = 0;
    TickType_t wait = portMAX_DELAY;
    while (!atomic_load(&stopping)) {
        Report report;
        if (xQueueReceive(reports, &report, wait) && report.generation) consume(&clock, &report, &scope);
        lock();
        const uint64_t now = local_us();
        expire_delivery(&clock, scope, now);
        wait = portMAX_DELAY;
        if (scope == generation && clock.permission_us > now) {
            const uint64_t ms = (clock.permission_us - now + 999) / 1000;
            wait = pdMS_TO_TICKS(ms) + 1;
        }
        unlock();
    }
    atomic_store(&worker_stopped, true);
    vTaskDelete(NULL);
}

static void stop_on_host(struct ble_npl_event *event)
{
    ble_npl_event_deinit(event);
    ptob_port_stop();
}

bool ptob_rx_start(const PtobRxConfig *config)
{
    if (!config || !config->key_epoch || mutex) return false;
    if (config->phase_adjust_ns && (!config->phase_source_id ||
        config->phase_adjust_ns < -MAX_PHASE_ADJUST_NS || config->phase_adjust_ns > MAX_PHASE_ADJUST_NS)) return false;
    uint8_t any = 0;
    for (unsigned i = 0; i < sizeof config->key; ++i) any |= config->key[i];
    if (!any) return false;
    options = *config;
    memcpy(ptob_rx_trust.psk, config->key, sizeof config->key);
    ptob_rx_trust.epoch = config->key_epoch;
    mutex = xSemaphoreCreateMutexStatic(&mutex_object);
    reports = xQueueCreateStatic(QUEUE_DEPTH, sizeof(Report), queue_storage, &queue_object);
    if (!mutex || !reports || !ptob_port_init()) return false;
    if (!xTaskCreateStatic(worker, "ptob_rx", STACK_BYTES, NULL, 4, stack, &worker_object)) {
        ptob_port_stop();
        return false;
    }
    ble_npl_event_init(&stop_event, stop_on_host, NULL);
    started = true;
    return true;
}

void ptob_rx_host_sync(void) { if (started && !atomic_load(&stopping)) ptob_port_start(); }
void ptob_rx_host_reset(void) { if (started && !atomic_load(&stopping)) ptob_port_reset(); }

PtobRxStatus ptob_rx_status(void)
{
    if (!started) return (PtobRxStatus){.state = PTOB_STOPPED};
    lock();
    PtobRxStatus copy = status;
    uint64_t tick;
    RefClockReading reading;
    if (copy.state == PTOB_TIME_VALID) {
        if (!atomic_load(&stopping) && counter_available && anchor_live(local_us()) &&
            ref_counter_port_now(&tick) && ref_clock_read(&delivered, tick, &reading)) {
            copy.utc_ns = reading.time_ns;
            copy.phase_ns = reading.phase_remaining_ns;
            copy.posix_adjusting = options.discipline_posix && wall_initialized;
        } else copy = (PtobRxStatus){.state = PTOB_TIME_LOST};
    }
    if (copy.state == PTOB_STOPPED && !atomic_load(&worker_stopped))
        copy.state = PTOB_TIME_LOST;
    copy.drops = atomic_load(&drops);
    unlock();
    return copy;
}

bool ptob_rx_now_ns(int64_t *utc_ns)
{
    if (!utc_ns) return false;
    const PtobRxStatus sample = ptob_rx_status();
    if (sample.state != PTOB_TIME_VALID) return false;
    *utc_ns = sample.utc_ns;
    return true;
}

void ptob_rx_stop(void)
{
    if (!started || atomic_exchange(&stopping, true)) return;
    lock();
    withdraw(PTOB_TIME_LOST);
    unlock();
    const Report wake = {0};
    (void)xQueueSend(reports, &wake, 0); // A full queue already wakes the worker.
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &stop_event);
}

void ptob_rx_port_state(PtobRxState state)
{
    lock();
    const bool retire_train = state == PTOB_TIME_LOST && last_report_us;
    if (status.state != PTOB_FAULT && (status.state != state || retire_train)) withdraw(state);
    unlock();
}

void ptob_rx_port_capture(const PtobLocator *gm, PtobCapture capture,
                          const uint8_t bytes[PTOB_GR3_BYTES])
{
    Report row = {.gm = *gm, .capture = capture};
    lock();
    row.generation = generation;
    unlock();
    memcpy(row.bytes, bytes, sizeof row.bytes);
    if (!xQueueSend(reports, &row, 0)) atomic_fetch_add(&drops, 1);
}

void ptob_rx_port_set_anchor(const PtobGmAnchor *anchor)
{
    lock();
    gm_anchor = *anchor;
    session_until_us = anchor->received_us + GM_SESSION_US;
    have_pair = false;
    withdraw(PTOB_SYNCING);
    unlock();
}

void ptob_rx_port_clear_anchor(void)
{
    lock();
    gm_anchor = (PtobGmAnchor){0};
    have_pair = false;
    if (status.state != PTOB_FAULT) withdraw(PTOB_TIME_LOST);
    unlock();
}

bool ptob_rx_port_anchor_live(void)
{
    lock();
    const bool valid = anchor_live(local_us());
    unlock();
    return valid;
}

bool ptob_rx_port_reacquire(void) { return atomic_exchange(&reacquire, false); }

void ptob_rx_counter_available(bool available)
{
    lock();
    counter_available = available;
    unlock();
}

uint64_t ptob_rx_port_last_report_us(void)
{
    lock();
    const uint64_t last = last_report_us;
    unlock();
    return last;
}

bool ptob_rx_port_fault(void)
{
    lock();
    const bool fault = status.state == PTOB_FAULT;
    unlock();
    return fault;
}

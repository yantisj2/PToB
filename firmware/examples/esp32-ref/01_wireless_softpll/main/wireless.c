// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// One NimBLE host task owns all radio APIs, session state and PLL updates.
// main.c only reads a short snapshot or requests shutdown.
#include "wireless.h"
#include "ref_local.h"
#include "ref_join.h"
#include "ref_session.h"
#include "radio.h"
#include "pulse.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "mbedtls/md.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_npl.h"
#include "host/ble_hs.h"
#include "host/ble_gap.h"
#include "host/util/util.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static RefSoftPll pll;
static RefJoin join;
static RefSession session;
static RefDisplayModel snapshot;
static portMUX_TYPE snapshot_lock = portMUX_INITIALIZER_UNLOCKED;
static struct ble_npl_callout service;
static uint64_t self, boot, next_challenge, last_receive;
static uint32_t publication = 1, last_tx_pub;
static uint16_t sync_handle = UINT16_MAX;
static uint8_t own_addr_type;
static bool initialized, syncing, periodic_started, discovery_started, challenge_started;
static bool ready, app_fault;
static atomic_bool stopping, drained, host_exited, stop_complete, stop_failed;
static uint64_t stopped_at_ms;
static RefCorrection history[2];
static unsigned history_count;
static struct { uint64_t peer, nonce, seen_tick; } challenges[8];
static unsigned challenge_next;
static RefFrame pending_beacon, pending_request;
static bool beacon_pending, request_pending;
static RefCounterHealth counter_health;

static uint64_t random_nonzero(void)
{ uint64_t value = 0; while (!value) esp_fill_random(&value, sizeof(value)); return value; }

static bool sign_frame(const uint8_t *data, size_t n, uint8_t tag[16])
{
    uint8_t digest[32];
    const mbedtls_md_info_t *sha = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!sha || mbedtls_md_hmac(sha, REF_GROUP_KEY, sizeof(REF_GROUP_KEY), data, n, digest)) return false;
    memcpy(tag, digest, 16);
    return true;
}

static int event(struct ble_gap_event *e, void *arg);

static int set_data(uint8_t instance, const RefFrame *frame, bool periodic)
{
    uint8_t bytes[REF_WIRE_BYTES];
    if (!ref_wire_encode(frame, sign_frame, bytes)) return BLE_HS_EINVAL;
    struct os_mbuf *data = os_msys_get_pkthdr(sizeof(bytes), 0);
    if (!data) return BLE_HS_ENOMEM;
    if (os_mbuf_append(data, bytes, sizeof(bytes))) { os_mbuf_free_chain(data); return BLE_HS_ENOMEM; }
    // Both SDK setters consume their mbuf, including their error paths.
    return periodic ? ble_gap_periodic_adv_set_data(instance, data) : ble_gap_ext_adv_set_data(instance, data);
}

static bool transient(int result)
{
    return result == BLE_HS_EBUSY || result == BLE_HS_EAGAIN || result == BLE_HS_ENOMEM ||
           result == BLE_HS_HCI_ERR(0x0c) || result == BLE_HS_HCI_ERR(0x07);
}

static void update_payloads(void)
{
    if (beacon_pending) {
        const int rc = set_data(0, &pending_beacon, true);
        if (!rc) beacon_pending = false;
        else if (!transient(rc)) app_fault = true;
    }
    if (request_pending) {
        const int rc = set_data(1, &pending_request, false);
        if (!rc) {
            request_pending = false;
            if (!challenge_started) {
                if (ble_gap_ext_adv_start(1, 0, 0)) app_fault = true;
                else challenge_started = true;
            }
        } else if (!transient(rc)) app_fault = true;
    }
}

static bool configure_advertiser(uint8_t instance, uint8_t sid)
{
    struct ble_gap_ext_adv_params p = {0};
    p.own_addr_type = own_addr_type;
    p.primary_phy = BLE_HCI_LE_PHY_1M;
    p.secondary_phy = BLE_HCI_LE_PHY_1M;
    p.tx_power = 0x7f;
    p.sid = sid;
    p.itvl_min = p.itvl_max = 160; // 100 ms discovery, separate from timing.
    return ble_gap_ext_adv_configure(instance, &p, NULL, event, NULL) == 0;
}

static bool scan(void)
{
    if (ble_gap_disc_active()) return true;
    struct ble_gap_ext_disc_params p = {0};
    p.passive = 1; p.itvl = 160; p.window = 160;
    return ble_gap_ext_disc(own_addr_type, 0, 0, 0, 0, 0, &p, NULL, event, NULL) == 0;
}

static RefFrame beacon(void)
{
    RefFrame f = {.kind = REF_BEACON, .sender = self, .session = boot, .publication = publication};
    f.count = (uint8_t)history_count;
    for (unsigned i = 0; i < history_count; ++i) f.corrections[i] = history[i];
    // Rotate fresh authenticated requests. A common broadcast supports several
    // followers; no follower controls the leader's free-running clock.
    uint64_t now = 0;
    (void)ref_counter_port_now(&now);
    for (unsigned i = 0; i < 8; ++i) {
        const unsigned slot = (challenge_next + i) % 8;
        if (now >= challenges[slot].seen_tick && now - challenges[slot].seen_tick > REF_MS_TICKS(8000)) challenges[slot].peer = 0;
        if (challenges[slot].peer) {
            f.reply_to = challenges[slot].peer; f.nonce = challenges[slot].nonce;
            challenge_next = (slot + 1) % 8;
            break;
        }
    }
    return f;
}

static void withdraw(void)
{
    memset(&join, 0, sizeof(join));
    ref_pll_begin(&pll);
    memset(&session, 0, sizeof(session));
    request_pending = false;
    ref_pulse_withdraw();
    ref_radio_port_scope();
}

static void request_challenge(uint64_t now, bool select_new, const RefFrame *f)
{
    if (select_new) {
        withdraw();
        ref_session_select(&session, self, f->sender, f->session, random_nonzero());
    } else ref_session_challenge(&session, random_nonzero());
    RefFrame request = {.kind = REF_CHALLENGE, .sender = self, .session = boot,
                        .publication = publication++, .nonce = session.nonce};
    if (!request.publication) { app_fault = true; return; }
    pending_request = request; request_pending = true;
    next_challenge = now + REF_MS_TICKS(2000);
}

static void receive(const RefRadioEvent *r, uint64_t now)
{
    RefFrame f;
    if (r->generation != ref_radio_port_generation() || !ref_wire_decode(r->bytes, sizeof(r->bytes), sign_frame, &f) ||
        f.kind != REF_BEACON || f.sender == self) return;
    last_receive = now;
    if (f.sender != session.peer || f.session != session.boot) {
        request_challenge(now, true, &f);
        return; // Fresh context has no permission from the old context.
    }
    if (!ref_session_accept(&session, &f, now)) return;
    if (challenge_started && session.nonce_used) {
        if (ble_gap_ext_adv_stop(1)) { app_fault = true; return; }
        challenge_started = false;
    }
    const RefPrediction prior = ref_pll_predict(&pll, r->tick);
    (void)ref_join_original(&join, f.publication, r->event, r->channel, r->tick, prior);
    for (unsigned i = 0; i < f.count; ++i) {
        RefOriginal original;
        if (ref_join_correction(&join, &f.corrections[i], now, &original)) {
            (void)ref_pll_observe(&pll, original.tick, now, f.corrections[i].time_ns,
                                   original.channel, original.prediction);
            if (pll.state == REF_PLL_FAULT) {
                request_challenge(now, true, &f);
                break;
            }
        }
    }
}

static void transmitted(const RefRadioEvent *r)
{
    RefFrame f;
    if (!ref_wire_decode(r->bytes, sizeof(r->bytes), sign_frame, &f) || f.kind != REF_BEACON ||
        f.sender != self || f.session != boot || f.publication <= last_tx_pub) return;
    // A repeated immutable payload is not a new publication. Its exact event
    // remains in the tuple, so a later correction cannot match the wrong copy.
    uint64_t ns;
    if (!ref_ticks_to_ns(r->tick, REF_PLL_HZ, &ns) || ns > INT64_MAX || publication == UINT32_MAX) {
        app_fault = true; return;
    }
    if (history_count) history[1] = history[0];
    history[0] = (RefCorrection){f.publication, r->event, r->channel, 1, (int64_t)ns};
    if (history_count < 2) ++history_count;
    last_tx_pub = f.publication;
    ++publication;
    pending_beacon = beacon(); beacon_pending = true;
}

static void publish_status(uint64_t now)
{
    RefDisplayModel m = {0};
    m.leader = REF_ROLE_LEADER;
    m.local_boot = boot; m.peer_id = session.peer; m.peer_boot = session.boot;
    snprintf(m.device, sizeof(m.device), "%s", REF_DEVICE_LABEL);
    if (session.peer) snprintf(m.peer, sizeof(m.peer), "%s", ref_registry_name(session.peer));
    RefClockReading reading;
    if (m.leader && ready && !app_fault) {
        uint64_t ns = 0;
        m.available = ref_ticks_to_ns(now, REF_PLL_HZ, &ns);
        m.relative_ns = ns;
        snprintf(m.state, sizeof(m.state), "REFERENCE");
        RefClock local;
        if (ref_clock_begin(&local, REF_PLL_HZ, now, (int64_t)ns, now + REF_MS_TICKS(500))) ref_pulse_model(&local, true);
    } else {
        m.admitted = session.until_tick && now < session.until_tick;
        m.available = m.admitted && ref_pll_read(&pll, now, &reading);
        if (m.available) { m.relative_ns = (uint64_t)reading.time_ns; m.phase_ns = reading.phase_remaining_ns; }
        snprintf(m.state, sizeof(m.state), "%s", app_fault ? "RADIO FAULT" :
            !syncing ? "SEARCHING" : !m.admitted ? "AUTHENTICATE" : ref_pll_state_name(pll.state));
        if (m.available) ref_pulse_model(&pll.clock, pll.state == REF_PLL_LOCK);
        else ref_pulse_withdraw();
    }
    m.rate_ppb = (int32_t)pll.rate_ppb;
    m.pairs = pll.accepted;
    m.predicted = pll.predicted; m.unavailable = pll.unavailable; m.refused = pll.refused;
    m.maximum_error_ns = pll.maximum_error_ns;
    m.missing = join.missing + ref_radio_port_dropped();
    m.rms_ns = pll.causal_rms_ns > UINT32_MAX ? UINT32_MAX : (uint32_t)pll.causal_rms_ns;
    m.rms_known = pll.error_count != 0;
    if (app_fault || ref_radio_port_fault()) { m.fault = true; m.available = false; snprintf(m.state, sizeof(m.state), "RADIO FAULT"); ref_pulse_withdraw(); }
    portENTER_CRITICAL(&snapshot_lock); snapshot = m; portEXIT_CRITICAL(&snapshot_lock);
}

static void service_tick(struct ble_npl_event *unused)
{
    (void)unused;
    if (atomic_load(&stopping)) {
        ref_pulse_withdraw();
        bool ok = true;
        if (ble_gap_disc_active() && ble_gap_disc_cancel()) ok = false;
        if (sync_handle != UINT16_MAX && ble_gap_periodic_adv_sync_terminate(sync_handle)) ok = false;
        if (periodic_started && ble_gap_periodic_adv_stop(0)) ok = false;
        if (discovery_started && ble_gap_ext_adv_stop(0)) ok = false;
        if (challenge_started && ble_gap_ext_adv_stop(1)) ok = false;
        if (!ok) atomic_store(&stop_failed, true);
        else atomic_store(&drained, true); // Successful calls are controller HCI acknowledgments.
        return;
    }
    uint64_t now = counter_health.tick;
    if (!ref_counter_port_now(&now) || !ref_counter_health(&counter_health, now, (uint64_t)esp_timer_get_time())) app_fault = true;
    if (app_fault || ref_radio_port_fault()) {
        app_fault = true; publish_status(now);
        ble_npl_callout_reset(&service, ble_npl_time_ms_to_ticks32(20));
        return;
    }
    RefRadioEvent r;
    unsigned budget = 16;
    while (budget-- && ref_radio_port_pop(&r)) {
        if (REF_ROLE_LEADER) { if (r.transmit) transmitted(&r); }
        else if (!r.transmit) receive(&r, now);
    }
    if (!REF_ROLE_LEADER && session.peer && now >= next_challenge) request_challenge(now, false, NULL);
    update_payloads();
    if (!REF_ROLE_LEADER && syncing && last_receive && now - last_receive > REF_MS_TICKS(3000)) {
        if (sync_handle != UINT16_MAX) {
            if (ble_gap_periodic_adv_sync_terminate(sync_handle)) app_fault = true;
            sync_handle = UINT16_MAX;
        }
        syncing = false; withdraw();
        if (!scan()) app_fault = true;
    }
    publish_status(now);
    ble_npl_callout_reset(&service, ble_npl_time_ms_to_ticks32(20));
}

static int event(struct ble_gap_event *e, void *arg)
{
    (void)arg;
    if (atomic_load(&stopping)) return 0;
    if (e->type == BLE_GAP_EVENT_EXT_DISC) {
        RefFrame f;
        const struct ble_gap_ext_disc_desc *d = &e->ext_disc;
        if (!ref_wire_decode(d->data, d->length_data, sign_frame, &f)) return 0;
        if (REF_ROLE_LEADER && f.kind == REF_CHALLENGE) {
            uint64_t now;
            if (!ref_counter_port_now(&now)) return 0;
            unsigned slot = 0;
            for (; slot < 8; ++slot) if (!challenges[slot].peer || challenges[slot].peer == f.sender ||
                (now >= challenges[slot].seen_tick && now - challenges[slot].seen_tick > REF_MS_TICKS(8000))) break;
            if (slot < 8) { challenges[slot].peer = f.sender; challenges[slot].nonce = f.nonce; challenges[slot].seen_tick = now; }
        } else if (!REF_ROLE_LEADER && !syncing && f.kind == REF_BEACON &&
                   d->sid == REF_WIRE_SID && d->periodic_adv_itvl) {
            struct ble_gap_periodic_sync_params p = {0};
            p.sync_timeout = 200; // Two seconds, no skipped timing opportunities.
            if (!ble_gap_periodic_adv_sync_create(&d->addr, d->sid, &p, event, NULL)) syncing = true;
        }
    } else if (e->type == BLE_GAP_EVENT_PERIODIC_SYNC) {
        if (e->periodic_sync.status) { syncing = false; (void)scan(); }
        else { sync_handle = e->periodic_sync.sync_handle; ref_radio_port_scope(); (void)ble_gap_disc_cancel(); }
    } else if (e->type == BLE_GAP_EVENT_PERIODIC_SYNC_LOST) {
        sync_handle = UINT16_MAX; syncing = false; withdraw();
        if (!scan()) app_fault = true;
    }
    return 0;
}

static void on_sync(void)
{
    if (ready || ble_hs_util_ensure_addr(0) || ble_hs_id_infer_auto(0, &own_addr_type) ||
        !ref_radio_port_install()) { app_fault = true; return; }
    boot = random_nonzero(); // RF is now active and supplies hardware RNG entropy.
    if (!configure_advertiser(REF_ROLE_LEADER ? 0 : 1, REF_WIRE_SID)) { app_fault = true; return; }
    if (REF_ROLE_LEADER) {
        const RefFrame f = beacon();
        struct ble_gap_periodic_adv_params p = {0};
        p.itvl_min = p.itvl_max = 50; // 50 * 1.25 ms = 62.5 ms, exactly 16 Hz.
        if (ble_gap_periodic_adv_configure(0, &p) || set_data(0, &f, true) ||
            set_data(0, &f, false) || ble_gap_periodic_adv_start(0)) { app_fault = true; return; }
        periodic_started = true;
        if (ble_gap_ext_adv_start(0, 0, 0)) { app_fault = true; return; }
        discovery_started = true;
    }
    if (!scan()) { app_fault = true; return; }
    ready = true;
    ble_npl_callout_init(&service, nimble_port_get_dflt_eventq(), service_tick, NULL);
    ble_npl_callout_reset(&service, 1);
}

static void on_reset(int reason) { (void)reason; app_fault = true; }
static void host_task(void *arg)
{
    (void)arg;
    nimble_port_run();
    atomic_store(&host_exited, true);
    vTaskDelete(NULL);
}

static void stop_task(void *arg)
{
    (void)arg;
    // The UI task retains the original two-second deadline even if an SDK
    // stop call blocks. No forced sleep may bypass this acknowledgment.
    if (initialized) {
        if (nimble_port_stop()) { atomic_store(&stop_failed, true); vTaskDelete(NULL); }
        while (!atomic_load(&host_exited)) vTaskDelay(1);
    }
    bool ok = ref_pulse_stop() && ref_radio_port_stop();
    if (initialized && nimble_port_deinit() != ESP_OK) ok = false;
    stopped_at_ms = (uint64_t)esp_timer_get_time() / 1000;
    atomic_store(ok ? &stop_complete : &stop_failed, true);
    vTaskDelete(NULL);
}

bool ref_wireless_begin(uint64_t identity)
{
    self = identity;
    ref_pll_begin(&pll);
    if (!ref_counter_port_begin() || nimble_port_init() != ESP_OK) return false;
    initialized = true;
    ble_hs_cfg.sync_cb = on_sync; ble_hs_cfg.reset_cb = on_reset;
    return xTaskCreatePinnedToCore(host_task, "ref_ble", 8192, NULL, 5, NULL, 0) == pdPASS;
}

void ref_wireless_snapshot(RefDisplayModel *m)
{
    portENTER_CRITICAL(&snapshot_lock); *m = snapshot; portEXIT_CRITICAL(&snapshot_lock);
    if (atomic_load(&stopping)) { m->available = false; m->admitted = false; }
}
void ref_wireless_request_stop(void)
{
    static bool worker_started;
    atomic_store(&stopping, true);
    if (!worker_started && (atomic_load(&drained) || !initialized || !ready)) {
        worker_started = true;
        if (xTaskCreatePinnedToCore(stop_task, "ref_stop", 4096, NULL, 3, NULL, 0) != pdPASS)
            atomic_store(&stop_failed, true);
    }
}
bool ref_wireless_stopped(void) { return atomic_load(&stop_complete); }
uint64_t ref_wireless_stopped_at_ms(void) { return atomic_load(&stop_complete) ? stopped_at_ms : 0; }
bool ref_wireless_stop_failed(void) { return atomic_load(&stop_failed); }

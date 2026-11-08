// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "rx_port.h"
#include "counter.h"
#include "join_nimble.h"
#include "receive_stamp.h"
#include "esp_timer.h"
#include "host/ble_gap.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "nimble/nimble_port.h"
#include "sdkconfig.h"
#include <string.h>

#if CONFIG_BT_NIMBLE_MAX_PERIODIC_SYNCS != 1
#error "Example 02 owns one periodic sync"
#endif

#ifndef PTOB_TIER
#define PTOB_TIER 0
#endif
enum { NO_SYNC = UINT16_MAX, ACQUIRE_US = 5000000, JOIN_RETRY_US = 2000000 };
static struct ble_npl_callout poll;
static PtobLocator gm, candidate;
static uint16_t sync_handle = NO_SYNC;
static int64_t sync_until, acquire_until, retry_at;
static uint8_t own_addr;
static bool poll_initialized;
static bool admitted, pending, scan_owned, running, stop_requested, terminate_requested, lost, fault;
static int gap_event(struct ble_gap_event *event, void *arg);

static bool stop_scan(void)
{
    if (!scan_owned) return true;
    const int rc = ble_gap_disc_cancel();
    if (rc && rc != BLE_HS_EALREADY) return false;
    scan_owned = false;
    return true;
}

static bool scan(void)
{
    // A different application owner may be using the global scanner.
    if (scan_owned) return true;
    if (ble_gap_disc_active()) return false;
    const struct ble_gap_ext_disc_params params = {.passive = 1, .itvl = 160, .window = 80};
    const int rc = ble_gap_ext_disc(own_addr, 0, 0, 0, 0, 0, &params, NULL, gap_event, NULL);
    scan_owned = rc == 0;
    return scan_owned;
}

static bool retire_sync(void)
{
    if (pending) {
        if (!terminate_requested) {
            const int rc = ble_gap_periodic_adv_sync_create_cancel();
            if (rc && rc != BLE_HS_EALREADY) return false;
            terminate_requested = true;
        }
        return false; // Wait for the real create completion, including cancellation.
    }
    if (sync_handle != NO_SYNC) {
        const int rc = ble_gap_periodic_adv_sync_terminate(sync_handle);
        if (rc && rc != BLE_HS_ENOTCONN && rc != BLE_HS_EALREADY) return false;
        sync_handle = NO_SYNC;
    }
    terminate_requested = false;
    admitted = false;
    memset(&gm, 0, sizeof gm);
    ptob_stamp_flush();
    return true;
}

static void stop_poll(void)
{
    if (poll_initialized) {
        ble_npl_callout_stop(&poll);
        ble_npl_callout_deinit(&poll);
        poll_initialized = false;
    }
}

static bool start_sync(void)
{
    const struct ble_gap_periodic_sync_params params = {.sync_timeout = 200};
    ble_addr_t address = {.type = BLE_ADDR_RANDOM};
    memcpy(address.val, gm.address, sizeof address.val);
    pending = true;
    sync_until = esp_timer_get_time() + 3000000;
    if (!ble_gap_periodic_adv_sync_create(&address, PTOB_GM_SID, &params, gap_event, NULL)) return true;
    pending = false;
    return false;
}

static void follow_or_discover(int64_t now)
{
    PtobGmAnchor anchor;
    if (ptob_bootstrap_poll(now, &anchor)) {
        ptob_rx_port_set_anchor(&anchor);
        admitted = true;
        // Resume scanning so the controller can find the periodic train's
        // SyncInfo. The GAP callback stops it once synchronization completes.
        if (!scan() || !start_sync()) lost = true;
    }
    if (ptob_bootstrap_busy()) return;

    if (sync_handle != NO_SYNC) {
        const uint64_t last = ptob_rx_port_last_report_us();
        if (last && (int64_t)last + ACQUIRE_US > acquire_until)
            acquire_until = (int64_t)last + ACQUIRE_US;
    }
    if (ptob_rx_port_reacquire() || (admitted && !ptob_rx_port_anchor_live()) ||
        (pending && now >= sync_until) || (sync_handle != NO_SYNC && now >= acquire_until))
        lost = true;
    if (lost) {
        if (admitted) ptob_rx_port_clear_anchor();
        admitted = false;
        if (!stop_scan() || !retire_sync()) return;
        lost = false;
    }
    if (admitted) return;

    if (candidate.root) {
        gm = candidate;
        candidate = (PtobLocator){0};
        retry_at = now + JOIN_RETRY_US;
        if (stop_scan() && ptob_bootstrap_start(&ptob_rx_trust, &gm, now)) {
            ptob_rx_port_state(PTOB_BOOTSTRAPPING);
            return;
        }
    }
    if (!scan()) fault = true;
    else ptob_rx_port_state(PTOB_SEARCHING);
}

static void service(struct ble_npl_event *unused)
{
    (void)unused;
    if (!running) return;
    if (ptob_stamp_fault() || ptob_rx_port_fault()) fault = true;
    if (stop_requested || fault) {
        if (fault) ptob_rx_port_state(PTOB_FAULT);
        if (ptob_bootstrap_stop() && stop_scan() && retire_sync()) {
            ptob_rx_counter_available(false);
            if (ptob_stamp_stop() && ref_counter_port_stop()) {
                running = false;
                stop_poll();
                if (!fault) ptob_rx_port_state(PTOB_STOPPED);
                return;
            }
            fault = true;
        }
    } else follow_or_discover(esp_timer_get_time());
    const uint32_t interval_ms = ptob_bootstrap_busy() ? 15 : 100;
    if (ble_npl_callout_reset(&poll, ble_npl_time_ms_to_ticks32(interval_ms)) != BLE_NPL_OK)
        ptob_rx_port_state(PTOB_FAULT);
}

static void discover(const struct ble_gap_ext_disc_desc *d)
{
    if (candidate.root || esp_timer_get_time() < retry_at || d->addr.type != BLE_ADDR_RANDOM ||
        d->data_status != BLE_HCI_ADV_DATA_STATUS_COMPLETE || !(d->props & 1)) return;
    (void)ptob_locator(&ptob_rx_trust, d->data, d->length_data, d->addr.val, &candidate);
}

static int gap_event(struct ble_gap_event *e, void *arg)
{
    (void)arg;
    switch (e->type) {
    case BLE_GAP_EVENT_EXT_DISC:
        if (!admitted && !ptob_bootstrap_busy() && !lost && !stop_requested && !fault)
            discover(&e->ext_disc);
        break;
    case BLE_GAP_EVENT_DISC_COMPLETE:
        // A delayed completion for our previous scan may arrive after restart.
        if (!ble_gap_disc_active()) scan_owned = false;
        break;
    case BLE_GAP_EVENT_PERIODIC_SYNC:
        pending = false;
        if (e->periodic_sync.status) {
            lost = true;
            break;
        }
        sync_handle = e->periodic_sync.sync_handle;
        if (terminate_requested || stop_requested || fault ||
            e->periodic_sync.sid != PTOB_GM_SID || e->periodic_sync.per_adv_ival != 50 ||
            e->periodic_sync.adv_phy != BLE_HCI_LE_PHY_1M ||
            e->periodic_sync.adv_addr.type != BLE_ADDR_RANDOM ||
            memcmp(gm.address, e->periodic_sync.adv_addr.val, 6) || !stop_scan()) {
            lost = true;
            break;
        }
        ptob_stamp_flush();
        acquire_until = esp_timer_get_time() + ACQUIRE_US;
        ptob_rx_port_state(PTOB_ACQUIRING);
        break;
    case BLE_GAP_EVENT_PERIODIC_SYNC_LOST:
        if (e->periodic_sync_lost.sync_handle == sync_handle) {
            sync_handle = NO_SYNC;
            lost = true;
            ptob_rx_port_state(PTOB_TIME_LOST);
        }
        break;
    case BLE_GAP_EVENT_PERIODIC_REPORT: {
        uint64_t now;
        PtobCapture capture;
        if (stop_requested || fault || lost || e->periodic_report.sync_handle != sync_handle ||
            e->periodic_report.data_status != BLE_HCI_PERIODIC_DATA_STATUS_COMPLETE ||
            e->periodic_report.data_length != PTOB_GR3_BYTES || !ref_counter_port_now(&now)) break;
        if (ptob_stamp_match(e->periodic_report.data, now, &capture))
            ptob_rx_port_capture(&gm, capture, e->periodic_report.data);
        break;
    }
    default: break;
    }
    return 0;
}

bool ptob_port_init(void)
{
    if (!ptob_bootstrap_register()) return false;
    poll_initialized = ble_npl_callout_init(&poll, nimble_port_get_dflt_eventq(), service, NULL) == 0;
    return poll_initialized;
}

void ptob_port_start(void)
{
    if (running) return;
    running = true; // Service must also unwind partial startup.
    if (ble_hs_util_ensure_addr(0) || ble_hs_id_infer_auto(0, &own_addr) ||
        !ref_counter_port_begin() || !ptob_stamp_install(PTOB_TIER == 0 ? 20 : 12)) {
        fault = true;
    } else ptob_rx_counter_available(true);
    service(NULL);
}

void ptob_port_stop(void)
{
    stop_requested = true;
    if (running) service(NULL);
    else {
        stop_poll();
        ptob_rx_port_state(PTOB_STOPPED);
    }
}

void ptob_port_reset(void)
{
    // A host reset has invalidated its handles. Retire the adapter on CPU0.
    scan_owned = pending = false;
    sync_handle = NO_SYNC;
    ptob_bootstrap_reset();
    fault = true;
    ptob_rx_port_state(PTOB_FAULT);
}

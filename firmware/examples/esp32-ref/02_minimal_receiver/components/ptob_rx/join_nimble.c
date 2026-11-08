// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "join_nimble.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "host/ble_att.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "os/os_mbuf.h"
#include <string.h>

enum { NO_LINK = UINT16_MAX, JOIN_TIMEOUT_US = 5000000 };
typedef enum { IDLE, CONNECTING, MTU, LOCATING, SEND_REQUEST, WAIT_CHALLENGE,
               SEND_PROOF, WAIT_ACCEPT, CLOSING } JoinState;
static const ble_uuid128_t service_id = BLE_UUID128_INIT(0x5c, 0xd4, 0x4e, 0x7d, 0x8a, 0x18, 0x4b, 0x97,
    0x75, 0x47, 0xef, 0xa6, 0x00, 0x04, 0x0d, 0x85);
static const ble_uuid128_t data_id = BLE_UUID128_INIT(0x5c, 0xd4, 0x4e, 0x7d, 0x8a, 0x18, 0x4b, 0x97,
    0x75, 0x47, 0xef, 0xa6, 0x01, 0x04, 0x0d, 0x85);
static const uint8_t marker[] = {'R', '2', 'G', '1', 1};
static JoinState state;
static uint16_t connection = NO_LINK, value_handle;
static uint64_t client_id, client_boot, connect_until_us, deadline_us, inbox_us;
static bool connecting, closing, inbox_full, anchor_ready;
static uint8_t inbox[PTOB_JOIN_BYTES];
static PtobJoin exchange;
static PtobLocator peer;
static PtobGmAnchor accepted;
static const PtobTrust *group;

static int access_record(uint16_t handle, uint16_t attr, struct ble_gatt_access_ctxt *ctx, void *arg)
{
    (void)attr;
    (void)arg;
    if (ctx->op == BLE_GATT_ACCESS_OP_READ_CHR)
        return os_mbuf_append(ctx->om, marker, sizeof marker) ? BLE_ATT_ERR_INSUFFICIENT_RES : 0;
    if (handle != connection || (state != WAIT_CHALLENGE && state != WAIT_ACCEPT) || inbox_full ||
        OS_MBUF_PKTLEN(ctx->om) != sizeof inbox || os_mbuf_copydata(ctx->om, 0, sizeof inbox, inbox))
        return BLE_ATT_ERR_UNLIKELY;
    // Measure arrival here. The host poll authenticates the record later,
    // and its scheduling delay must not shift the arrival time.
    inbox_us = (uint64_t)esp_timer_get_time();
    inbox_full = true;
    return 0;
}

static const struct ble_gatt_chr_def characteristics[] = {
    {.uuid = &data_id.u, .access_cb = access_record, .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP},
    {0},
};
static const struct ble_gatt_svc_def services[] = {
    {.type = BLE_GATT_SVC_TYPE_PRIMARY, .uuid = &service_id.u, .characteristics = characteristics},
    {0},
};

bool ptob_bootstrap_register(void)
{
    uint8_t factory[6];
    if (esp_efuse_mac_get_default(factory) != ESP_OK) return false;
    for (unsigned i = 0; i < sizeof factory; ++i) client_id = (client_id << 8) | factory[i];
    return client_id && !ble_gatts_count_cfg(services) && !ble_gatts_add_svcs(services);
}

static int located(uint16_t handle, const struct ble_gatt_error *error, struct ble_gatt_attr *attr, void *arg)
{
    (void)arg;
    if (handle != connection || state != LOCATING) return 1;
    uint8_t received[sizeof marker];
    if (!error->status && attr && attr->handle && attr->om && OS_MBUF_PKTLEN(attr->om) == sizeof marker &&
        !os_mbuf_copydata(attr->om, 0, sizeof received, received) && !memcmp(received, marker, sizeof marker)) {
        value_handle = attr->handle;
        state = SEND_REQUEST;
        return 1;
    }
    if (error->status) state = CLOSING;
    return 0;
}

static int mtu_done(uint16_t handle, const struct ble_gatt_error *error, uint16_t mtu, void *arg)
{
    (void)arg;
    if (handle != connection || state != MTU) return 0;
    state = LOCATING;
    if (error->status || mtu < PTOB_JOIN_BYTES + 3 ||
        ble_gattc_read_by_uuid(handle, 1, UINT16_MAX, &data_id.u, located, NULL)) state = CLOSING;
    return 0;
}

static int link_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    if (event->type == BLE_GAP_EVENT_CONNECT && connecting) {
        connecting = closing = false;
        if (event->connect.status) state = IDLE;
        else {
            connection = event->connect.conn_handle;
            if (state == CONNECTING) {
                state = MTU;
                const int rc = ble_gattc_exchange_mtu(connection, mtu_done, NULL);
                if (rc == BLE_HS_EALREADY) {
                    const struct ble_gatt_error ok = {0};
                    mtu_done(connection, &ok, ble_att_mtu(connection), NULL);
                } else if (rc) state = CLOSING;
            }
        }
    } else if (event->type == BLE_GAP_EVENT_DISCONNECT && event->disconnect.conn.conn_handle == connection) {
        connection = NO_LINK;
        state = IDLE;
        closing = inbox_full = false;
    }
    return 0;
}

bool ptob_bootstrap_start(const PtobTrust *trust, const PtobLocator *gm, uint64_t now_us, bool tracking)
{
    uint8_t address[6];
    if (state != IDLE || gm->root == client_id) return false;
    if (!tracking && (!ptob_join_address(trust, gm->mesh, client_id, address) || ble_hs_id_set_rnd(address)))
        return false;
    // The Bluetooth controller has started and supplies hardware RNG entropy.
    if (!client_boot) esp_fill_random(&client_boot, sizeof client_boot);
    exchange = (PtobJoin){.client = client_id, .boot = client_boot};
    esp_fill_random(&exchange.nonce, sizeof exchange.nonce);
    if (!client_boot || !exchange.nonce) return false;
    group = trust;
    peer = *gm;
    connect_until_us = now_us + (tracking ? 1500000 : 4000000);
    deadline_us = now_us + JOIN_TIMEOUT_US;
    inbox_full = anchor_ready = false;
    ble_addr_t target = {.type = BLE_ADDR_RANDOM};
    memcpy(target.val, gm->address, sizeof target.val);
    // A refresh uses the existing mesh address and leaves gaps for periodic RX.
    const struct ble_gap_conn_params params = {.scan_itvl = 32, .scan_window = tracking ? 8 : 32,
        .itvl_min = 6, .itvl_max = 6, .supervision_timeout = 50};
    state = CONNECTING;
    connecting = true;
    // Keep one deadline owner. The SDK's own timer also sends a cancel and
    // can race with our teardown. Completion still belongs to link_event.
    if (!ble_gap_connect(BLE_OWN_ADDR_RANDOM, &target, BLE_HS_FOREVER, &params, link_event, NULL)) return true;
    connecting = false;
    state = IDLE;
    return false;
}

bool ptob_bootstrap_busy(void) { return state != IDLE; }

static void close_link(void)
{
    state = CLOSING;
    if (closing) return;
    if (connecting) closing = !ble_gap_conn_cancel();
    else if (connection != NO_LINK) closing = !ble_gap_terminate(connection, BLE_ERR_REM_USER_CONN_TERM);
    else state = IDLE;
}

bool ptob_bootstrap_stop(void)
{
    anchor_ready = false;
    if (state != IDLE) close_link();
    return state == IDLE;
}

void ptob_bootstrap_reset(void)
{
    state = IDLE;
    connection = NO_LINK;
    connecting = closing = inbox_full = anchor_ready = false;
}

bool ptob_bootstrap_poll(uint64_t now_us, PtobGmAnchor *anchor)
{
    if (state == IDLE) {
        if (!anchor_ready) return false;
        anchor_ready = false;
        *anchor = accepted;
        return now_us < anchor->bootstrap_until_us;
    }
    if (state == CLOSING || now_us >= deadline_us ||
        (state == CONNECTING && now_us >= connect_until_us)) {
        close_link();
        return false;
    }
    if (inbox_full) {
        inbox_full = false;
        if (state == WAIT_CHALLENGE && ptob_join_challenge(group, &peer, &exchange, inbox))
            state = SEND_PROOF;
        else if (state == WAIT_ACCEPT && ptob_join_accept(group, &peer, &exchange, inbox, inbox_us, &accepted)) {
            anchor_ready = true;
            close_link();
        }
    }
    if (state == SEND_REQUEST || state == SEND_PROOF) {
        uint8_t wire[PTOB_JOIN_BYTES];
        const JoinState next = state == SEND_REQUEST ? WAIT_CHALLENGE : WAIT_ACCEPT;
        if (!ptob_join_send(group, &peer, &exchange, now_us, wire)) close_link();
        else {
            state = next;
            if (ble_gattc_write_no_rsp_flat(connection, value_handle, wire, sizeof wire)) close_link();
        }
    }
    return false;
}

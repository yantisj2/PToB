// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ptob_rx.h"
#include "screen.h"
#include "ref_local.h"
#include "ref_button.h"
#include "ref_shutdown.h"
#include "w12.h"
#include "driver/usb_serial_jtag.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

static RefShutdown shutdown;
static atomic_bool host_exited, shutdown_done, shutdown_failed;
static uint64_t completed_ms, identity;
static bool host_started;

static uint64_t now_ms(void) { return (uint64_t)esp_timer_get_time() / 1000; }
static void reset(int reason) { (void)reason; ptob_rx_host_reset(); }

static void host(void *unused)
{
    (void)unused;
    nimble_port_run();
    atomic_store(&host_exited, true);
    vTaskDelete(NULL);
}

static void stop_host(void *unused)
{
    (void)unused;
    if (host_started) {
        if (nimble_port_stop()) {
            atomic_store(&shutdown_failed, true);
            vTaskDelete(NULL);
            return;
        }
        while (!atomic_load(&host_exited)) vTaskDelay(1);
        if (nimble_port_deinit() != ESP_OK) {
            atomic_store(&shutdown_failed, true);
            vTaskDelete(NULL);
            return;
        }
    }
    completed_ms = now_ms();
    atomic_store(&shutdown_done, true);
    vTaskDelete(NULL);
}

static bool begin(void)
{
    if (nimble_port_init() != ESP_OK) return false;
    PtobRxConfig config = {.key_epoch = REF_KEY_EPOCH,
        .discipline_posix = true, .step_on_first_lock = true};
    memcpy(config.key, REF_GROUP_KEY, sizeof config.key);
    if (!ptob_rx_start(&config)) {
        (void)nimble_port_deinit();
        return false;
    }
    ble_hs_cfg.sync_cb = ptob_rx_host_sync;
    ble_hs_cfg.reset_cb = reset;
    host_started = xTaskCreatePinnedToCore(host, "nimble", 4096, NULL, 5, NULL, 0) == pdPASS;
    return host_started;
}

static void print_status(PtobRxStatus s)
{
    static const char *const states[] = {"BOOTSTRAPPING", "SEARCHING", "SYNCING", "ACQUIRING",
        "TIME_VALID", "TIME_LOST", "STOPPED", "FAULT"};
    printf("REF2_STATUS id=%016" PRIx64 " build=%.12s state=%s source=%016" PRIx64 " utc_ns=%" PRId64
           " rate_ppb=%" PRId32 " pairs=%" PRIu32 " misses=%" PRIu32 " drops=%" PRIu32
           " wall=%u power=%s\n", identity, REF_SOURCE_ID, states[s.state], s.source_id, s.utc_ns, s.rate_ppb, s.pairs,
           s.misses, s.drops, s.posix_adjusting, ref_shutdown_stage_name(shutdown.stage));
}

static void command(const char *line)
{
    if (!strcmp(line, "stop")) ref_shutdown_request(&shutdown, false, now_ms());
    else if (!strcmp(line, "status")) print_status(ptob_rx_status());
    else if (*line) puts("REF2 commands: status, stop");
}

static void read_usb(void)
{
    static char line[64];
    static unsigned count;
    static bool overflow;
    uint8_t byte;
    for (unsigned i = 0; i < 64 && usb_serial_jtag_read_bytes(&byte, 1, 0) == 1; ++i) {
        if (byte == '\n' || byte == '\r') {
            line[count] = '\0';
            if (!overflow) command(line);
            else puts("REF2 command too long");
            count = 0;
            overflow = false;
        } else if ((byte == '\b' || byte == 127) && count && !overflow) --count;
        else if (byte >= 32 && byte <= 126 && !overflow && count + 1 < sizeof line)
            line[count++] = (char)byte;
        else overflow = true;
    }
}

void app_main(void)
{
    uint8_t mac[6];
    if (esp_read_mac(mac, ESP_MAC_EFUSE_FACTORY) != ESP_OK ||
        (REF_DEVICE_BOUND && memcmp(mac, REF_EXPECTED_MAC, sizeof mac))) {
        puts("REF2_REFUSED factory binding differs");
        return;
    }
    for (unsigned i = 0; i < sizeof mac; ++i) identity = (identity << 8) | mac[i];
    usb_serial_jtag_driver_config_t usb = {.tx_buffer_size = 512, .rx_buffer_size = 256};
    const bool console = usb_serial_jtag_driver_install(&usb) == ESP_OK;
    const bool board_ok = !REF_BOARD_W12 || ref_w12_wake();
    bool display = REF_BOARD_W12 && board_ok && ref_w12_display_begin();
    const bool started = board_ok && begin();
    RefButton button = {0};
    bool info = false, stop_started = false, ready = false;
    uint64_t drawn = 0;
    static uint8_t fb[1024], previous[1024];
    puts("ESP32-REF 02 / BLE grandmaster time receiver");
    puts("USB commands: status, stop");

    for (;;) {
        const uint64_t now = now_ms();
        const RefButtonEvent event = REF_BOARD_W12 && board_ok ?
            ref_button_poll(&button, ref_w12_pressed(), now) : REF_BUTTON_NONE;
        if (event == REF_BUTTON_TAP && !shutdown.requested) info = !info;
        if (event == REF_BUTTON_OFF) ref_shutdown_request(&shutdown, true, now);
        if (console) read_usb();

        PtobRxStatus state = started ? ptob_rx_status() : (PtobRxStatus){.state = PTOB_FAULT};
        const bool released = !REF_BOARD_W12 || ref_button_released(&button, now);
        if (shutdown.requested) {
            ptob_rx_stop();
            if (!stop_started && started && state.state == PTOB_STOPPED) {
                stop_started = true;
                if (xTaskCreate(stop_host, "stop_ble", 4096, NULL, 3, NULL) != pdPASS)
                    atomic_store(&shutdown_failed, true);
            }
            const bool done = atomic_load(&shutdown_done);
            const RefShutdownStage stage = ref_shutdown_poll(&shutdown, now, done,
                done ? completed_ms : 0, state.state == PTOB_FAULT || atomic_load(&shutdown_failed), released);
            if (stage == REF_SHUTDOWN_READY && !ready) { puts("REF2_READY radio=OFF counter=OFF"); ready = true; }
            if (stage == REF_SHUTDOWN_SLEEP) {
                const bool off = ref_w12_display_stop();
                if (off) (void)ref_w12_sleep();
                ref_shutdown_board_failed(&shutdown);
                if (off && ref_w12_wake()) display = ref_w12_display_begin();
                memset(previous, 0, sizeof previous);
            }
        }
        if (display && (event != REF_BUTTON_NONE || now - drawn >= 200)) {
            const char *source = ptob_registry_name(state.source_id);
            const ReceiverScreen screen = {.receiver = state, .alias = REF_DEVICE_LABEL,
                .source = source ? source : "T096 GM", .tier = PTOB_TIER, .info = info,
                .power_off = shutdown.power_off, .released = released, .power_fault = shutdown.fault};
            receiver_screen(&screen, fb);
            if (memcmp(previous, fb, sizeof fb)) {
                display = ref_w12_display_send(fb);
                memcpy(previous, fb, sizeof fb);
            }
            drawn = now;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

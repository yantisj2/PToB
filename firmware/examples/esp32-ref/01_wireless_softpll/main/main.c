// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "wireless.h"
#include "ref_local.h"
#include "ref_button.h"
#include "ref_shutdown.h"
#include "w12.h"
#include "pulse.h"
#include "driver/usb_serial_jtag.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint64_t identity;
static uint8_t framebuffer[1024];
static RefShutdown shutdown;

static void status(const RefDisplayModel *m)
{
    printf("REF1_STATUS {\"id\":\"%016" PRIx64 "\",\"version\":\"0.2.1\",\"source\":\"%.12s\",\"role\":\"%s\",\"state\":\"%s\","
           "\"available\":%u,\"admitted\":%u,\"pairs\":%" PRIu32 ",\"missing\":%" PRIu32
           ",\"boot\":\"%016" PRIx64 "\",\"peer\":\"%016" PRIx64 "\",\"peer_boot\":\"%016" PRIx64 "\","
           "\"predicted\":%" PRIu32 ",\"unavailable\":%" PRIu32 ",\"refused\":%" PRIu32 ",\"max_error_ns\":%" PRIu64
           ",\"rms_ns\":%" PRIu32 ",\"rms_known\":%u,\"rate_ppb\":%" PRId32 ",\"phase_remaining_ns\":%" PRId64
           ",\"power_stage\":\"%s\",\"power_fault\":%u,\"failed_stage\":\"%s\",\"owners_stopped\":%u,"
           "\"tx\":\"PROGRAMMED_ESTIMATE\",\"rf_bound\":\"UNKNOWN\",\"counter_hz\":%d,\"pulse_gpio\":%d}\n",
           identity, REF_SOURCE_ID, REF_ROLE_LEADER ? "LEADER" : "FOLLOWER",
           shutdown.fault ? "OFF FAILED" : shutdown.requested ? ref_shutdown_stage_name(shutdown.stage) : m->state,
           m->available, m->admitted, m->pairs, m->missing, m->local_boot, m->peer_id, m->peer_boot,
           m->predicted, m->unavailable, m->refused, m->maximum_error_ns, m->rms_ns, m->rms_known, m->rate_ppb,
           m->phase_ns, ref_shutdown_stage_name(shutdown.stage), shutdown.fault,
           shutdown.fault ? shutdown.board_failure ? "BOARD_POWER" : "OWNER_DRAIN" : "NONE",
           ref_wireless_stopped(), REF_CLOCK_HZ, REF_PULSE_GPIO);
}

void app_main(void)
{
    uint8_t factory[6];
    if (esp_read_mac(factory, ESP_MAC_EFUSE_FACTORY) != ESP_OK ||
        (REF_DEVICE_BOUND && memcmp(factory, REF_EXPECTED_MAC, 6))) {
        puts("REF1_REFUSED factory binding differs; board pins untouched");
        return;
    }
    for (unsigned i = 0; i < 6; ++i) identity = (identity << 8) | factory[i];
    usb_serial_jtag_driver_config_t usb = {.tx_buffer_size = 1024, .rx_buffer_size = 256};
    const bool console = usb_serial_jtag_driver_install(&usb) == ESP_OK;
    bool display = false;
    bool start_ok = true;
    if (REF_BOARD_W12) {
        start_ok = ref_w12_wake();
        if (start_ok) display = ref_w12_display_begin();
    }
    if (start_ok) start_ok = ref_pulse_begin() && ref_wireless_begin(identity);
    RefButton button = {0};
    bool details = false, ready_printed = false;
    uint64_t drawn_ms = 0, logged_ms = 0;
    char line[48]; unsigned line_length = 0; bool line_overflow = false;
    puts("ESP32-REF 01 / relative BLE SoftPLL / no UTC reference");
    puts("Commands: status, stop. Short button: details. Hold 4s, release: power off.");
    while (true) {
        const uint64_t now = (uint64_t)esp_timer_get_time() / 1000;
        const RefButtonEvent action = REF_BOARD_W12 ? ref_button_poll(&button, ref_w12_pressed(), now) : REF_BUTTON_NONE;
        if (action == REF_BUTTON_TAP && !shutdown.requested) details = !details;
        if (action == REF_BUTTON_OFF) {
            ref_shutdown_request(&shutdown, true, now);
        }
        if (console) {
            uint8_t byte;
            for (unsigned budget = 0; budget < 64 && usb_serial_jtag_read_bytes(&byte, 1, 0) == 1; ++budget) {
                if (byte == '\n' || byte == '\r') {
                    line[line_length] = 0;
                    if (line_overflow) puts("REF1_ERROR invalid or overlong command ignored");
                    else if (!strcmp(line, "stop")) ref_shutdown_request(&shutdown, false, now);
                    else if (!strcmp(line, "status")) { RefDisplayModel m; ref_wireless_snapshot(&m); status(&m); }
                    else if (line_length) puts("REF1_HELP commands: status, stop");
                    line_length = 0; line_overflow = false;
                } else if ((byte == 8 || byte == 127) && !line_overflow) {
                    if (line_length) --line_length;
                } else if (byte < 32 || byte > 126 || line_length + 1 >= sizeof(line)) line_overflow = true;
                else if (!line_overflow) line[line_length++] = (char)byte;
            }
        }
        if (shutdown.requested) {
            ref_wireless_request_stop();
            const RefShutdownStage stage = ref_shutdown_poll(&shutdown, now, ref_wireless_stopped(),
                ref_wireless_stopped_at_ms(), ref_wireless_stop_failed(),
                !REF_BOARD_W12 || ref_button_released(&button, now));
            if (!shutdown.fault) {
                if (stage == REF_SHUTDOWN_READY && !ready_printed) {
                    printf("REF1_READY id=%016" PRIx64 " radio=OFF counter=OFF output=OFF\n", identity);
                    ready_printed = true;
                }
                if (stage == REF_SHUTDOWN_SLEEP) {
                    bool stopped_display = ref_w12_display_stop();
                    if (stopped_display) (void)ref_w12_sleep();
                    ref_shutdown_board_failed(&shutdown); // Any return is failure.
                    if (stopped_display && ref_w12_wake()) display = ref_w12_display_begin();
                }
            }
        }
        RefDisplayModel model;
        ref_wireless_snapshot(&model);
        if (!start_ok) { model.fault = true; snprintf(model.state, sizeof(model.state), "START FAILED"); }
        if (shutdown.stage == REF_SHUTDOWN_READY) snprintf(model.state, sizeof(model.state), "RADIO OFF");
        if (shutdown.fault) snprintf(model.state, sizeof(model.state), "OFF FAILED");
        if (!model.state[0]) snprintf(model.state, sizeof(model.state), "STARTING");
        snprintf(model.device, sizeof(model.device), "%s", REF_DEVICE_LABEL);
        model.details = details; model.stopping = shutdown.power_off; model.fault = model.fault || shutdown.fault;
        model.released = !REF_BOARD_W12 || ref_button_released(&button, now);
        if (action != REF_BUTTON_NONE || now - drawn_ms >= 250) {
            if (display) { ref_display_render(&model, framebuffer); if (!ref_w12_display_send(framebuffer)) display = false; }
            drawn_ms = now;
        }
        if (now - logged_ms >= 1000) { status(&model); logged_ms = now; }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

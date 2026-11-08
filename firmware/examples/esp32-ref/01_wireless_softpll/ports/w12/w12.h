// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_W12_H
#define ESP32_REF_W12_H
#include <stdbool.h>
#include <stdint.h>
bool ref_w12_wake(void);
bool ref_w12_pressed(void);
bool ref_w12_display_begin(void);
bool ref_w12_display_send(const uint8_t framebuffer[1024]);
bool ref_w12_display_stop(void);
// Caller has stopped BLE, capture, output, I2C and has observed 40 ms release.
// Success enters deep sleep and never returns. Any return is a failure.
bool ref_w12_sleep(void);
#endif

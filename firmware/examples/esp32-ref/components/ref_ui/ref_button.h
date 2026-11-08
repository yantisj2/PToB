// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_BUTTON_H
#define ESP32_REF_BUTTON_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { REF_BUTTON_NONE, REF_BUTTON_TAP, REF_BUTTON_OFF } RefButtonEvent;
typedef struct {
    uint64_t changed_ms, pressed_ms;
    bool raw_pressed, stable_pressed, armed, hold_sent;
} RefButton;
// Begin with zeroed state. A held-at-boot button is ignored until released.
RefButtonEvent ref_button_poll(RefButton *button, bool pressed, uint64_t now_ms);
bool ref_button_released(const RefButton *button, uint64_t now_ms);
#endif

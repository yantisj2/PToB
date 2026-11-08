// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "ref_button.h"

RefButtonEvent ref_button_poll(RefButton *b, bool pressed, uint64_t now)
{
    if (pressed != b->raw_pressed) { b->raw_pressed = pressed; b->changed_ms = now; }
    if (!b->armed) {
        if (!pressed && now - b->changed_ms >= 40) b->armed = true;
        return REF_BUTTON_NONE;
    }
    if (now - b->changed_ms >= 30 && b->stable_pressed != pressed) {
        b->stable_pressed = pressed;
        if (pressed) { b->pressed_ms = now; b->hold_sent = false; }
        else if (!b->hold_sent && now - b->pressed_ms >= 40) return REF_BUTTON_TAP;
    }
    if (b->stable_pressed && !b->hold_sent && now - b->pressed_ms >= 4000) {
        b->hold_sent = true;
        return REF_BUTTON_OFF;
    }
    return REF_BUTTON_NONE;
}

bool ref_button_released(const RefButton *b, uint64_t now)
{ return b->armed && !b->raw_pressed && !b->stable_pressed && now - b->changed_ms >= 40; }

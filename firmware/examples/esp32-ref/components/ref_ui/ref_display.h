// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_DISPLAY_H
#define ESP32_REF_DISPLAY_H
#include <stdbool.h>
#include <stdint.h>

// Pure framebuffer rendering. The board adapter owns I2C and OLED power.
typedef struct {
    char state[20], device[25], peer[25];
    uint64_t relative_ns;
    uint64_t local_boot, peer_id, peer_boot, maximum_error_ns;
    int64_t phase_ns;
    int32_t rate_ppb;
    uint32_t pairs, missing, rms_ns;
    uint32_t predicted, unavailable, refused;
    bool leader, available, admitted, rms_known, details, stopping, released, fault;
} RefDisplayModel;
// Shared 5×7 OLED font, clipped to the right inset.
void ref_fb_text(uint8_t *fb, unsigned x, unsigned y, const char *s, unsigned scale);
void ref_fb_right(uint8_t *fb, unsigned y, const char *s);
void ref_display_render(const RefDisplayModel *model, uint8_t framebuffer[1024]);
#endif

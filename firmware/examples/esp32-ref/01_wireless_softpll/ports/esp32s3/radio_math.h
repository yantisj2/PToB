// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_RADIO_MATH_H
#define ESP32_REF_RADIO_MATH_H
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint64_t tick, last_read_us;
    bool started, failed;
} RefCaptureCounter;

// Extend the low 32 bits of the 80 MHz hardware counter. The watchdog clock
// only bounds wrap ambiguity. Its value never enters the delivered time.
bool ref_capture32_advance(RefCaptureCounter *counter, uint32_t raw, uint64_t now_us,
                          uint64_t *tick);
typedef struct { uint64_t before, after; uint32_t slots, half_us; } RefRadioBridge;
typedef struct { uint64_t tick, coarse_us; bool started, failed; } RefCounterHealth;
// Coarse time is only a watchdog. It never changes a phase timestamp.
bool ref_counter_health(RefCounterHealth *health, uint64_t tick, uint64_t coarse_us);
bool ref_radio_rx_tick(const RefRadioBridge *bridge, uint32_t coarse, uint16_t fine,
                        unsigned extra_us, uint64_t *tick);
bool ref_radio_tx_tick(const RefRadioBridge *bridge, uint32_t cookie,
                        uint64_t available, uint64_t *tick);
bool ref_radio_channel(uint32_t access_address, const uint16_t map[3],
                        uint16_t event, uint8_t *channel);
#endif

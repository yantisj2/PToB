/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Existing TIMER3 extension is the ONLY wrap/epoch owner. ISR readers never
 * create an extension or splice LF callback time into the GM7 domain.
 * HF-derived microseconds, with original capture epoch and integer rounding. */
bool r2_gm7_clock_now(uint64_t *microseconds, uint32_t *epoch);
bool r2_gm7_clock_event(uint32_t raw, uint32_t epoch, uint64_t *ticks);
/* Called exclusively by the existing serialized capture extension/stop. */
void r2_gm7_clock_begin(uint32_t new_epoch);
void r2_gm7_clock_invalidate(void);
void r2_gm7_clock_publish(uint64_t extended, uint32_t raw, uint64_t epoch,
                          bool valid);

/* Standard BLE packet preamble+access-address duration. This returns the
 * register START coordinate; callers MUST enclose its applicable PHY chain and
 * quantization before treating it as a physical packet endpoint. Existing G1
 * ADDRESS stays unchanged. */
bool r2_gm7_clock_start(uint32_t address_raw, uint32_t epoch, unsigned phy,
                        uint64_t *start_tick);

/* Outward HF-tick enclosure. chain_us comes from the pinned HAL ceiling for
 * the actual PHY/direction; include one capture tick of quantization. */
bool r2_gm7_clock_bounds(uint32_t raw, uint32_t epoch, unsigned prefix_us,
                         unsigned chain_us, uint64_t *lower, uint64_t *upper);

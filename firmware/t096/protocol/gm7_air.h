/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "gm7_schedule.h"
#define GM7_PRIMARY_PDU_BYTES 15u
#define GM7_AUX_OFFSET_US 510u
#define GM7_AUX_TARGET_US 525u
#define GM7_AUX_GAP_US (GM7_AUX_TARGET_US - 184u)
/* Exact nonconnectable, nonscannable, unchained extended advertising pair.
 * The primary is LE1M/channel37. Its AuxPtr names an LE2M data channel and
 * the [510,540)us offset interval from the primary's actual packet START.
 * This builder grants no timing or source permission. */
int gm7_air_build(uint8_t primary[GM7_PRIMARY_PDU_BYTES], struct gm7_job *,
                  const uint8_t static_random_address[6],
                  uint8_t auxiliary_channel);
int gm7_air_received(const uint8_t *pdu, size_t available, const uint8_t **ad,
                     size_t *length, uint8_t address[6]);

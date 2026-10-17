/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "dc_clock.h"
struct r2_identity;
struct r2_ui_model;
void r2_dc_init(const struct r2_identity *);
void r2_dc_poll(bool allowed);
void r2_dc_ui(struct r2_ui_model *);
bool r2_dc_metric(uint32_t *rms,unsigned *samples);
void r2_dc_usb_observation(bool valid,uint64_t tick,uint64_t ns,uint16_t frame,
 uint32_t usb,uint32_t capture,uint32_t source,uint32_t generation);

/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "clock.h"
#include "precision.h"
#include "rc_unicast.h"
/* Pure conversion of an already published local GNSS map. No authority from USB. */
bool r2_dc_clock(const struct r2_source_clock *,const struct r2_clock *,
                 struct r2_capture,uint64_t now_us,uint64_t identity,uint64_t boot,struct r2_uc_clock *);
#define R2_DC_RMS_WINDOW 32u
struct r2_dc_rms {
 uint64_t tick,ns,squares,last_ms;
 uint32_t usb_epoch,capture_epoch,source_epoch,generation;
 uint32_t errors[R2_DC_RMS_WINDOW];unsigned count,head;uint16_t frame;bool seen;
};
void r2_dc_rms_add(struct r2_dc_rms *,uint64_t tick,uint64_t ns,uint16_t frame,
 uint32_t usb_epoch,uint32_t capture_epoch,uint32_t source_epoch,uint32_t generation,uint64_t now_ms);
bool r2_dc_rms_read(const struct r2_dc_rms *,uint64_t now_ms,uint32_t *rms,unsigned *samples);

/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#ifndef R2_DC_GNSS_H
#define R2_DC_GNSS_H
#include <stddef.h>
#include <stdint.h>
void r2_dc_gnss_start(void);
void r2_dc_gnss_receive(const uint8_t *,size_t,int64_t);
void r2_dc_gnss_usb_reset(void);
void r2_dc_gnss_stop(void);
size_t r2_dc_gnss_next(uint8_t *,size_t,int64_t,uint32_t *);
uint32_t r2_dc_gnss_epoch(void);
/* Raw output/input losses, rejected host frames, command drops, queued bytes. */
void r2_dc_gnss_status(uint32_t out[5]);
#endif

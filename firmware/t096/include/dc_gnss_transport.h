/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#ifndef R2_DC_GNSS_TRANSPORT_H
#define R2_DC_GNSS_TRANSPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define DC_GNSS_FRAME_MAX 1029u
#define DC_GNSS_RING_SIZE 4096u
struct dc_gnss_ring { uint8_t bytes[DC_GNSS_RING_SIZE]; unsigned head,tail,count; uint32_t dropped; };
size_t dc_gnss_ring_put(struct dc_gnss_ring *,const uint8_t *,size_t);
size_t dc_gnss_ring_get(struct dc_gnss_ring *,uint8_t *,size_t);
struct dc_gnss_demux {
 uint8_t line[128],header[3]; unsigned used,header_used,binary_left; int64_t started;
 bool discard; uint32_t overflow;
};
typedef void (*dc_gnss_time_sink)(const uint8_t *,size_t,int64_t,void *);
/* Only RMC/GGA and aid ACKs enter the existing clock parser. RTCM is opaque. */
void dc_gnss_demux_feed(struct dc_gnss_demux *,const uint8_t *,size_t,int64_t,dc_gnss_time_sink,void *);
uint32_t dc_gnss_crc24q(const uint8_t *,size_t);
bool dc_gnss_command_allowed(const uint8_t *,size_t);
struct dc_gnss_frame_parser { uint8_t bytes[DC_GNSS_FRAME_MAX]; size_t used,wanted; int64_t started; uint32_t rejected; };
/* Returns a whole validated command/correction frame. Never forwards partial data. */
size_t dc_gnss_frame_byte(struct dc_gnss_frame_parser *,uint8_t,int64_t);
#endif

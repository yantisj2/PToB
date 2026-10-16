/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define R2_PRECISION_AD_BYTES 158u
#define R2_PRECISION_BODY_BYTES 122u
#define R2_PRECISION_INTERVAL_US 31250u
struct r2_precise_correction {
 uint32_t publication;
 uint16_t event;
 uint8_t channel;
 uint64_t ticks,generation,expires_ns;
};
struct r2_precision_packet {
 uint64_t boot,created_ns,expires_ns;
 int64_t utc_offset_ns;
 uint32_t source_epoch,train,publication,capture_epoch;
 uint8_t root,flags,count;
 struct r2_precise_correction correction[2];
};
int r2_precision_encode(uint8_t ad[R2_PRECISION_AD_BYTES],const struct r2_precision_packet *p,const uint8_t keys[4][16]);
int r2_precision_decode(struct r2_precision_packet *p,const uint8_t *ad,size_t size,unsigned recipient,const uint8_t key[16]);

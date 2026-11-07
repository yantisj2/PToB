/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Diagnostic only. No failure/reset/permission predicate is derived here. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <errno.h>
#define R2_RC_CHECK_TAG UINT16_C(0x8000)
#define R2_RC_CHECK_SITE_MAX 4095u
#define R2_RC_CHECK_TRANSPORT_BASE 2560u
#define R2_RC_CHECK_OPERATION UINT32_C(0x5243434b) /* RCCK, not a peer/API op */
static inline uint16_t r2_rc_check_pack(unsigned site){
 return site&&site<=R2_RC_CHECK_SITE_MAX?(uint16_t)(R2_RC_CHECK_TAG|(site<<3)):0;
}
static inline unsigned r2_rc_check_site(int error,uint16_t detail){
 return error==-EIO&&(detail&R2_RC_CHECK_TAG)?(detail>>3)&R2_RC_CHECK_SITE_MAX:0;
}

/* Upper16 bits are volatile owner provenance in the same4B scalar. Never
 * wrap: exhausted metadata stays UNKNOWN until a physical BSS reset. */
#define R2_RC_CHECK_GENERATION_MASK UINT32_C(0xffff0000)
#define R2_RC_CHECK_GENERATION_STEP UINT32_C(0x00010000)
static inline uint32_t r2_rc_check_next(uint32_t word){
 uint32_t generation=word&R2_RC_CHECK_GENERATION_MASK;
 return generation==R2_RC_CHECK_GENERATION_MASK?generation:generation+R2_RC_CHECK_GENERATION_STEP;
}
static inline uint16_t r2_rc_check_recorded(uint32_t word){
 return (word&R2_RC_CHECK_GENERATION_MASK)==R2_RC_CHECK_GENERATION_MASK?0:(uint16_t)word;
}

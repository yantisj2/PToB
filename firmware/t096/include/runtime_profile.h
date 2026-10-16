/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
/* Defaults are operator-selected profiles, not elected serving roles. */
static inline bool r2_profile_default_rc(uint64_t silicon){(void)silicon;return false;}
static inline uint8_t r2_profile_caps(bool rc){return rc?6u:3u;}
/* Former GM publications never authorize a loop back through that same unit. */
static inline bool r2_profile_relay_root(uint64_t local,uint64_t root,uint64_t boot,uint32_t epoch){return local&&root&&root!=local&&boot&&epoch;}
/* Each role migration gets a fresh authenticated time/control incarnation.
 * The physical console boot stays unchanged and no value is persisted. */
static inline uint64_t r2_profile_incarnation(uint64_t boot,uint32_t count){return boot^(UINT64_C(0x9e3779b97f4a7c15)*count);}
enum r2_runtime_profile {R2_PROFILE_GM,R2_PROFILE_RC,R2_PROFILE_TOGGLE};

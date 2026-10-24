/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Contemporary Klo -> HF -> Khi bracket. Kernel timestamps retain their own
 * lifetime semantics. This is a conservative permission conversion, never RF
 * evidence or a phase map. 200ppm is an unverified operating assumption. */
struct gm7_bridge {
  uint64_t kernel_lo, kernel_hi, hf;
  uint32_t epoch;
};
bool gm7_bridge_valid(struct gm7_bridge);
bool gm7_bridge_to_hf(struct gm7_bridge, uint64_t kernel_until,
                      uint64_t *hf_until);
bool gm7_bridge_to_kernel(struct gm7_bridge, uint64_t hf_until,
                          uint64_t *kernel_until);
bool gm7_bridge_past(struct gm7_bridge, uint64_t kernel_at, uint64_t *hf_at);

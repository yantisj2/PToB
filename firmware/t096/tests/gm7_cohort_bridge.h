/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Host-only ABI bridge; not a firmware wire or public transport. */
#pragma once
#include "radio_service.h"
#include "gm7_timing.h"
struct gm7_cohort_input {
  struct r2_enrollment path;
  struct r2_group_client anchor;
  struct r2_radio_scope scope;
  struct r2_source_clock source;
  uint64_t kernel, hf, resume_until, root, boot;
  int64_t utc_ns;
  uint32_t source_epoch, timing_epoch;
  bool source_ready, handoff;
};
struct gm7_cohort_output {
  struct r2_gm7_timing timing;
  bool closed, ready;
};

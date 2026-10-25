/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
/* Host-only handoff of actual service outputs to the real PA receive body. */
struct gm7_bootstrap_fixture {
  struct r2_enrollment roster;
  struct r2_group_client anchor;
  struct r2_radio_scope scope;
  uint64_t at;
};

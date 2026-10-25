/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
int r2_gm7_diagnostic_format(char *, size_t, unsigned page, const char *source,
                             uint64_t physical_boot, bool runtime_gm);

/* Initial GM boot only. Read published capture readiness; never requests HF,
 * advances capture ownership or treats fix/dwell as controller readiness. */
int r2_gm7_start_ready(void);
void r2_gm7_start_failed(int error);

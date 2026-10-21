/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 * Cleanup only: never extends source, message, lease or output authority.
 */
#pragma once
#include <stdint.h>
#define R2_CALLBACK_RETIRE_US UINT64_C(5000000)
#define R2_LE_SUPERVISION_MAX_US UINT64_C(32000000)
#define R2_CLEANUP_DISPATCH_US UINT64_C(3000000)
#define R2_CLEANUP_US (R2_LE_SUPERVISION_MAX_US + R2_CALLBACK_RETIRE_US + R2_CLEANUP_DISPATCH_US)
#define R2_CLEANUP_MS (R2_CLEANUP_US / 1000)

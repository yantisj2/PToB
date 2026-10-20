/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Ephemeral host-test mailbox: both executables use these exact C headers.
 * Not a device wire format, persistent evidence format or timing measurement. */
#pragma once
#include "group_admission.h"
#include "radio_precision.h"
struct gm5_handoff_fixture {
 struct r2_group_client accepted,promoted;
 struct r2_radio_scope selected;
 uint64_t at;
};

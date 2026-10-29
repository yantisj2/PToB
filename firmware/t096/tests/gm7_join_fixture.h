/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Test-only production service -> actual transport/JOIN boundary fixture.
 * Only dummy test enrollment; pointers are cleared before serialization. */
#pragma once
#include "group_admission.h"
struct gm7_join_fixture {
 uint32_t version;
 struct r2_group_config group;
 struct r2_gm7_admission_source source[5];
};

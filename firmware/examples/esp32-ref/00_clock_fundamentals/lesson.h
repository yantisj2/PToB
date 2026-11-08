// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_LESSON_H
#define ESP32_REF_LESSON_H

#include <stdbool.h>
#include <stdio.h>

// Run the same deterministic walkthrough on every host. No device APIs.
bool ref_run_clock_lesson(FILE *output);

#endif

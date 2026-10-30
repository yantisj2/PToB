// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
// Preserve selected GCC frame boundaries and names for retained .su evidence.
// Host fixtures keep their normal optimizer; no source/timing gate changes.
#if defined(ESP_PLATFORM) && !defined(__clang__)
#define W12_STACK_FRAME __attribute__((noinline,noclone))
#else
#define W12_STACK_FRAME
#endif

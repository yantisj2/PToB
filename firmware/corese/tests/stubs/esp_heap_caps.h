// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Host-only allocation adapter; native builds use the real IDF PSRAM allocator.
#pragma once
#include <cstdlib>
#define MALLOC_CAP_SPIRAM 1u
#define MALLOC_CAP_8BIT 2u
inline void* heap_caps_calloc(size_t count,size_t size,unsigned){return std::calloc(count,size);}

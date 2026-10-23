// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace v12::periodic_rx {struct Stamp;}
// Boot-configured domain. A fault never falls back into another live map.
bool n30_fine_backend_selected();
bool n30_fine_backend_start(uint64_t boot,uint64_t factory,uint64_t now_us);
bool n30_fine_backend_current(uint64_t* tick20);
// 1 means wait for the original search upper bound; 2 accepted; 0 refused.
// Only the raw falling capture supplies phase. Descriptor phase is discarded.
int n30_fine_backend_receive(v12::periodic_rx::Stamp&);

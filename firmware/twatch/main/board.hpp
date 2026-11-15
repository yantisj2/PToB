// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace watchboard {
bool idle();bool init();bool display_init();bool flush(const uint16_t*);bool brightness(uint8_t);bool sleep(bool);
bool touch(int&,int&);int battery();bool settings_load(void*,unsigned);bool settings_save(const void*,unsigned);
}

// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
namespace watch {struct RenderCache;}
namespace watchboard {
bool power_off(); // Main owner only, after UI and radio drain completes.
bool imu_bus_open();int imu_read(uint8_t,uint8_t*,unsigned);int imu_write(uint8_t,const uint8_t*,unsigned);
bool idle();bool init();bool display_init();bool flush(const uint16_t*,const watch::RenderCache* =nullptr);bool brightness(uint8_t);bool sleep(bool);
bool touch(int&,int&,bool* valid=nullptr);int battery();bool usb_power(bool&);bool settings_load(void*,unsigned);bool settings_save(const void*,unsigned);
}

// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
struct FakeLock {unsigned held=0;};
using esp_pm_lock_handle_t=FakeLock*;
constexpr int ESP_PM_CPU_FREQ_MAX=1;
struct esp_pm_config_t {int max_freq_mhz=0,min_freq_mhz=0;bool light_sleep_enable=false;};
int esp_pm_lock_create(int,int,const char*,esp_pm_lock_handle_t*);
int esp_pm_lock_acquire(esp_pm_lock_handle_t);
int esp_pm_lock_release(esp_pm_lock_handle_t);
int esp_pm_configure(const esp_pm_config_t*);

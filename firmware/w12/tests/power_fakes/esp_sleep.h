// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
enum {ESP_OK=0,ESP_FAIL=-1,ESP_ERR_INVALID_STATE=259,ESP_SLEEP_WAKEUP_ALL=0};
int esp_sleep_disable_wakeup_source(int);
int esp_sleep_enable_ext0_wakeup(int,int);
void esp_deep_sleep_start();

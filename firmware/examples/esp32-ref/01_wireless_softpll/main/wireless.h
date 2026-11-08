// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_WIRELESS_H
#define ESP32_REF_WIRELESS_H
#include "ref_display.h"
bool ref_wireless_begin(uint64_t identity);
void ref_wireless_snapshot(RefDisplayModel *model);
void ref_wireless_request_stop(void);
bool ref_wireless_stopped(void);
uint64_t ref_wireless_stopped_at_ms(void);
bool ref_wireless_stop_failed(void);
#endif

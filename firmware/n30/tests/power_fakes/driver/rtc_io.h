// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "gpio.h"
int rtc_gpio_pullup_en(gpio_num_t);
int rtc_gpio_pulldown_dis(gpio_num_t);
int rtc_gpio_deinit(gpio_num_t);

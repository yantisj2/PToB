// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
struct gpio_config_t {uint64_t pin_bit_mask;int mode;};
constexpr int GPIO_MODE_INPUT=1,GPIO_NUM_8=8,ESP_OK=0;
int gpio_config(const gpio_config_t*);int gpio_get_level(int);

// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
using gpio_num_t=int;
enum {GPIO_NUM_0=0,GPIO_NUM_3=3,GPIO_NUM_4=4,GPIO_NUM_12=12,GPIO_NUM_17=17,GPIO_NUM_18=18,GPIO_NUM_45=45,GPIO_NUM_47=47,GPIO_NUM_48=48,GPIO_MODE_OUTPUT=2};
struct gpio_config_t {uint64_t pin_bit_mask=0;int mode=0;};
int gpio_set_level(gpio_num_t,uint32_t);
int gpio_config(const gpio_config_t*);
int gpio_get_level(gpio_num_t);
int gpio_hold_en(gpio_num_t);
int gpio_hold_dis(gpio_num_t);
void gpio_deep_sleep_hold_en();
void gpio_deep_sleep_hold_dis();

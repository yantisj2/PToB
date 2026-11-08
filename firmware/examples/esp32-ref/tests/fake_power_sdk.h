// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef REF_FAKE_POWER_SDK_H
#define REF_FAKE_POWER_SDK_H
#include <stdint.h>
#define REF_W12_LORA_PRESENT 1
#define GPIO_NUM_0 0
#define ESP_OK 0
#define GPIO_MODE_INPUT 1
#define GPIO_MODE_OUTPUT 2
#define GPIO_PULLUP_ENABLE 1
#define ESP_SLEEP_WAKEUP_ALL 0
typedef struct { uint64_t pin_bit_mask; int mode, pull_up_en; } gpio_config_t;
int gpio_set_level(int,int); int gpio_config(const gpio_config_t *); int gpio_hold_dis(int);
int gpio_get_level(int); int rtc_gpio_deinit(int); int rtc_gpio_pullup_en(int); int rtc_gpio_pulldown_dis(int);
int esp_sleep_disable_wakeup_source(int); int esp_sleep_enable_ext0_wakeup(int,int); int gpio_hold_en(int);
void gpio_deep_sleep_hold_dis(void); void gpio_deep_sleep_hold_en(void); void esp_deep_sleep_start(void);
#endif

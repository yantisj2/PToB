// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "w12.h"
#ifdef REF_POWER_TEST
#include "fake_power_sdk.h"
#else
#include "ref_local.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
#endif

// Fixed Meshnology W12 board routes, independent of timing/protocol code.
// Prevent phantom powering: an unpowered OLED must not have high I2C pins.
static const struct { int pin, off; } PINS[] = {
    {45, 1}, {47, 0}, {17, 0}, {18, 0},
#if REF_W12_LORA_PRESENT
    {3, 0}, {12, 0}, {4, 0},
#endif
};

static bool off_level(int pin, int level)
{
    if (gpio_set_level(pin, level) != ESP_OK) return false;
    gpio_config_t config = {0};
    config.pin_bit_mask = UINT64_C(1) << pin;
    config.mode = GPIO_MODE_OUTPUT;
    return gpio_config(&config) == ESP_OK;
}

bool ref_w12_wake(void)
{
    gpio_deep_sleep_hold_dis();
    for (unsigned i = 0; i < sizeof(PINS) / sizeof(PINS[0]); ++i) {
        if (!off_level(PINS[i].pin, PINS[i].off) || gpio_hold_dis(PINS[i].pin) != ESP_OK) return false;
    }
    if (rtc_gpio_deinit(GPIO_NUM_0) != ESP_OK) return false;
    gpio_config_t button = {0};
    button.pin_bit_mask = 1;
    button.mode = GPIO_MODE_INPUT;
    button.pull_up_en = GPIO_PULLUP_ENABLE;
    return gpio_config(&button) == ESP_OK;
}

bool ref_w12_pressed(void) { return gpio_get_level(GPIO_NUM_0) == 0; }

bool ref_w12_sleep(void)
{
    if (ref_w12_pressed()) return false;
    if (esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL) != ESP_OK ||
        rtc_gpio_pullup_en(GPIO_NUM_0) != ESP_OK ||
        rtc_gpio_pulldown_dis(GPIO_NUM_0) != ESP_OK ||
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0) != ESP_OK) return false;
    for (unsigned i = 0; i < sizeof(PINS) / sizeof(PINS[0]); ++i) {
        if (!off_level(PINS[i].pin, PINS[i].off) || gpio_hold_en(PINS[i].pin) != ESP_OK) return false;
    }
    gpio_deep_sleep_hold_en();
    if (ref_w12_pressed()) return false; // Catch a new press during preparation.
    esp_deep_sleep_start();
    return false; // A returning SDK call is never reported as completed sleep.
}

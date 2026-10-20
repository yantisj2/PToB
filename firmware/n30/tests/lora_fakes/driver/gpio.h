// Synthetic host API boundary; no hardware access. MIT, Jonathan Yantis.
#pragma once
#include <cstdint>
using gpio_num_t=int;using esp_err_t=int;
constexpr int ESP_OK=0,GPIO_MODE_INPUT=1,GPIO_MODE_OUTPUT=2,GPIO_MODE_INPUT_OUTPUT=3;
constexpr int GPIO_NUM_8=8,GPIO_NUM_12=12,GPIO_NUM_13=13,GPIO_NUM_14=14;
struct gpio_config_t{uint64_t pin_bit_mask=0;int mode=0;};
esp_err_t gpio_config(const gpio_config_t*);
esp_err_t gpio_set_level(gpio_num_t,int);
int gpio_get_level(gpio_num_t);

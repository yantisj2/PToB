// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "n30_power.hpp"
#include "board.hpp"
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "n30_display.hpp"
namespace n30 {
int power_wake_init(){return watchboard::init()?ESP_OK:ESP_FAIL;}
int power_off(){
 if(!n30_display_power_ready())return ESP_ERR_INVALID_STATE;
 if(!watchboard::power_off())return ESP_FAIL;
 vTaskDelay(pdMS_TO_TICKS(250));
 return ESP_ERR_TIMEOUT; // A successful rail shutdown never returns. Never reset.
}
}

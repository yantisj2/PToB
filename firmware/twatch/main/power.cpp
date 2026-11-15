// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "n30_power.hpp"
#include "board.hpp"
#include "esp_err.h"
namespace n30 {
int power_wake_init(){return watchboard::init()?ESP_OK:ESP_FAIL;}
// Revision one offers display sleep only. No inherited N30 rail shutdown.
int power_off(){return ESP_ERR_NOT_SUPPORTED;}
}

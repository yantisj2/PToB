// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "power_off.hpp"
#include "w12_board_profile.hpp"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "esp_sleep.h"
namespace {
static_assert(w12_board::profile==2||w12_board::profile==12,"Power control requires the Meshnology W12 route");
struct Pin {gpio_num_t gpio;uint8_t off;};
constexpr Pin pins[]={
 {GPIO_NUM_45,1},{GPIO_NUM_47,0},{GPIO_NUM_17,0},{GPIO_NUM_18,0},
#if W12_GNSS_W12_POWER
 {GPIO_NUM_48,1},
#endif
#ifdef PTOB_W12_LORA_RX
 {GPIO_NUM_12,0},{GPIO_NUM_3,0},{GPIO_NUM_4,0},
#endif
};
int off_level(const Pin& pin){
 int rc=gpio_set_level(pin.gpio,pin.off);if(rc)return rc;
 gpio_config_t cfg{};cfg.pin_bit_mask=1ULL<<pin.gpio;cfg.mode=GPIO_MODE_OUTPUT;
 return gpio_config(&cfg);
}
}
namespace w12 {
int power_wake_init(){
 // Remove retained digital holds before the normal peripheral initialization.
 // Stage the same safe levels before releasing each pad to avoid a rail pulse.
 gpio_deep_sleep_hold_dis();
 for(const auto& pin:pins){int rc=off_level(pin);if(rc)return rc;rc=gpio_hold_dis(pin.gpio);if(rc)return rc;}
 return rtc_gpio_deinit(GPIO_NUM_0);
}
int power_off(){
 // Called only after all application owners acknowledge the original drain.
 if(gpio_get_level(GPIO_NUM_0)==0)return ESP_ERR_INVALID_STATE;
 int rc=esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);if(rc)return rc;
 if((rc=rtc_gpio_pullup_en(GPIO_NUM_0))||(rc=rtc_gpio_pulldown_dis(GPIO_NUM_0))||
    (rc=esp_sleep_enable_ext0_wakeup(GPIO_NUM_0,0)))return rc;
 for(const auto& pin:pins){rc=off_level(pin);if(rc)return rc;rc=gpio_hold_en(pin.gpio);if(rc)return rc;}
 gpio_deep_sleep_hold_en();
 // Catch a new press during rail preparation; never enter with wake active.
 if(gpio_get_level(GPIO_NUM_0)==0)return ESP_ERR_INVALID_STATE;
 esp_deep_sleep_start();
 return ESP_FAIL; // A returning sleep API is a fault, never a completed shutdown.
}
}

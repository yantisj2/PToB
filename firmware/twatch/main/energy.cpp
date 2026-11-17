// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "energy.hpp"
#include "esp_pm.h"
#include "esp_private/esp_clk.h"
#include <atomic>
namespace watch::energy {
namespace {
esp_pm_lock_handle_t ble_lock=nullptr,display_lock=nullptr;
std::atomic<bool> ready{false},ble_held{false},display_held{false},vbus{false};
std::atomic<int> first_error{0};
bool checked(int rc){if(rc){int empty=0;first_error.compare_exchange_strong(empty,rc);}return rc==0;}
bool set(esp_pm_lock_handle_t lock,std::atomic<bool>& held,bool required){
 // Each handle has a single runtime owner; init hands them over before tasks start.
 if(!lock)return false;
 if(held.load()==required)return true;
 if(!checked(required?esp_pm_lock_acquire(lock):esp_pm_lock_release(lock)))return false;
 held.store(required);return true;
}
}
bool init(){
 if(ready.load())return true;
 if(!checked(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"watch_ble",&ble_lock))||
    !checked(esp_pm_lock_create(ESP_PM_CPU_FREQ_MAX,0,"watch_panel",&display_lock))||
    !set(ble_lock,ble_held,true)||!set(display_lock,display_held,true))return false;
 esp_pm_config_t config{};config.max_freq_mhz=240;config.min_freq_mhz=80;config.light_sleep_enable=false;
 if(!checked(esp_pm_configure(&config)))return false;
 ready.store(true);return true;
}
bool ble(bool required){return ready.load()&&set(ble_lock,ble_held,required);}
bool display(bool required){return ready.load()&&set(display_lock,display_held,required);}
void usb(bool present){vbus.store(present);}
bool usb(){return vbus.load();}
Status status(){return {uint32_t(esp_clk_cpu_freq()/1000000),first_error.load(),ready.load(),ble_held.load(),display_held.load(),vbus.load()};}
}

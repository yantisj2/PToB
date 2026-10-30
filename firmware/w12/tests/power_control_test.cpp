// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "power_off.hpp"
#include "driver/gpio.h"
#include "esp_sleep.h"
#include <cassert>
#include <cstdio>
#include <vector>
struct Call {int kind,pin,level;};
std::vector<Call> calls;int nth=0,fail=0,reads=0,pressed_read=0;bool returning=false;
struct Slept{};
int record(int kind,int pin=0,int level=0){calls.push_back({kind,pin,level});return ++nth==fail?-99:0;}
int gpio_set_level(int pin,uint32_t level){return record(1,pin,int(level));}
int gpio_config(const gpio_config_t* cfg){assert(cfg->mode==GPIO_MODE_OUTPUT);assert(cfg->pin_bit_mask&&!(cfg->pin_bit_mask&(cfg->pin_bit_mask-1)));return record(2,__builtin_ctzll(cfg->pin_bit_mask));}
int gpio_get_level(int pin){assert(pin==0);return ++reads==pressed_read?0:1;}
int gpio_hold_en(int pin){return record(3,pin);}
int gpio_hold_dis(int pin){return record(4,pin);}
void gpio_deep_sleep_hold_en(){record(5);}
void gpio_deep_sleep_hold_dis(){record(6);}
int rtc_gpio_pullup_en(int pin){assert(pin==0);return record(7);}
int rtc_gpio_pulldown_dis(int pin){assert(pin==0);return record(8);}
int rtc_gpio_deinit(int pin){assert(pin==0);return record(9);}
int esp_sleep_disable_wakeup_source(int source){assert(source==ESP_SLEEP_WAKEUP_ALL);return record(10);}
int esp_sleep_enable_ext0_wakeup(int pin,int level){assert(pin==0&&level==0);return record(11);}
void esp_deep_sleep_start(){record(12);if(!returning)throw Slept{};}
void reset(){calls.clear();nth=fail=reads=pressed_read=0;returning=false;}
int main(){
 const std::vector<int> pins={45,47,17,18,
#if W12_GNSS_W12_POWER
 48,
#endif
#ifdef PTOB_W12_LORA_RX
 12,3,4,
#endif
 };
 assert(w12::power_wake_init()==0);assert(calls.front().kind==6&&calls.back().kind==9);
 for(unsigned i=0;i<pins.size();i++){const auto at=1+3*i;assert(calls[at].kind==1&&calls[at].pin==pins[i]);assert(calls[at+1].kind==2&&calls[at+2].kind==4);}
 const int wake_steps=nth;
 for(int step=2;step<=wake_steps;step++){reset();fail=step;assert(w12::power_wake_init()==-99);assert(nth==step);}
 reset();try{w12::power_off();assert(false);}catch(Slept&){}
 assert(calls[0].kind==10&&calls[1].kind==7&&calls[2].kind==8&&calls[3].kind==11);
 for(unsigned i=0;i<pins.size();i++){
  const auto at=4+3*i;assert(calls[at].kind==1&&calls[at].pin==pins[i]);
  assert(calls[at].level==int(pins[i]==45||pins[i]==48));assert(calls[at+1].kind==2&&calls[at+2].kind==3);
 }
 assert(calls[calls.size()-2].kind==5&&calls.back().kind==12);
 const int setup_steps=nth-2;
 for(int step=1;step<=setup_steps;step++){reset();fail=step;assert(w12::power_off()==-99);assert(nth==step);}
 for(int read:{1,2}){reset();pressed_read=read;assert(w12::power_off()==ESP_ERR_INVALID_STATE);for(auto c:calls)assert(c.kind!=12);}
 reset();returning=true;assert(w12::power_off()==ESP_FAIL);
 std::puts("PASS actual W12 off/wake rails, GPIO0 wake, every checked API failure and held/new-press refusal; no hardware");
}

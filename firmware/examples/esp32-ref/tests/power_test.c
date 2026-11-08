// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "w12.h"
#include "fake_power_sdk.h"
#include <stdio.h>
#include <string.h>
static int level[49], held[49], calls, fail_at, reads, press_at, sleep_calls;
static int result(void) { return ++calls==fail_at ? -1 : 0; }
int gpio_set_level(int pin,int value) { level[pin]=value;return result(); }
int gpio_config(const gpio_config_t *p) { (void)p;return result(); }
int gpio_hold_dis(int pin) { held[pin]=0;return result(); }
int gpio_hold_en(int pin) { held[pin]=1;return result(); }
int gpio_get_level(int pin) { (void)pin;return ++reads==press_at ? 0 : 1; }
int rtc_gpio_deinit(int p) { (void)p;return result(); }
int rtc_gpio_pullup_en(int p) { (void)p;return result(); }
int rtc_gpio_pulldown_dis(int p) { (void)p;return result(); }
int esp_sleep_disable_wakeup_source(int p) { (void)p;return result(); }
int esp_sleep_enable_ext0_wakeup(int p,int l) { if(p||l)return -1;return result(); }
void gpio_deep_sleep_hold_dis(void) { memset(held,0,sizeof(held)); }
void gpio_deep_sleep_hold_en(void) {}
void esp_deep_sleep_start(void) { ++sleep_calls; }
#define CHECK(c) do { if(!(c)){fprintf(stderr,"power failure line %d\n",__LINE__);return 1;} } while(0)
int main(void)
{
    CHECK(ref_w12_wake()); calls=reads=0;
    CHECK(!ref_w12_sleep()); // A simulated returning sleep API is failure.
    CHECK(sleep_calls==1 && level[45]==1 && level[47]==0 && level[17]==0 && level[18]==0);
    CHECK(level[3]==0 && level[12]==0 && level[4]==0 && held[45] && held[17]);
    const int successful_calls=calls;
    for(int failure=1;failure<=successful_calls;++failure) {
        calls=reads=sleep_calls=0;fail_at=failure;
        CHECK(!ref_w12_sleep() && sleep_calls==0);
    }
    fail_at=0;
    for(int press=1;press<=2;++press) {
        calls=reads=sleep_calls=0;press_at=press;
        CHECK(!ref_w12_sleep() && sleep_calls==0);
    }
    puts("PASS: real W12 power code with SDK fakes; safe rails, holds, returning sleep, every API refusal and late button press");
    return 0;
}

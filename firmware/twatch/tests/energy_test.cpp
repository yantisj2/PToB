// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include <cassert>
#include <cstdio>
#include <vector>
#include "energy.hpp"
#include "esp_pm.h"
#include "panel_sleep.hpp"
static FakeLock locks[2];static unsigned created=0;static int fail=0;
static esp_pm_config_t config{};
int esp_pm_lock_create(int type,int,const char*,esp_pm_lock_handle_t* out){assert(type==ESP_PM_CPU_FREQ_MAX&&created<2);if(fail==1)return 71;*out=&locks[created++];return 0;}
int esp_pm_lock_acquire(esp_pm_lock_handle_t lock){if(fail==2)return 72;++lock->held;assert(lock->held==1);return 0;}
int esp_pm_lock_release(esp_pm_lock_handle_t lock){if(fail==3)return 73;assert(lock->held==1);--lock->held;return 0;}
int esp_pm_configure(const esp_pm_config_t* c){if(fail==4)return 74;config=*c;return 0;}
int esp_clk_cpu_freq(){return (locks[0].held||locks[1].held?240:80)*1000000;}
int main(int argc,char** argv){
 if(argc==2){fail=argv[1][0]-'0';assert(!watch::energy::init());assert(!watch::energy::status().ready);assert(watch::energy::status().error==70+fail);return 0;}
 using namespace watch::energy;
 assert(init()&&init()&&created==2);assert(config.min_freq_mhz==80&&config.max_freq_mhz==240&&!config.light_sleep_enable);
 assert(status().ble&&status().display&&status().cpu_mhz==240);
 assert(display(false)&&display(false)&&status().cpu_mhz==240);
 assert(ble(false)&&ble(false)&&status().cpu_mhz==80);
 for(unsigned i=0;i<1000;i++){
  assert(ble(true)&&status().cpu_mhz==240);assert(display(true));
  assert(ble(false)&&status().cpu_mhz==240);assert(display(false)&&status().cpu_mhz==80);
 }
 fail=2;assert(!ble(true)&&!status().ble&&status().error==72);fail=0;assert(ble(true));
 fail=3;assert(!ble(false)&&status().ble&&status().cpu_mhz==240);fail=0;assert(ble(false));
 usb(true);assert(usb());usb(false);assert(!usb());
 std::vector<int> sequence;int reject=-1;
 auto cmd=[&](unsigned c){sequence.push_back(c);return int(c)!=reject;};
 auto enable=[&](bool on){sequence.push_back(on?1001:1000);return reject!=int(on?1001:1000);};
 auto delay=[&](unsigned ms){sequence.push_back(2000+ms);};
 assert(watch::PanelSleep::sleep(true,cmd,enable,delay));assert((sequence==std::vector<int>{0x28,0x10,2120,1000}));
 sequence.clear();assert(watch::PanelSleep::sleep(false,cmd,enable,delay));assert((sequence==std::vector<int>{1001,2010,0x11,2120}));
 for(int failure:{0x28,0x10,1000,1001,0x11}){sequence.clear();reject=failure;const bool asleep=(failure<1000&&failure!=0x11)||failure==1000;assert(!watch::PanelSleep::sleep(asleep,cmd,enable,delay));assert(sequence.back()==failure);}
 std::puts("PASS actual CPU owner locks/failures/1000 restarts and panel Sleep In/Out/failure ordering; no counter or touch power API");
}

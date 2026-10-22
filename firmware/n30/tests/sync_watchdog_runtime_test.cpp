// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Actual supervisor/reset/RTC path with synthetic FreeRTOS and restart boundaries.
#include <cassert>
#include <cstdio>
#include "freertos/task.h"
#include "esp_system.h"
namespace fake {uint64_t us=0;unsigned restarts=0;int reset=ESP_RST_POWERON;struct Yield{};struct Restart{};}
int64_t esp_timer_get_time(){return int64_t(fake::us);}
int esp_reset_reason(){return fake::reset;}
void esp_restart(){++fake::restarts;throw fake::Restart{};}
void vTaskDelay(unsigned ms){assert(ms==100);throw fake::Yield{};}
TaskHandle_t xTaskCreateStaticPinnedToCore(void(*)(void*),const char*,unsigned bytes,void*,unsigned priority,StackType_t*,StaticTask_t*,int core){assert(bytes==3072&&priority==2&&core==1);return reinterpret_cast<void*>(1);}
#define V12_SOURCE_REVISION "0000000000000000000000000000000000000000"
#define V12_BUILD_ID "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
#include "../../w12/main/fault_watchdog.cpp"
namespace {
const uint8_t factory[6]={2,0,0,0,0,1};
constexpr uint64_t D=300000000;
void cold(){
 v12::input={};v12::model={};v12::view={};v12::intent={};fake::us=0;fake::restarts=0;fake::reset=ESP_RST_POWERON;
#ifdef PTOB_N30
 v12::sync_model={};v12::last_sync_observed_us=0;
#endif
 assert(v12::fault_watchdog_start(factory,1));
}
void tick(uint64_t now,bool expected,uint64_t until,uint32_t mask=0){
 fake::us=now;v12::fault_watchdog_main_progress(mask,0,0,777,true,true);v12::fault_watchdog_host_progress();
#ifdef PTOB_N30
 v12::fault_watchdog_sync_progress(expected,until);
#else
 (void)expected;(void)until;
#endif
 try{v12::supervise(nullptr);}catch(const fake::Yield&){}catch(const fake::Restart&){}
}
}
int main(){
#ifdef PTOB_N30
 cold();tick(0,true,0);tick(120000000,true,0);assert(!fake::restarts);
 tick(D-1,true,0);assert(!fake::restarts);tick(D,true,0);assert(fake::restarts==1);
 assert(v12::intent.mask==v12::FaultNoTimeSync&&v12::intent.elapsed==D&&v12::intent.parent==777);
 // The existing RTC evidence/one-shot explicit-parent restoration survives a
 // no-time reset without restoring a model, permission or former GM cache.
 v12::view={};v12::input={};v12::model={};v12::sync_model={};v12::last_sync_observed_us=0;fake::reset=ESP_RST_SW;fake::us=0;
 assert(v12::fault_watchdog_start(factory,2));auto previous=v12::fault_watchdog_status();
 assert(previous.previous_boot==1&&previous.previous_mask==v12::FaultNoTimeSync&&previous.previous_elapsed_us==D&&previous.parent_pending);
 cold();tick(0,true,0);tick(D-1,true,D+1000000);assert(!fake::restarts);
 tick(D+1000000,true,0);tick(2*D+999999,true,0);assert(!fake::restarts);tick(2*D+1000000,true,0);assert(fake::restarts==1);
 // Valid time arriving at an untaken due boundary wins over reboot.
 cold();tick(0,true,0);tick(D,true,D+1);assert(!fake::restarts);tick(D+1,true,0);tick(2*D,true,0);assert(!fake::restarts);
 tick(2*D+1,true,0);assert(fake::restarts==1);
 // A brief genuine time observation between supervisor wakes also breaks
 // continuous loss, even when its deadline has expired at the next wake.
 cold();tick(0,true,0);tick(D-1,true,0);v12::fault_watchdog_sync_progress(true,D);
 tick(D,true,0);assert(!fake::restarts);tick(2*D-1,true,0);assert(!fake::restarts);
 tick(2*D,true,0);assert(fake::restarts==1);
 // Explicit OFF/maintenance cancels the interval and rearming requires300s.
 cold();tick(0,true,0);tick(D-1,false,0);tick(2*D,false,0);assert(!fake::restarts);
 tick(2*D+1,true,0);tick(3*D,true,0);assert(!fake::restarts);tick(3*D+1,true,0);assert(fake::restarts==1);
 cold();tick(100,true,0);tick(99,true,0);tick(D+100,true,0);assert(!fake::restarts);
 cold();tick(uint64_t(INT64_MAX)-D,true,0);tick(uint64_t(INT64_MAX)-1,true,0);assert(!fake::restarts);tick(uint64_t(INT64_MAX),true,0);assert(fake::restarts==1);
#else
 cold();tick(0,true,0);tick(10*D,true,0);assert(!fake::restarts&&v12::fault_watchdog_status().mask==0);
#endif
 // Original actual-fault supervision is still120s, independently of no time.
 cold();tick(0,true,0,v12::FaultR2);tick(119999999,true,0,v12::FaultR2);assert(!fake::restarts);
 tick(120000000,true,0,v12::FaultR2);assert(fake::restarts==1&&v12::intent.first_mask==v12::FaultR2&&v12::intent.elapsed==120000000);
 std::puts("PASS actual supervisor/reset/RTC: N30 full300s no-time, recovery/OFF/rollback/overflow and unchanged120s fault/default W12");
}

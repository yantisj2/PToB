// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Actual supervisor/reset/RTC path with synthetic FreeRTOS and restart boundaries.
#include <cassert>
#include <cstdio>
#include "freertos/task.h"
#include "esp_system.h"
#include "../main/no_time_retention.hpp"
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
#ifdef PTOB_N30
constexpr uint64_t D=720000000;
static_assert(V12_NO_TIME_SYNC_US==D);
#else
constexpr uint64_t D=300000000;
#endif
void cold(){
 v12::input={};v12::model={};v12::view={};v12::intent={};fake::us=0;fake::restarts=0;fake::reset=ESP_RST_POWERON;
#if defined(PTOB_N30) || defined(PTOB_W12_SYNC_WATCHDOG)
 v12::sync_model={};v12::last_sync_observed_us=0;
#endif
 assert(v12::fault_watchdog_start(factory,1));
}
void tick(uint64_t now,bool expected,uint64_t until,uint32_t mask=0){
 fake::us=now;v12::fault_watchdog_main_progress(mask,0,0,777,true,true);v12::fault_watchdog_host_progress();
#if defined(PTOB_N30) || defined(PTOB_W12_SYNC_WATCHDOG)
 v12::fault_watchdog_sync_progress(expected,until);
#else
 (void)expected;(void)until;
#endif
 try{v12::supervise(nullptr);}catch(const fake::Yield&){}catch(const fake::Restart&){}
}
}
int main(){
#if defined(PTOB_W12_SYNC_WATCHDOG) && !defined(PTOB_N30)
 cold();tick(0,true,0);tick(D-1,true,0);assert(!fake::restarts);tick(D,true,0);assert(fake::restarts==1&&v12::intent.mask==v12::FaultNoTimeSync&&v12::intent.elapsed==300000000);
#elif defined(PTOB_N30)
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
 // Explicit OFF/maintenance cancels the interval and rearming requires720s.
 cold();tick(0,true,0);tick(D-1,false,0);tick(2*D,false,0);assert(!fake::restarts);
 tick(2*D+1,true,0);tick(3*D,true,0);assert(!fake::restarts);tick(3*D+1,true,0);assert(fake::restarts==1);
 // Healthy deliberate BLE OFF never accrues a no-time deadline, whether
 // LoRa alternates between fresh UTC and unavailable. Main/host still advance.
 cold();tick(0,true,0);tick(D-1,false,0);
 for(uint64_t at=D;at<=10*D;at+=100000){tick(at,false,(at/100000)%2?at+250000:0);assert(!fake::restarts&&!v12::sync_model.active()&&!(v12::fault_watchdog_status().mask&v12::FaultNoTimeSync));}
 cold();tick(100,true,0);tick(99,true,0);tick(D+100,true,0);assert(!fake::restarts);
 cold();tick(uint64_t(INT64_MAX)-D,true,0);tick(uint64_t(INT64_MAX)-1,true,0);assert(!fake::restarts);tick(uint64_t(INT64_MAX),true,0);assert(fake::restarts==1);
 // Actual N30 retention policy cancels the supervisor's destructive absence
 // timer, without supplying UTC or changing independent actual-fault timing.
 cold();n30::NoTimeRetention retention;n30_lora_view cache{};
 tick(0,retention.expected(true,false,cache),0);cache.metadata_cached=true;
 tick(D-1,retention.expected(true,false,cache),0);cache={};
 for(uint64_t at=D;at<=12*D;at+=1000000)tick(at,retention.expected(true,false,cache),0);
 assert(!fake::restarts&&!(v12::fault_watchdog_status().mask&v12::FaultNoTimeSync));
 tick(12*D+1,retention.expected(true,false,cache),0,v12::FaultLoRa);
 tick(12*D+120000000,retention.expected(true,false,cache),0,v12::FaultLoRa);assert(!fake::restarts);
 tick(12*D+120000001,retention.expected(true,false,cache),0,v12::FaultLoRa);assert(fake::restarts==1&&v12::intent.first_mask==v12::FaultLoRa);
 // Boot just after META and miss further META. Healthy listening is ordinary
 // acquisition, not a reason to reboot before the next authenticated control.
 cold();n30::NoTimeRetention acquisition;n30_lora_view receiver{};receiver.requested=receiver.active=true;
 for(uint64_t at=0;at<=5*D;at+=100000){receiver.sampled_us=at;tick(at,acquisition.expected(true,false,receiver,at),0);assert(!fake::restarts);}
 receiver.metadata_cached=true;receiver.sampled_us=5*D+1;tick(5*D+1,acquisition.expected(true,false,receiver,5*D+1),0);assert(!fake::restarts);
 n30::NoTimeRetention stale;receiver={};receiver.requested=receiver.active=true;receiver.sampled_us=1;
 assert(stale.expected(true,false,receiver,250001));receiver.sampled_us=250001;receiver.fault=true;assert(stale.expected(true,false,receiver,250001));
#else
 cold();tick(0,true,0);tick(10*D,true,0);assert(!fake::restarts&&v12::fault_watchdog_status().mask==0);
#endif
 // Original actual-fault supervision is still120s, independently of no time.
 cold();tick(0,true,0,v12::FaultR2);tick(119999999,true,0,v12::FaultR2);assert(!fake::restarts);
 tick(120000000,true,0,v12::FaultR2);assert(fake::restarts==1&&v12::intent.first_mask==v12::FaultR2&&v12::intent.elapsed==120000000);
 std::puts("PASS actual supervisor/reset/RTC: N30 full720s no-time / W12 default300s, recovery/OFF/rollback/overflow and unchanged120s fault/default W12");
}

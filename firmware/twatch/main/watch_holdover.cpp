// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "watch_holdover.hpp"
#include "freertos/FreeRTOS.h"
#include "esp_timer.h"
namespace watch {
namespace {
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
HoldoverMonitor monitor;
constexpr unsigned QueueSize=16;
v12::n30_probe::Pair queued[QueueSize];
uint8_t head=0,count=0,peak=0;
uint32_t dropped=0,drained=0,work_max_us=0;
bool overflow=false;
v12::n30_probe::PairSink previous_pair=nullptr;
bool initialized=false;
void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
void pair(const v12::n30_probe::Pair& value){
 // Preserve N30 timing/window diagnostics; observer ownership remains local.
 if(previous_pair)previous_pair(value);
 // BLE callback does a bounded copy only. All frozen-clock projections and
 // quantile accounting run later in the existing main task, without masking
 // interrupts or holding this producer lock.
 portENTER_CRITICAL(&mux);
 if(count==QueueSize){overflow=true;inc(dropped);}
 else{queued[(head+count)%QueueSize]=value;++count;if(count>peak)peak=count;}
 portEXIT_CRITICAL(&mux);
}
}
void holdover_init(){
 // Called once after timing diagnostics initialization, before BLE starts.
 if(initialized)return;
 previous_pair=v12::n30_probe::pair_sink;v12::n30_probe::pair_sink=pair;initialized=true;
}
HoldoverView holdover_update(const v12::R2Status& status,uint64_t now,bool parked){
 const auto begin=uint64_t(esp_timer_get_time());
 portENTER_CRITICAL(&mux);const auto pending=count;const bool lost=overflow;
 if(lost){for(unsigned i=0;i<pending;++i)inc(dropped);head=count=0;}
 overflow=false;
 portEXIT_CRITICAL(&mux);
 if(lost)monitor.missing(); // Never score across a lost queue interval.
 else for(unsigned i=0;i<pending;++i){
  portENTER_CRITICAL(&mux);const auto value=queued[head];head=uint8_t((head+1)%QueueSize);--count;portEXIT_CRITICAL(&mux);
  monitor.pair(value);inc(drained);
 }
 const auto result=monitor.update(status,now,parked);
 const auto end=uint64_t(esp_timer_get_time());
 if(end>=begin)work_max_us=std::max(work_max_us,uint32_t(std::min<uint64_t>(UINT32_MAX,end-begin)));
 return result;
}
HoldoverStatus holdover_status(){
 // Main-task reader only; the BLE callback touches the queue, never monitor.
 auto result=monitor.status();result.drained=drained;result.work_max_us=work_max_us;
 portENTER_CRITICAL(&mux);result.dropped=dropped;result.queued=count;result.peak=peak;portEXIT_CRITICAL(&mux);
 return result;
}
}

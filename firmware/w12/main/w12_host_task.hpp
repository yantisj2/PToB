// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
namespace w12 {
// Reserve the full existing host stack before peripheral heap fragmentation.
// ESP-IDF Xtensa stack depths are bytes; never shrink the configured budget.
template<unsigned Bytes> class HostTask {
  static_assert(sizeof(StackType_t)==1,"ESP-IDF byte-sized stack units required");
  alignas(16) StackType_t stack_[Bytes]{};
  StaticTask_t control_{};TaskHandle_t handle_=nullptr;bool used_=false;
 public:
  bool start(TaskFunction_t entry,unsigned priority,int core){
    if(used_||!entry)return false;
    handle_=xTaskCreateStaticPinnedToCore(entry,"nimble_host",Bytes,nullptr,priority,stack_,&control_,core);
    if(!handle_)return false;
    used_=true;return true;
  }
  void exited(){handle_=nullptr;vTaskDelete(nullptr);}
};
}

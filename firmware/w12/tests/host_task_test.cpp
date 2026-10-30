// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include <cassert>
#include <cstdint>
#include <cstdio>
using StackType_t=uint8_t;struct StaticTask_t {unsigned state;};using TaskHandle_t=void*;using TaskFunction_t=void(*)(void*);
static bool fail=false;static unsigned calls=0,depth=0,priority_seen=0;static int core_seen=-1;static StackType_t* seen_stack=nullptr;
TaskHandle_t xTaskCreateStaticPinnedToCore(TaskFunction_t f,const char*,unsigned bytes,void*,unsigned priority,StackType_t* stack,StaticTask_t* tcb,int core){assert(f&&stack&&tcb);++calls;depth=bytes;priority_seen=priority;core_seen=core;seen_stack=stack;return fail?nullptr:tcb;}
void vTaskDelete(void*){}
#include "../main/w12_host_task.hpp"
static void entry(void*){}
int main(){w12::HostTask<8192> task;assert(!task.start(nullptr,21,0)&&calls==0);fail=true;assert(!task.start(entry,21,0));fail=false;assert(task.start(entry,21,0));assert(depth==8192&&priority_seen==21&&core_seen==0&&reinterpret_cast<uintptr_t>(seen_stack)%16==0);const auto n=calls;assert(!task.start(entry,21,0)&&calls==n);task.exited();assert(!task.start(entry,21,0));std::puts("PASS full static host stack, exact scheduling, failed creation and single lifetime");}

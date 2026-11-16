#pragma once
#include "FreeRTOS.h"
extern void (*fake_worker)(void*);
inline void* xTaskCreateStaticPinnedToCore(void(*fn)(void*),const char*,unsigned,void*,int,StackType_t*,StaticTask_t*,int){fake_worker=fn;return reinterpret_cast<void*>(1);}
void vTaskDelay(unsigned);

using TaskHandle_t=void*;
#define pdTRUE 1
inline void xTaskNotifyGive(TaskHandle_t){}
inline unsigned ulTaskNotifyTake(int,unsigned ticks){vTaskDelay(ticks);return 0;}

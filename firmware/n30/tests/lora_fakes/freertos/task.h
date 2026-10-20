// Synthetic host API boundary; no hardware access. MIT, Jonathan Yantis.
#pragma once
#include "FreeRTOS.h"
using TaskHandle_t=void*;
void vTaskDelay(TickType_t);
uint32_t ulTaskNotifyTake(BaseType_t,TickType_t);
void xTaskNotifyGive(TaskHandle_t);
void vTaskNotifyGiveFromISR(TaskHandle_t,BaseType_t*);
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t);
BaseType_t xTaskCreatePinnedToCore(void(*)(void*),const char*,uint32_t,void*,unsigned,TaskHandle_t*,int);

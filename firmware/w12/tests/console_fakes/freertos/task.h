#pragma once
using TaskHandle_t=void*;
void vTaskDelay(unsigned);
inline int xTaskCreatePinnedToCore(void(*)(void*),const char*,unsigned,void*,unsigned,TaskHandle_t*,unsigned){return 1;}

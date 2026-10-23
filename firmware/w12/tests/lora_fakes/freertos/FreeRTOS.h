// Synthetic host API boundary; no hardware access. MIT, Jonathan Yantis.
#pragma once
#include <cstdint>
using BaseType_t=int;using TickType_t=uint32_t;using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define portENTER_CRITICAL_ISR(p) ((void)(p))
#define portEXIT_CRITICAL_ISR(p) ((void)(p))
#define pdMS_TO_TICKS(v) (v)
constexpr int pdFALSE=0,pdTRUE=1,pdPASS=1;

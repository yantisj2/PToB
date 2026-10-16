#pragma once
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#ifdef V12_TEST_CRITICAL_ENTER
#define portENTER_CRITICAL(x) V12_TEST_CRITICAL_ENTER(x)
#else
#define portENTER_CRITICAL(x) ((void)(x))
#endif
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
#define pdMS_TO_TICKS(x) (x)
#define pdPASS 1
inline bool xPortInIsrContext(){return false;}

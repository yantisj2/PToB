// Synthetic host API boundary; no hardware access. MIT, Jonathan Yantis.
#pragma once
#include <cstdint>
#include <cassert>
using BaseType_t=int;using TickType_t=uint32_t;using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
// Deterministic cross-core handoff at the outer critical-section boundary.
namespace test_critical {inline unsigned depth=0,after_n=0;inline void(*after_exit)()=nullptr;
inline void enter(){++depth;}
inline void leave(){assert(depth);--depth;if(!depth&&after_exit){if(after_n){--after_n;return;}auto run=after_exit;after_exit=nullptr;run();}}}
#define portENTER_CRITICAL(p) ((void)(p),test_critical::enter())
#define portEXIT_CRITICAL(p) ((void)(p),test_critical::leave())
#define portENTER_CRITICAL_ISR(p) portENTER_CRITICAL(p)
#define portEXIT_CRITICAL_ISR(p) portEXIT_CRITICAL(p)
#define pdMS_TO_TICKS(v) (v)
constexpr int pdFALSE=0,pdTRUE=1,pdPASS=1;

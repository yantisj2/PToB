#pragma once
#include <cstdint>
extern uint64_t fake_now;
inline int64_t esp_timer_get_time(){return fake_now;}

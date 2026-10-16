#pragma once
#include <cstdint>
extern uint64_t fake_now;
inline int64_t esp_timer_get_time(){
#ifdef FAKE_ESP_TIMER_READ
  return FAKE_ESP_TIMER_READ();
#else
  return int64_t(fake_now);
#endif
}

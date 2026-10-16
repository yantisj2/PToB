#pragma once
#include <cstdint>
#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/portmacro.h"
#else
#include <mutex>
#endif
namespace v12 {
// Single owner publishes; readers pin one complete version. All state here
// stays internal. Caller-owned value slots may be in PSRAM. Only tiny index/pin
// updates hold this lock; value copies never mask interrupts or wait for readers.
class TdPublication {
 static constexpr uint32_t Writing=UINT32_MAX;
#ifdef ESP_PLATFORM
 mutable portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
 void enter()const{portENTER_CRITICAL(&mux);}void leave()const{portEXIT_CRITICAL(&mux);}
#else
 mutable std::mutex mux;
 void enter()const{mux.lock();}void leave()const{mux.unlock();}
#endif
 struct Guard{const TdPublication& p;explicit Guard(const TdPublication& value):p(value){p.enter();}~Guard(){p.leave();}};
 uint32_t current=0,pins[3]{},read_misses=0,write_misses=0;
 static void count(uint32_t& n){if(n!=UINT32_MAX)++n;}
public:
 int reserve(){Guard lock(*this);
  for(unsigned i=0;i<3;i++)if(i!=current&&!pins[i]){pins[i]=Writing;return int(i);}
  count(write_misses);return -1;
 }
 void commit(unsigned slot){Guard lock(*this);current=slot;pins[slot]=0;}
 int pin(){Guard lock(*this);
  if(pins[current]>=Writing-1){count(read_misses);return -1;}
  ++pins[current];return int(current);
 }
 void release(unsigned slot){Guard lock(*this);--pins[slot];}
 uint32_t missed_reads()const{Guard lock(*this);return read_misses;}
 uint32_t missed_writes()const{Guard lock(*this);return write_misses;}
};
}

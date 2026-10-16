#include "public_observer.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
namespace v12 {
namespace {
struct Input {uint16_t length=0;uint8_t bytes[MaxData]{};};
portMUX_TYPE public_mux=portMUX_INITIALIZER_UNLOCKED;
Ring<Input,16> queue;PublicObserverStatus snapshot;TaskHandle_t task=nullptr;
void worker(void*){
  PublicReceiver receiver; // no client secret, no trust anchor provisioned
  while(true){Input in;portENTER_CRITICAL(&public_mux);const bool have=queue.pop(in);portEXIT_CRITICAL(&public_mux);
    if(have){const int64_t begin=esp_timer_get_time();receiver.receive(in.bytes,in.length);
      const uint64_t work=uint64_t(esp_timer_get_time()-begin);portENTER_CRITICAL(&public_mux);
      snapshot.records=receiver.status;snapshot.drops=queue.drops;snapshot.peak=queue.peak;
      if(work>snapshot.work_max_us)snapshot.work_max_us=work;
      portEXIT_CRITICAL(&public_mux);
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}
}
bool public_observer_init(){return xTaskCreatePinnedToCore(worker,"v12_public",12288,nullptr,1,&task,1)==pdPASS;}
bool public_observer_enqueue(const uint8_t* bytes,size_t n){
  if(!bytes||!n||n>MaxData)return false;
  Input in;in.length=uint16_t(n);std::memcpy(in.bytes,bytes,n);
  portENTER_CRITICAL(&public_mux);const bool ok=queue.push(in);snapshot.drops=queue.drops;snapshot.peak=queue.peak;
  portEXIT_CRITICAL(&public_mux);return ok;
}
PublicObserverStatus public_observer_status(){portENTER_CRITICAL(&public_mux);const auto s=snapshot;portEXIT_CRITICAL(&public_mux);return s;}
}

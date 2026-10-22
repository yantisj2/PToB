#include "fault_watchdog.hpp"
#include "fault_watchdog_model.hpp"
#include "console_protocol.hpp"
#include "esp_attr.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstddef>
namespace v12 {
namespace {
constexpr uint32_t Magic=0x57313246,Version=1;
struct RestartIntent {
  uint32_t magic,version,checksum,mask,first_mask,restarts;
  uint64_t fingerprint,boot,elapsed,parent;
  int32_t rc;uint8_t api,relay,reserved[2];
};
RTC_NOINIT_ATTR RestartIntent intent;
static_assert(sizeof(RestartIntent)==64,"RTC watchdog evidence budget changed");
struct Input {
  uint64_t main_at=0,host_at=0,parent=0;
  uint32_t mask=0;int32_t rc=0;uint8_t api=0;
  bool host_seen=false,relay=false,fatal=false,test=false;
#ifdef PTOB_N30
  uint64_t sync_until_us=0,sync_observed_us=0;bool sync_expected=false;
#endif
};
portMUX_TYPE watchdog_mux=portMUX_INITIALIZER_UNLOCKED;
Input input;FaultWatchdogModel model;FaultWatchdogStatus view;
#ifdef PTOB_N30
ContinuousFaultWatchdogModel<UINT64_C(300000000)> sync_model;uint64_t last_sync_observed_us=0;
#endif
uint64_t fingerprint=0,boot_id=0;
StaticTask_t task_storage;StackType_t task_stack[3072];
uint64_t now_us(){const auto n=esp_timer_get_time();return n>0?uint64_t(n):0;}
uint64_t bind(const uint8_t factory[6]){
  uint64_t h=UINT64_C(14695981039346656037);
  const auto bytes=[&](const uint8_t* p,unsigned n){for(unsigned i=0;i<n;i++)h=(h^p[i])*UINT64_C(1099511628211);};
  bytes(factory,6);bytes(reinterpret_cast<const uint8_t*>(V12_SOURCE_REVISION),40);
  bytes(reinterpret_cast<const uint8_t*>(V12_BUILD_ID),64);return h;
}
uint32_t checksum(RestartIntent value){value.checksum=0;return console_crc(&value,sizeof value);}
void supervise(void*){
  for(;;){
    const auto now=now_us();Input current;RestartIntent saved{};bool restart=false;
    portENTER_CRITICAL(&watchdog_mux);
    current=input;uint32_t mask=current.mask;
    if(current.fatal)mask|=FaultInit;
    if(current.test)mask|=FaultTest;
    if(now>=current.main_at&&now-current.main_at>=2000000)mask|=FaultMainStalled;
    if(current.host_seen&&now>=current.host_at&&now-current.host_at>=2000000)mask|=FaultHostStalled;
    model.observe(now,mask);view.mask=mask;view.first_mask=model.first_mask();
    view.since_us=model.active()?model.start_us():0;
    view.elapsed_us=model.active()&&now>=model.start_us()?now-model.start_us():0;
    view.test=current.test;
    restart=model.take_restart(now);
#ifdef PTOB_N30
    // A valid observation can begin/end between100ms supervisor wakes. It
    // still breaks continuous loss; only this owner advances the timer model.
    if(current.sync_observed_us!=last_sync_observed_us){
      sync_model.observe(now,0);last_sync_observed_us=current.sync_observed_us;
    }
    sync_model.observe(now,current.sync_expected&&now>=current.sync_until_us?FaultNoTimeSync:0);
    const bool sync_restart=sync_model.take_restart(now);
    view.mask|=sync_model.current_mask();
    if(sync_model.active()&&(!model.active()||sync_restart)){
      view.first_mask=sync_model.first_mask();view.since_us=sync_model.start_us();
      view.elapsed_us=now>=view.since_us?now-view.since_us:0;
    }
    mask=view.mask;restart|=sync_restart;
#endif
    if(restart){
      saved={Magic,Version,0,mask,view.first_mask,view.restarts==UINT32_MAX?UINT32_MAX:view.restarts+1,
        fingerprint,boot_id,view.elapsed_us,current.parent,current.rc,current.api,uint8_t(current.relay),{0,0}};
    }
    portEXIT_CRITICAL(&watchdog_mux);
    if(restart){
      // RTC only, never NVS/flash or a blocking logger/shutdown callback.
      // A failed original radio drain remains failed; stock MCU reset ends it.
      saved.checksum=checksum(saved);intent=saved;esp_restart();
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}
}
bool fault_watchdog_start(const uint8_t factory[6],uint64_t boot){
  if(!factory||!boot||view.enabled)return false;
  fingerprint=bind(factory);boot_id=boot;const RestartIntent previous=intent;intent.magic=0;
  bool previous_delay=previous.elapsed>=FaultWatchdogModel::DelayUs;
#ifdef PTOB_N30
  if(previous.mask==FaultNoTimeSync)previous_delay=previous.elapsed>=decltype(sync_model)::DelayUs;
#endif
  if(esp_reset_reason()==ESP_RST_SW&&previous.magic==Magic&&previous.version==Version&&
     previous.fingerprint==fingerprint&&previous.checksum==checksum(previous)&&
     previous_delay&&previous.mask&&previous.first_mask&&previous.boot){
    view.previous_boot=previous.boot;view.previous_mask=previous.mask;
    view.previous_elapsed_us=previous.elapsed;view.restarts=previous.restarts;
    view.previous_api=previous.api;view.previous_rc=previous.rc;
    view.parent=previous.parent;view.parent_relay=previous.relay;view.parent_pending=previous.parent!=0;
    input.parent=previous.parent;input.relay=previous.relay;
  }
  input.main_at=now_us();
  view.enabled=xTaskCreateStaticPinnedToCore(supervise,"clock_fault_wd",sizeof task_stack,nullptr,2,
    task_stack,&task_storage,1)!=nullptr;
  return view.enabled;
}
void fault_watchdog_fatal(){portENTER_CRITICAL(&watchdog_mux);input.fatal=true;portEXIT_CRITICAL(&watchdog_mux);}
void fault_watchdog_main_progress(uint32_t mask,uint8_t api,int32_t rc,uint64_t parent,bool relay,bool parent_applied){
  const auto now=now_us();portENTER_CRITICAL(&watchdog_mux);
  input.main_at=now;input.mask=mask;input.api=api;input.rc=rc;
  // During one-shot restoration retain the intent until the standard selector
  // has actually applied. No path, lease or clock map is restored from RTC.
  if(!view.parent_pending&&parent_applied){input.parent=parent;input.relay=relay;}
  portEXIT_CRITICAL(&watchdog_mux);
}
#ifdef PTOB_N30
void fault_watchdog_sync_progress(bool expected,uint64_t valid_until_us){
  portENTER_CRITICAL(&watchdog_mux);input.sync_expected=expected;input.sync_until_us=valid_until_us;
  const auto now=now_us();if(expected&&valid_until_us>now)input.sync_observed_us=now;
  portEXIT_CRITICAL(&watchdog_mux);
}
#endif
void fault_watchdog_host_progress(){const auto now=now_us();portENTER_CRITICAL(&watchdog_mux);input.host_at=now;input.host_seen=true;portEXIT_CRITICAL(&watchdog_mux);}
FaultWatchdogStatus fault_watchdog_status(){portENTER_CRITICAL(&watchdog_mux);const auto result=view;portEXIT_CRITICAL(&watchdog_mux);return result;}
void fault_watchdog_host_expected(){fault_watchdog_host_progress();}
bool fault_watchdog_test(){
  portENTER_CRITICAL(&watchdog_mux);const bool ok=view.enabled&&!input.test&&!input.fatal&&!input.mask&&!model.active();
  if(ok)input.test=true;
  portEXIT_CRITICAL(&watchdog_mux);return ok;
}
void fault_watchdog_parent_restored(){portENTER_CRITICAL(&watchdog_mux);view.parent_pending=false;portEXIT_CRITICAL(&watchdog_mux);}
}

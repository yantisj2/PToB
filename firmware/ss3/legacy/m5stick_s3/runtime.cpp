#include "runtime.hpp"
#include "buttons.hpp"
#ifdef V12_SS3
#include "relay_buttons.hpp"
#endif
#include "board.hpp"
#include "ui.hpp"
#include "../ptob_gnss_reference.hpp"
#include "driver/gpio.h"
#include "driver/mcpwm_cap.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <cstdio>
#include <algorithm>
#include <initializer_list>
#include <atomic>
#ifndef M5STICK_S3_EXTERNAL_PPS
#define M5STICK_S3_EXTERNAL_PPS 0
#endif
namespace sticks3 {
namespace {
gptimer_handle_t clock=nullptr;QueueHandle_t actions=nullptr;
TaskHandle_t keys_task=nullptr;std::atomic<uint32_t> button_drops{0},button_events{0};
uint64_t now(){uint64_t t=0;if(clock)gptimer_get_raw_count(clock,&t);return t;}
void sample_buttons(void*) {
#ifdef V12_SS3
 RelayButtons buttons(gpio_get_level(gpio_num_t(Key1))==0,gpio_get_level(gpio_num_t(Key2))==0,now());
#else
 GoButtons buttons(gpio_get_level(gpio_num_t(Key1))==0,gpio_get_level(gpio_num_t(Key2))==0,now());
#endif
 TickType_t last=xTaskGetTickCount();
 for(;;){
#ifdef V12_SS3
  const auto action=buttons.update(gpio_get_level(gpio_num_t(Key1))==0,gpio_get_level(gpio_num_t(Key2))==0,now());
  if(action!=ButtonInput::None){const uint8_t next=action;
#else
  if(buttons.update(gpio_get_level(gpio_num_t(Key1))==0,gpio_get_level(gpio_num_t(Key2))==0,now())){
   const uint8_t next=ButtonInput::NextPage;
#endif
   if(button_events.load()!=UINT32_MAX)++button_events;
   if(xQueueSend(actions,&next,0)!=pdTRUE && button_drops.load()!=UINT32_MAX)++button_drops;
  }
  vTaskDelayUntil(&last,pdMS_TO_TICKS(KeySampleMs));
 }
}
#if M5STICK_S3_EXTERNAL_PPS
struct Raw {uint32_t count,number;};
portMUX_TYPE capture_lock=portMUX_INITIALIZER_UNLOCKED;
Raw queue[16]{};unsigned head=0,tail=0,size=0;
uint32_t edges=0,drops=0,peak=0,map_count=0,map_serial=0,request_serial=0,map_failures=0;
mcpwm_cap_timer_handle_t cap_timer=nullptr;mcpwm_cap_channel_handle_t cap_channel=nullptr,map_channel=nullptr;
ptob_gnss::CounterMap mapping;
uint64_t before=0,after=0,requested=0,last_request=0,last_edge=0;
uint32_t last_bound=UINT32_MAX;bool enabled=false,last_mapped=false;
bool on_edge(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t* event,void*) {
 portENTER_CRITICAL_ISR(&capture_lock);ptob_gnss::increment(edges);
 if(size==16)ptob_gnss::increment(drops);
 else{queue[head]={event->cap_value,edges};head=(head+1)%16;++size;if(size>peak)peak=size;}
 portEXIT_CRITICAL_ISR(&capture_lock);return false;
}
bool on_map(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t* event,void*) {
 portENTER_CRITICAL_ISR(&capture_lock);map_count=event->cap_value;++map_serial;portEXIT_CRITICAL_ISR(&capture_lock);return false;
}
bool failed() {
 for(auto* h:{&map_channel,&cap_channel})if(*h){(void)mcpwm_capture_channel_disable(*h);(void)mcpwm_del_capture_channel(*h);*h=nullptr;}
 if(cap_timer){(void)mcpwm_capture_timer_stop(cap_timer);(void)mcpwm_capture_timer_disable(cap_timer);(void)mcpwm_del_capture_timer(cap_timer);cap_timer=nullptr;}
 enabled=false;mapping.reset();return false;
}
#endif
}
bool buttons_start(gptimer_handle_t reference) {
 clock=reference;gpio_config_t cfg{};cfg.pin_bit_mask=KeyMask;cfg.mode=GPIO_MODE_INPUT;cfg.pull_up_en=GPIO_PULLUP_ENABLE;
 if(!clock || gpio_config(&cfg)!=ESP_OK)return false;
 actions=xQueueCreate(8,sizeof(uint8_t));
 return actions && xTaskCreatePinnedToCore(sample_buttons,"sticks3_keys",2048,nullptr,3,&keys_task,1)==pdPASS;
}
ButtonInput::Action next_action(){uint8_t v=ButtonInput::None;return actions && xQueueReceive(actions,&v,0)==pdTRUE?ButtonInput::Action(v):ButtonInput::None;}
bool pps_start(gptimer_handle_t reference) {
#if M5STICK_S3_EXTERNAL_PPS
 clock=reference;uint32_t hz=0;
 if(!clock || gptimer_get_resolution(clock,&hz)!=ESP_OK || hz!=20000000)return false;
 mcpwm_capture_timer_config_t cfg{};cfg.group_id=0;cfg.clk_src=MCPWM_CAPTURE_CLK_SRC_APB;
 if(mcpwm_new_capture_timer(&cfg,&cap_timer)!=ESP_OK || mcpwm_capture_timer_get_resolution(cap_timer,&hz)!=ESP_OK || hz!=80000000 || mcpwm_capture_timer_enable(cap_timer)!=ESP_OK || mcpwm_capture_timer_start(cap_timer)!=ESP_OK)return failed();
 mcpwm_capture_channel_config_t ch{};ch.gpio_num=Pps;ch.prescale=1;ch.flags.pos_edge=1;
 const mcpwm_capture_event_callbacks_t edge_cb={.on_cap=on_edge};
 if(mcpwm_new_capture_channel(cap_timer,&ch,&cap_channel)!=ESP_OK || mcpwm_capture_channel_register_event_callbacks(cap_channel,&edge_cb,nullptr)!=ESP_OK || mcpwm_capture_channel_enable(cap_channel)!=ESP_OK)return failed();
 ch={};ch.gpio_num=-1;ch.prescale=1;const mcpwm_capture_event_callbacks_t map_cb={.on_cap=on_map};
 if(mcpwm_new_capture_channel(cap_timer,&ch,&map_channel)!=ESP_OK || mcpwm_capture_channel_register_event_callbacks(map_channel,&map_cb,nullptr)!=ESP_OK || mcpwm_capture_channel_enable(map_channel)!=ESP_OK)return failed();
 enabled=true;std::printf("STICKS3_PPS enabled=1 gpio=6 method=MCPWM_80MHz map=CAUSAL_BRACKET utc=UNAVAILABLE authority=NONE physical_bias=UNKNOWN\n");return true;
#else
 (void)reference;return false;
#endif
}
void pps_poll(uint64_t tick) {
#if M5STICK_S3_EXTERNAL_PPS
 if(!enabled)return;
 uint32_t serial,cap;portENTER_CRITICAL(&capture_lock);serial=map_serial;cap=map_count;portEXIT_CRITICAL(&capture_lock);
 if(requested && serial!=request_serial){
  if(serial!=request_serial+1 || !mapping.observe({before,after,now(),cap}) || !mapping.release(now())){ptob_gnss::increment(map_failures);mapping.reset();}
  requested=0;
 }else if(requested && tick>=requested && tick-requested>2000000){ptob_gnss::increment(map_failures);mapping.reset();requested=0;}
 for(unsigned budget=0;budget<8;++budget){Raw edge{};bool have=false;
  portENTER_CRITICAL(&capture_lock);if(size){edge=queue[tail];tail=(tail+1)%16;--size;have=true;}portEXIT_CRITICAL(&capture_lock);
  if(!have)break;
  const auto result=mapping.convert(edge.count,now());last_mapped=result.valid;
  last_edge=result.valid?result.tick:0;last_bound=result.valid?result.bracket_ns:UINT32_MAX;
 }
 if(!requested && (!last_request || tick-last_request>=5000000)){
  requested=last_request=tick;esp_err_t result;
  portENTER_CRITICAL(&capture_lock);request_serial=map_serial;before=now();result=mcpwm_capture_channel_trigger_soft_catch(map_channel);after=now();portEXIT_CRITICAL(&capture_lock);
  if(result!=ESP_OK){ptob_gnss::increment(map_failures);mapping.reset();requested=0;}
 }
#else
 (void)tick;
#endif
}
void pps_augment(View& view,uint64_t tick) {
#if M5STICK_S3_EXTERNAL_PPS
 view.pps_enabled=enabled;view.pps_mapped=last_mapped && last_edge && tick>=last_edge && tick-last_edge<40000000;
 portENTER_CRITICAL(&capture_lock);view.pps_edges=edges;view.pps_drops=drops;portEXIT_CRITICAL(&capture_lock);
 view.pps_age_ms=last_edge && tick>=last_edge?uint32_t(std::min(uint64_t(UINT32_MAX), (tick-last_edge)/20000)):UINT32_MAX;
 view.pps_bracket_ns=view.pps_mapped?last_bound:UINT32_MAX;
#else
 (void)view;(void)tick;
#endif
}
void status(uint64_t tick) {
 std::printf("STICKS3_UI_STATUS tick=%llu button_events=%lu button_queue_drop=%lu key_stack_min=%lu buttons=11,12 debounce_ms=40 overlap=ONE_UNTIL_ALL_RELEASED radio_toggles=NONE power_button=UNCHANGED\n",
  (unsigned long long)tick,(unsigned long)button_events.load(),(unsigned long)button_drops.load(),(unsigned long)(keys_task?uxTaskGetStackHighWaterMark(keys_task):0));
#if M5STICK_S3_EXTERNAL_PPS
 uint32_t n,d,p;portENTER_CRITICAL(&capture_lock);n=edges;d=drops;p=peak;portEXIT_CRITICAL(&capture_lock);
 std::printf("STICKS3_PPS_STATUS tick=%llu enabled=%u gpio=6 physical_edges=%lu queue_drop=%lu queue_peak=%lu map_accepted=%lu map_rejected=%lu map_failure=%lu map_available=%u utc=UNAVAILABLE GPS=ABSENT authority=NONE physical_bias=UNKNOWN\n",
  (unsigned long long)tick,unsigned(enabled),(unsigned long)n,(unsigned long)d,(unsigned long)p,(unsigned long)mapping.accepted,(unsigned long)mapping.rejected,(unsigned long)map_failures,unsigned(last_mapped));
#else
 std::printf("STICKS3_PPS_STATUS tick=%llu enabled=0 GPIO6=UNTOUCHED utc=UNAVAILABLE authority=NONE\n",(unsigned long long)tick);
#endif
 display_status();
}
}

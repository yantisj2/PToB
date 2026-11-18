// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "n30_display.hpp"
#include "status_adapter.hpp"
#include "dual_time.hpp"
#include "display_time_zone.hpp"
#include "sync_watchdog.hpp"
#include "rc9_time.hpp"
#include "n30_lora.hpp"
#include "ptob_registry.h"
#include "watch.hpp"
#include "shadow_gnss.hpp"
#include "dual_radio.hpp"
#include "control_frame.hpp"
#include "fine_capture.hpp"
#include "watch_button.hpp"
#include "touch_capture.hpp"
#include "radio_candidates.hpp"
#include "radio_diagnostics.hpp"
#include "native_transport.hpp"
#include "watch_holdover.hpp"
#include "calendar.hpp"
#include "board.hpp"
#include "energy.hpp"
#include "console_power.hpp"
#include "imu.hpp"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
#include <new>
namespace {
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
bool imu_restart_pending=false,imu_restarting=false;
bool poweroff_pending=false;
uint8_t button_singles_pending=0,button_power=0;int button_power_error=0;
uint32_t button_edges=0,button_singles=0,button_doubles=0,button_gap_us=0;
watch::WatchButton physical_button;esp_timer_handle_t button_timer=nullptr;
uint64_t button_sample_at=0;bool button_raw=false;
uint32_t power_key_presses=0,power_key_errors=0;
uint32_t ui_stack_min=6144;
struct BatteryStatus {uint64_t at=0,sample_at=0,awake_us=0,observed_us=0;uint32_t samples=0;int percent=-1;uint8_t page=0,hours=1,columns=0;bool available=false;};
BatteryStatus battery_status;
struct UiWorkspace {
 watch::PresentationClock presentation;watch::InstrumentMotion motion;
 watch::RenderCache render_cache;watch::calendar::Snapshot agenda;watch::Model model;
};
static_assert(sizeof(UiWorkspace)<=4096,"Keep large UI working state within its PSRAM budget");
// Keep the 30-column temporary out of the worker's rendering call stack.
__attribute__((noinline)) void history_totals(const watch::BatteryHistory& history,uint64_t now,BatteryStatus& b){
 const auto& w=history.snapshot(now,b.hours);b.awake_us=w.awake_us;b.observed_us=w.observed_us;
 for(const auto& c:w.columns)b.columns+=c.level>=0;
}
watch::TouchStroke touch_queue[8];unsigned touch_head=0,touch_count=0;
uint32_t touch_context=0,touch_tag=UINT32_MAX,touch_samples=0,touch_errors=0,touch_drops=0,touch_gap_us=0,touch_stack=0,touch_strokes=0,touch_actions=0;
bool touch_stopped=true;
TaskHandle_t ui_task=nullptr;
// Published before each potentially slow operation. Read-only USB diagnostics
// can locate a stuck owner even when its last rendered snapshot is unchanged.
uint8_t ui_phase=0;uint64_t ui_phase_at=0;uint32_t ui_loops=0,ui_loop_max_us=0;
void phase(uint8_t value){const auto at=uint64_t(esp_timer_get_time());portENTER_CRITICAL(&mux);ui_phase=value;ui_phase_at=at;portEXIT_CRITICAL(&mux);}

bool touch_wake_pending=false;uint64_t touch_wake_at=0;uint32_t touch_wake_context=0;
uint32_t wake_latency_us=0,wake_render_us=0,wake_flush_us=0;uint8_t wake_source=0;
uint8_t radio_pending=0,radio_busy=0,radio_failed=0;uint64_t radio_retry_after[2]{};

watch::calendar::Snapshot published_calendar;
watch::MobileSelection selection;uint32_t ble_refusal_seen=0;
watchimu::Status imu_status;bool shown_asleep=false;uint32_t wrist_wakes=0;
// Main-owner publication inputs never live on the UI task or main call stack.
struct PublicationWorkspace {n30_lora_view lora;watch::Candidate ble,lor,rc;watch::Model output;n30::Model zone;};
static_assert(sizeof(PublicationWorkspace)<=8192,"Bounded PSRAM-only publication scratch");
PublicationWorkspace* publication=nullptr;
watch::Model published;uint32_t frames=0,errors=0,render_us=0,flush_us=0,frame_gap_max_us=0;uint64_t shown_utc=0,shown_at=0;uint8_t shown_source=0,shown_rms_source=0;uint32_t shown_rms_ns=UINT32_MAX;uint64_t shown_metrics_at=0;bool ready=false,stop_requested=false,stopped=false;int page_request=-1;int face_request=-1;bool sleep_request=false;uint8_t shown_face=0,sleep_seconds=30;bool shown_local=false,shown_clock=false,shown_usb=false;
bool radio_request(unsigned index){
 if(index>1)return false;
 const uint8_t bit=1u<<index;const auto now=uint64_t(esp_timer_get_time());
 portENTER_CRITICAL(&mux);const bool ok=!stop_requested&&!(radio_busy&bit)&&now>=radio_retry_after[index];
 if(ok){radio_pending|=bit;radio_busy|=bit;radio_failed&=~bit;}
 portEXIT_CRITICAL(&mux);return ok;
}
bool radio_take(unsigned index){const uint8_t bit=1u<<index;portENTER_CRITICAL(&mux);const bool take=radio_pending&bit;radio_pending&=~bit;portEXIT_CRITICAL(&mux);return take;}
uint64_t now_us(){return uint64_t(esp_timer_get_time());}
// Timer-service sampling must never wait for rendering, SPI flush or IMU I2C.
// Only sample GPIO and publish small counters/mailboxes here; no shutdown work.
void sample_button(void*){
 const auto now=now_us();const bool pressed=gpio_get_level(GPIO_NUM_0)==0;
 portENTER_CRITICAL(&mux);
 if(stop_requested){portEXIT_CRITICAL(&mux);return;}
 if(button_sample_at&&now>=button_sample_at)button_gap_us=std::max(button_gap_us,uint32_t(std::min<uint64_t>(UINT32_MAX,now-button_sample_at)));
 button_sample_at=now;if(pressed!=button_raw){button_raw=pressed;++button_edges;}
 const auto action=physical_button.update(pressed,now);
 if(action==watch::ButtonAction::Single){++button_singles;if(!poweroff_pending&&button_singles_pending<3)++button_singles_pending;}
 if(action==watch::ButtonAction::PowerOff){++button_doubles;button_singles_pending=0;poweroff_pending=true;button_power=1;}
 if(action!=watch::ButtonAction::None&&ui_task)xTaskNotifyGive(ui_task);
 portEXIT_CRITICAL(&mux);
}
void fail(){watchshadow::visible(false);phase(14);const bool idle=watchboard::idle();portENTER_CRITICAL(&mux);++errors;ready=false;stopped=idle;ui_task=nullptr;portEXIT_CRITICAL(&mux);}
void publish_touch_view(const watch::View& view){
 const uint32_t tag=unsigned(view.screen)|(unsigned(view.settings.face)<<4)|(unsigned(view.asleep)<<8);
 portENTER_CRITICAL(&mux);if(tag!=touch_tag){touch_tag=tag;++touch_context;}portEXIT_CRITICAL(&mux);
}
// Own only the CST9217. ESP-IDF serializes master transactions on the I2C bus;
// the UI remains the sole PMU/IMU/settings owner. Both owners join power drain.
void touch_worker(void*){
 watch::TouchCapture capture;uint64_t last=0;
 for(;;){
  portENTER_CRITICAL(&mux);const bool stop=stop_requested;const auto context=touch_context;const bool asleep=touch_tag&0x100;portEXIT_CRITICAL(&mux);
  if(stop){portENTER_CRITICAL(&mux);touch_stopped=true;portEXIT_CRITICAL(&mux);vTaskDelete(nullptr);return;}
  int x=0,y=0;bool valid=false;const bool contact=watchboard::touch(x,y,&valid);const auto now=now_us();watch::TouchStroke stroke;
  const bool completed=capture.update(valid,contact,x,y,context,now,stroke);
  const auto stack=uint32_t(uxTaskGetStackHighWaterMark(nullptr));
  portENTER_CRITICAL(&mux);++touch_samples;if(!valid)++touch_errors;touch_stack=stack;
  if(last&&now>=last)touch_gap_us=std::max(touch_gap_us,uint32_t(std::min<uint64_t>(UINT32_MAX,now-last)));
  if(!stop_requested&&valid&&contact&&asleep&&context==touch_context&&!touch_wake_pending){touch_wake_pending=true;touch_wake_at=now;touch_wake_context=context;if(ui_task)xTaskNotifyGive(ui_task);}
  if(completed&&!stop_requested){++touch_strokes;if(touch_count<8){touch_queue[(touch_head+touch_count)%8]=stroke;++touch_count;if(ui_task)xTaskNotifyGive(ui_task);}else ++touch_drops;}
  portEXIT_CRITICAL(&mux);last=now;vTaskDelay(pdMS_TO_TICKS(asleep?40:10));
 }
}
void worker(void*){
 phase(1);
 portENTER_CRITICAL(&mux);ui_task=xTaskGetCurrentTaskHandle();portEXIT_CRITICAL(&mux);
 // Keep persistent models, ephemerides and damage bookkeeping off the task stack.
 // Graphics state never falls back to scarce internal radio/DMA memory.
 auto* workspace_memory=heap_caps_malloc(sizeof(UiWorkspace),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!workspace_memory){fail();vTaskDelete(nullptr);return;}
 auto& workspace=*new(workspace_memory) UiWorkspace;
 auto& presentation=workspace.presentation;auto& motion=workspace.motion;auto& render_cache=workspace.render_cache;auto& agenda=workspace.agenda;
 watch::View view;watch::Settings saved;
 if(watchboard::settings_load(&saved,sizeof saved)){
  if(watch::migrate(saved))view.dirty=true;
  if(watch::valid(saved))view.settings=saved;
 }
 view.activity_us=now_us();bool sleeping=true;
 uint64_t last_frame=0,last_battery=0,last_usb=0,last_power_key=0,last_history_status=0,wake_event_at=0;uint8_t wake_kind=0;int battery=-1;bool usb=false;uint32_t battery_samples=0;
 auto* frame=static_cast<uint16_t*>(heap_caps_malloc(watch::Width*watch::Height*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!frame||!watchboard::display_init()){fail();vTaskDelete(nullptr);return;}
 if(!watchboard::sleep(true)){fail();vTaskDelete(nullptr);return;}
 if(!watch::energy::display(false)){fail();vTaskDelete(nullptr);return;}
 // No internal-RAM fallback: retain radio/DMA headroom if PSRAM is exhausted.
 auto* history_memory=heap_caps_malloc(sizeof(watch::BatteryHistory),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 auto* history=history_memory?new(history_memory) watch::BatteryHistory:nullptr;
 if(history)history->observe(now_us(),false);
 watchimu::init();
 publish_touch_view(view);portENTER_CRITICAL(&mux);touch_stopped=false;portEXIT_CRITICAL(&mux);
 if(xTaskCreatePinnedToCore(touch_worker,"watch_touch",2560,nullptr,2,nullptr,1)!=pdPASS){portENTER_CRITICAL(&mux);touch_stopped=true;portEXIT_CRITICAL(&mux);fail();vTaskDelete(nullptr);return;}
 uint8_t brightness=255;uint64_t calendar_minute=UINT64_MAX;bool calendar_valid=false,calendar_estimated=false;
 for(;;){uint64_t now=now_us();const auto loop_start=now;auto& m=workspace.model;int request,requested_face;bool requested_sleep;
  portENTER_CRITICAL(&mux);const bool stop=stop_requested;portEXIT_CRITICAL(&mux);
  if(stop){watchshadow::visible(false);phase(12);const bool ok=watch::energy::display(true)&&watchboard::sleep(true);if(ok)(void)watch::energy::display(false);phase(ok?13:14);portENTER_CRITICAL(&mux);stopped=ok;ready=false;ui_task=nullptr;if(!ok)++errors;portEXIT_CRITICAL(&mux);vTaskDelete(nullptr);return;}
  portENTER_CRITICAL(&mux);const bool restart=imu_restart_pending;imu_restart_pending=false;if(restart){imu_restarting=true;imu_status={};}portEXIT_CRITICAL(&mux);
  if(restart){phase(1);watchimu::init();portENTER_CRITICAL(&mux);imu_status=watchimu::status();imu_restarting=false;portEXIT_CRITICAL(&mux);}
  portENTER_CRITICAL(&mux);request=page_request;page_request=-1;requested_face=face_request;face_request=-1;requested_sleep=sleep_request;sleep_request=false;portEXIT_CRITICAL(&mux);
  phase(2);
  bool force=last_frame==0;if(request>=0){view.screen=static_cast<watch::Screen>(request);view.asleep=false;view.manual_sleep=false;view.glance_until_us=0;view.activity_us=now;force=true;}
  if(requested_face>=0){view.settings.face=uint8_t(requested_face);view.screen=watch::Screen::Face;view.asleep=false;view.manual_sleep=false;view.activity_us=now;view.dirty=true;force=true;}
  if(requested_sleep){view.asleep=true;view.manual_sleep=true;view.glance_until_us=0;view.activity_us=now;force=true;}
  // PMU I2C remains with this owner, never the GPIO timer or touch task.
  // The PMU latches a short press across slow frames. Poll while asleep too.
  if(!last_power_key||now-last_power_key>=80000){bool pressed=false;const bool ok=watchboard::power_key(pressed);last_power_key=now;
   if(pressed){if(view.asleep){wake_event_at=now;wake_kind=4;}view.power_button(now);force=true;publish_touch_view(view);}
   portENTER_CRITICAL(&mux);if(pressed)++power_key_presses;if(!ok)++power_key_errors;portEXIT_CRITICAL(&mux);
  }
  portENTER_CRITICAL(&mux);const bool single=button_singles_pending!=0;if(single)--button_singles_pending;portEXIT_CRITICAL(&mux);
  if(single){if(view.asleep){wake_event_at=now;wake_kind=2;}view.button(now);force=true;}
  portENTER_CRITICAL(&mux);const bool touch_wake=touch_wake_pending&&touch_wake_context==touch_context;const auto contact_at=touch_wake_at;touch_wake_pending=false;portEXIT_CRITICAL(&mux);
  if(touch_wake&&view.asleep){view.touch(0,0,now);wake_event_at=contact_at;wake_kind=1;force=true;}
  publish_touch_view(view);
  for(unsigned budget=0;budget<8;++budget){
   watch::TouchStroke stroke;portENTER_CRITICAL(&mux);const bool have=touch_count!=0;const auto context=touch_context;if(have){stroke=touch_queue[touch_head];touch_head=(touch_head+1)%8;--touch_count;}portEXIT_CRITICAL(&mux);
   if(!have)break;
   if(stroke.apply(view,context,now)){force=true;portENTER_CRITICAL(&mux);++touch_actions;portEXIT_CRITICAL(&mux);publish_touch_view(view);}
  }
  if(view.radio_request){radio_request(view.radio_request-1);view.radio_request=0;force=true;}
  phase(3);
  const auto movement=watchimu::poll(now,view.asleep,view.glance_until_us!=0);
  if(movement==watchimu::Motion::Lower&&view.lower_arm(now))force=true;
  if(movement==watchimu::Motion::Raise&&view.wrist_raise(now)){force=true;wake_event_at=now;wake_kind=3;portENTER_CRITICAL(&mux);++wrist_wakes;portEXIT_CRITICAL(&mux);}
  phase(4);
  if(!last_usb||now-last_usb>=500000){if(watchboard::usb_power(usb)){watch::energy::usb(usb);v12::log_usb_power(usb);}last_usb=now;}
  view.tick(now,usb);
  watchshadow::visible(view.shadow_visible());
  publish_touch_view(view);
  portENTER_CRITICAL(&mux);imu_status=watchimu::status();shown_asleep=view.asleep;shown_usb=usb;portEXIT_CRITICAL(&mux);
  phase(5);
  if(view.dirty){view.save_failed=!watchboard::settings_save(&view.settings,sizeof view.settings);view.dirty=false;force=true;}
  if(!sleeping&&view.asleep){if(!watchboard::sleep(true)){fail();vTaskDelete(nullptr);return;}sleeping=true;if(history)history->observe(now_us(),false);}
  const bool waking=sleeping&&!view.asleep;if(waking){force=true;render_cache.valid=false;}
  if(!view.asleep&&!watch::energy::display(true)){fail();vTaskDelete(nullptr);return;}
  if(waking&&!watchboard::wake_prepare()){fail();vTaskDelete(nullptr);return;}
  if(brightness!=view.settings.brightness){if(!watchboard::brightness(view.settings.brightness)){fail();vTaskDelete(nullptr);return;}brightness=view.settings.brightness;}
  phase(6);
  now=now_us();portENTER_CRITICAL(&mux);m=published;portEXIT_CRITICAL(&mux);
  if(history)history->observe(now,!sleeping);
  // Gauge history continues at 30 seconds regardless of app or panel state.
  if(!last_battery||now-last_battery>=30000000){battery=watchboard::battery();last_battery=now;++battery_samples;if(history)history->sample(now,battery,usb);}
  // Publish small RAM-only diagnostics from the history's sole owner, asleep too.
  if(force||!last_history_status||now-last_history_status>=1000000){
   BatteryStatus b;b.at=now;b.sample_at=last_battery;b.samples=battery_samples;b.percent=battery;b.page=uint8_t(view.screen);b.hours=view.battery_hours;b.available=history!=nullptr;
   if(history)history_totals(*history,now,b);
   portENTER_CRITICAL(&mux);battery_status=b;portEXIT_CRITICAL(&mux);last_history_status=now;
  }
  if(!m.common_clock)m=presentation.present(m,now);
  else if(m.time_valid&&now>=m.sampled_us){m.utc_us+=now-m.sampled_us;m.sampled_us=now;}
  m.shadow=watchshadow::snapshot();
  const uint64_t minute=m.utc_us/60000000;
  if(minute!=calendar_minute||m.time_valid!=calendar_valid||m.local_time!=calendar_estimated){
   phase(7);
   agenda=watch::calendar::snapshot(m.utc_us/1000000,m.time_valid,m.local_time);
   portENTER_CRITICAL(&mux);published_calendar=agenda;portEXIT_CRITICAL(&mux);
   calendar_minute=minute;calendar_valid=m.time_valid;calendar_estimated=m.local_time;
  }
  m.calendar=agenda;
  portENTER_CRITICAL(&mux);shown_face=view.settings.face;sleep_seconds=view.settings.timeout_s;shown_local=m.local_time;shown_clock=m.time_valid;shown_utc=m.utc_us;shown_at=now;shown_source=uint8_t(m.source);portEXIT_CRITICAL(&mux);

  if(!view.asleep&&(force||now-last_frame>=(view.screen==watch::Screen::Face?(view.settings.face==1?50000:(view.settings.face==2||view.settings.face==3)?100000:1000000):1000000))){
   phase(8);
   m.battery=battery;m.uptime_s=now/1000000;m.charging=usb;m.battery_history=history;motion.apply(m,now);
   phase(9);const auto render_begin=now_us();watch::render(frame,m,view,now,&render_cache);const auto render_elapsed=now_us()-render_begin;phase(10);const auto flush_begin=now_us();bool ok=watch::commit_frame(waking,[&](){return watchboard::flush(frame,&render_cache);},[&](){return watchboard::sleep(false);});if(ok&&waking)sleeping=false;const auto flush_elapsed=now_us()-flush_begin;const auto gap=last_frame?now-last_frame:0;last_frame=now;
   if(history)history->observe(now_us(),!sleeping);
   portENTER_CRITICAL(&mux);render_us=uint32_t(render_elapsed);flush_us=uint32_t(flush_elapsed);if(gap<1000000&&gap>frame_gap_max_us)frame_gap_max_us=uint32_t(gap);ready=ok;if(ok){++frames;shown_rms_ns=view.screen==watch::Screen::Face&&watch::receiver_rms_current(m,now)?m.rms_ns:UINT32_MAX;shown_rms_source=shown_rms_ns!=UINT32_MAX?uint8_t(watch::rms_receiver(m)):0;shown_metrics_at=now;}else ++errors;
   if(ok&&waking){wake_latency_us=uint32_t(std::min<uint64_t>(UINT32_MAX,now_us()-(wake_event_at?wake_event_at:render_begin)));wake_render_us=uint32_t(render_elapsed);wake_flush_us=uint32_t(flush_elapsed);wake_source=wake_kind;wake_event_at=0;wake_kind=0;}
   portEXIT_CRITICAL(&mux);
   if(!ok){watchshadow::visible(false);const bool idle=watchboard::idle();portENTER_CRITICAL(&mux);stopped=idle;ui_task=nullptr;portEXIT_CRITICAL(&mux);vTaskDelete(nullptr);return;}
  }
  const auto stack_free=uint32_t(uxTaskGetStackHighWaterMark(nullptr));
  portENTER_CRITICAL(&mux);++ui_loops;ui_stack_min=std::min(ui_stack_min,stack_free);ui_loop_max_us=std::max(ui_loop_max_us,uint32_t(std::min<uint64_t>(UINT32_MAX,now_us()-loop_start)));portEXIT_CRITICAL(&mux);
  if(sleeping&&!watch::energy::display(false)){fail();vTaskDelete(nullptr);return;}
  phase(11);ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(sleeping?80:30));
 }
}
}
bool display_init(){
 auto* memory=heap_caps_malloc(sizeof(PublicationWorkspace),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
 if(!memory)return false;
 publication=new(memory) PublicationWorkspace;
 button_raw=gpio_get_level(GPIO_NUM_0)==0;physical_button.update(button_raw,now_us());
 esp_timer_create_args_t timer{};timer.callback=sample_button;timer.name="watch_key";timer.skip_unhandled_events=true;
 if(esp_timer_create(&timer,&button_timer)!=ESP_OK)return false;
 if(esp_timer_start_periodic(button_timer,5000)!=ESP_OK){esp_timer_delete(button_timer);button_timer=nullptr;return false;}
 if(xTaskCreatePinnedToCore(worker,"watch_ui",6144,nullptr,1,nullptr,1)==pdPASS)return true;
 esp_timer_stop(button_timer);esp_timer_delete(button_timer);button_timer=nullptr;return false;
}
void watch::button_power_stage(uint8_t stage,int error){portENTER_CRITICAL(&mux);button_power=stage;button_power_error=error;portEXIT_CRITICAL(&mux);}
void n30_display_observe_source(const v12::R2Status&,uint64_t,uint32_t refusal){ble_refusal_seen=refusal;}
void n30_display_publish(const v12::R2Status& s,const DisplayState& d,uint64_t now,bool ble_parked){
 if(!publication)return;
 auto& lora=publication->lora;n30_lora_snapshot(lora);
 const auto hold=watch::holdover_update(s,now,ble_parked);
 auto& ble=publication->ble;auto& lor=publication->lor;auto& rc=publication->rc;
 watch::fill_ble_candidate(ble,s,d,now,ble_parked,hold);watch::fill_lora_candidate(lor,lora,now);new(&rc) watch::Candidate;
 const auto r=v12::r2_rc9_status();uint64_t utc=0;
 if(!ble_parked&&s.requested&&!s.fault&&!s.rc7_upstream_mode_pending&&n30::rc9_time(r,now,utc)){
  rc.clock.time_valid=true;rc.clock.utc_us=utc;rc.clock.sampled_us=now;rc.clock.until_us=std::min(r.until_us,now+250000);
  rc.clock.source=watch::Source::RC9;rc.root=r.root;rc.boot=r.root_boot;rc.epoch=r.source_epoch;rc.fresh=true;
 }
 auto& w=publication->output;new(&w) watch::Model(selection.select(ble,lor,rc,now));w.lora_on=lor.clock.lora_on;w.lora_receiving=lor.clock.lora_receiving;
 const auto ble_power=v12::transport_watch_ble_status();
 w.ble_state=ble_power.off_ack()&&!ble_power.requested?watch::RadioState::Off:ble_power.operating_fault||ble_power.stage==watch::BlePower::Fault||s.fault?watch::RadioState::Fault:ble_power.off_ack()?watch::RadioState::Off:ble_power.stage==watch::BlePower::Active&&s.requested?watch::RadioState::On:watch::RadioState::Changing;
 w.lora_state=lora.fault?watch::RadioState::Fault:lora.requested&&lora.active?watch::RadioState::On:!lora.requested&&lora.off_acknowledged&&lora.spi_retired&&lora.capture_released&&lora.reset_asserted?watch::RadioState::Off:watch::RadioState::Changing;
 portENTER_CRITICAL(&mux);
 // A rejected UI command is transient feedback, not a permanent radio fault.
 // Once the owner proves a settled state, show its recovered state again.
 if(now>=radio_retry_after[0]&&(w.ble_state==watch::RadioState::On||w.ble_state==watch::RadioState::Off))radio_failed&=~1u;
 if(now>=radio_retry_after[1]&&(w.lora_state==watch::RadioState::On||w.lora_state==watch::RadioState::Off))radio_failed&=~2u;
 if(radio_busy&1)w.ble_state=watch::RadioState::Changing;
 if(radio_busy&2)w.lora_state=watch::RadioState::Changing;
 if(radio_failed&1)w.ble_state=watch::RadioState::Fault;
 if(radio_failed&2)w.lora_state=watch::RadioState::Fault;
 portEXIT_CRITICAL(&mux);
 auto& zone_model=publication->zone;new(&zone_model) n30::Model(n30::model(s,d,now,ptob_registry_name(s.root_identity),ble_parked));
 static n30::DisplayTimeZone zone;zone.update(zone_model,s.tz_received,s.joined_peer,s.joined_boot,lora,now);
 if(r2_tz_valid(zone_model.tz)&&now<zone_model.tz_until_us){w.tz_minutes=zone_model.tz.offset_min;w.tz_id=zone_model.tz.id;}
 watchshadow::selected(w,now);
 const auto common=watchdual::clock();
 if(common.enabled){
  w.time_valid=common.valid;w.utc_us=common.utc_ns/1000;w.sampled_us=common.sampled_us;w.until_us=now+250000;
  w.holding=common.mode==watch::dual::Mode::Hold;w.local_time=w.holding;w.locked=common.fine;
  w.source=common.mode==watch::dual::Mode::Ble?ble.clock.source:common.mode==watch::dual::Mode::LoRa?watch::Source::LOR:watch::Source::None;
  w.source_rssi=common.mode==watch::dual::Mode::Ble?ble.clock.source_rssi:common.mode==watch::dual::Mode::LoRa?lor.clock.source_rssi:-127;
  w.common_clock=true;
 }
 watch::receiver_metrics(w,ble,lor,now);
 portENTER_CRITICAL(&mux);published=w;portEXIT_CRITICAL(&mux);
}

void watch::display_control(v12::ConsoleFrame& f){
 watch::ControlUi u;portENTER_CRITICAL(&mux);u.frames=frames;u.errors=errors;u.face=shown_face;u.ready=ready;u.asleep=shown_asleep;u.display_off=stopped&&touch_stopped;portEXIT_CRITICAL(&mux);
 const auto p=v12::transport_watch_ble_status();const auto g=watchshadow::control();
 watch::control_ui(f,u,p,g,n30_fine_released(),n30_fine_cleanup_fault());
}
void n30_display_status(v12::ConsoleFrame& f){portENTER_CRITICAL(&mux);auto n=frames,e=errors,duration=render_us,flush=flush_us,gap=frame_gap_max_us;bool r=ready,local=shown_local,clock=shown_clock;auto face=shown_face,sleep=sleep_seconds,source=shown_source;auto utc=shown_utc,at=shown_at;auto imu=imu_status;auto wakes=wrist_wakes;const auto edges=button_edges,singles=button_singles,doubles=button_doubles,gap_button=button_gap_us;const auto power=button_power;const auto power_error=button_power_error;const auto ts=touch_samples,te=touch_errors,td=touch_drops,tg=touch_gap_us,tk=touch_stack,tst=touch_strokes,tact=touch_actions,wl=wake_latency_us,wr=wake_render_us,wf=wake_flush_us;const auto ws=wake_source,ph=ui_phase;const auto ph_at=ui_phase_at;const auto loops=ui_loops,loop_max=ui_loop_max_us;bool asleep=shown_asleep,usb=shown_usb;portEXIT_CRITICAL(&mux);f.format("TWATCH_UI version=1 width=410 height=502 frames=%u errors=%u ready=%u face=%u sleep_s=%u local=%u clock=%u render_us=%u flush_us=%u frame_gap_max_us=%u source=%u utc_us=%llu sampled_us=%llu asleep=%u imu=%u imu_algorithm=1001 imu_stage=%u imu_error=%d imu_events=%u imu_errors=%u wrist_wakes=%u imu_samples=%u arm_lower=%u imu_bus=%u imu_budget=%u glance_s=5 usb=%u key_edges=%u key_single=%u key_double=%u key_gap_us=%u power_stage=%u power_error=%d touch_samples=%u touch_errors=%u touch_drops=%u touch_gap_us=%u touch_stack=%u touch_strokes=%u touch_actions=%u wake_us=%u wake_render_us=%u wake_flush_us=%u wake_source=%u GNSS=FACE_AUTO precision=UNQUALIFIED",unsigned(n),unsigned(e),unsigned(r),unsigned(face),unsigned(sleep),unsigned(local),unsigned(clock),unsigned(duration),unsigned(flush),unsigned(gap),unsigned(source),(unsigned long long)utc,(unsigned long long)at,unsigned(asleep),unsigned(imu.ready),unsigned(imu.stage),int(imu.error),unsigned(imu.wake_events),unsigned(imu.errors),unsigned(wakes),unsigned(imu.samples),unsigned(imu.lowered),unsigned(imu.bus_errors),unsigned(imu.budget_errors),unsigned(usb),unsigned(edges),unsigned(singles),unsigned(doubles),unsigned(gap_button),unsigned(power),power_error,unsigned(ts),unsigned(te),unsigned(td),unsigned(tg),unsigned(tk),unsigned(tst),unsigned(tact),unsigned(wl),unsigned(wr),unsigned(wf),unsigned(ws));
 portENTER_CRITICAL(&mux);const auto rms=shown_rms_ns;const auto rms_source=shown_rms_source;const auto rms_at=shown_metrics_at;portEXIT_CRITICAL(&mux);
 f.format(" shown_rms_ns=%u shown_rms_source=%u shown_metrics_us=%llu",unsigned(rms),unsigned(rms_source),(unsigned long long)rms_at);
 f.format(" ui_phase=%u ui_phase_age_us=%llu ui_loops=%u ui_loop_max_us=%u",unsigned(ph),(unsigned long long)(now_us()>=ph_at?now_us()-ph_at:0),unsigned(loops),unsigned(loop_max));
 portENTER_CRITICAL(&mux);const auto pk=power_key_presses,pke=power_key_errors,ui_stack=ui_stack_min;const auto battery=battery_status;portEXIT_CRITICAL(&mux);
 f.format(" pmu_key=%u pmu_key_errors=%u",unsigned(pk),unsigned(pke));
 f.format(" ui_stack_min_bytes=%u ui_psram_bytes=%u battery_psram_bytes=%u",unsigned(ui_stack),unsigned(sizeof(UiWorkspace)),unsigned(sizeof(watch::BatteryHistory)));
 f.format(" app=%u battery_history=%u battery_pct=%d battery_samples=%u battery_sample_us=%llu battery_at_us=%llu battery_hours=%u battery_columns=%u battery_awake_us=%llu battery_observed_us=%llu",unsigned(battery.page),unsigned(battery.available),battery.percent,unsigned(battery.samples),(unsigned long long)battery.sample_at,(unsigned long long)battery.at,unsigned(battery.hours),unsigned(battery.columns),(unsigned long long)battery.awake_us,(unsigned long long)battery.observed_us);
 f.format(" heap_internal_largest=%u heap_internal_min=%u",unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)),unsigned(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)));
 const auto energy=watch::energy::status();f.format(" cpu_mhz=%u cpu_ble=%u cpu_panel=%u cpu_rc=%d",unsigned(energy.cpu_mhz),unsigned(energy.ble),unsigned(energy.display),energy.error);
 const auto p=v12::transport_watch_ble_status();
 f.format(" ble_power_stage=%u ble_power_requested=%u ble_controller=%u ble_host_running=%u ble_power_off_ack=%u ble_power_generation=%u ble_power_rc=%ld ble_power_failed_stage=%u ble_power_deadline_us=%llu ble_power_stack=%u ble_sync_site=%u ble_sync_rc=%ld ble_power_worker=%u ble_power_alloc_failures=%u",unsigned(p.stage),unsigned(p.requested),unsigned(p.controller),unsigned(p.host_running),unsigned(p.off_ack()),unsigned(p.generation),long(p.error),unsigned(p.failed_stage),(unsigned long long)p.deadline_us,unsigned(p.stack_min_bytes),unsigned(p.sync_site),long(p.sync_error),unsigned(p.worker_running),unsigned(p.worker_alloc_failures));
 const bool cleanup_off=p.cleanup_completed_us&&p.cleanup_completed_us==p.off_proven_us;
 f.format(" ble_off_proof=%u ble_off_gen=%u ble_off_start_us=%llu ble_off_deadline_us=%llu ble_off_at_us=%llu ble_operating_fault=%u ble_cleanup_rc=%ld",
 unsigned(p.off_proven),unsigned(p.off_generation),(unsigned long long)(cleanup_off?p.cleanup_started_us:p.started_us),
 (unsigned long long)(cleanup_off?p.cleanup_deadline_us:p.deadline_us),(unsigned long long)p.off_proven_us,unsigned(p.operating_fault),long(p.cleanup_error));
 watchshadow::status(f);
}
bool n30_display_imu_restart(){portENTER_CRITICAL(&mux);const bool ok=!stop_requested&&!imu_restart_pending&&!imu_restarting;if(ok)imu_restart_pending=true;portEXIT_CRITICAL(&mux);return ok;}
void n30_display_imu_frame(v12::ConsoleFrame& f){
 portENTER_CRITICAL(&mux);const auto s=imu_status;const bool asleep=shown_asleep;const auto wakes=wrist_wakes;portEXIT_CRITICAL(&mux);
 f.format("TWATCH_IMU version=1 detector=1001 sampled_us=%llu ready=%u stage=%u error=%d kernel=%u available=%u orientation=P2 orientation_ok=%u interrupt_ctrl=%u irq=%u armed=%u asleep=%u return_active=%u accel_millihz=%u detector_millihz=%u config_reads=%u xyz=%d/%d/%d sensor_us=%llu last_sample_us=%llu sample_age_ms=%u polls=%u services=%u irq_high=%u stale_polls=%u max_poll_gap_us=%u max_service_us=%u samples=%u glance_events=%u tilt_events=%u wakes=%u lowered=%u arms=%u disarms=%u ignored_awake=%u ignored_disarmed=%u ignored_refractory=%u last_event_us=%llu meta=%u/%u/%u overflows=%u sensor_errors=%u resets=%u errors=%u bus_errors=%u budget_errors=%u ignored_stale=%u bus_rc=%ld bus_reg=%u bus_len=%u bus_write=%u bus_at_us=%llu bus_work_us=%u bus_max_us=%u pose_prepared=%u pose_rejected=%u bus_retries=%u bus_recovered=%u wearing=LEFT_ELBOW raw_units=COUNTS diagnostic_only=1\n",
 (unsigned long long)s.sampled_us,unsigned(s.ready),unsigned(s.stage),int(s.error),unsigned(s.kernel),unsigned(s.available),unsigned(s.orientation_ok),unsigned(s.interrupt_ctrl),unsigned(s.irq),unsigned(s.armed),unsigned(asleep),unsigned(s.return_active),unsigned(s.accel_millihz),unsigned(s.rate_millihz),unsigned(s.config_reads),int(s.x),int(s.y),int(s.z),(unsigned long long)s.sensor_us,(unsigned long long)s.last_sample_us,unsigned(s.sample_age_ms),unsigned(s.polls),unsigned(s.services),unsigned(s.irq_high),unsigned(s.stale_polls),unsigned(s.max_poll_gap_us),unsigned(s.max_service_us),unsigned(s.samples),unsigned(s.events),unsigned(s.tilt_events),unsigned(wakes),unsigned(s.lowered),unsigned(s.arms),unsigned(s.disarms),unsigned(s.ignored_awake),unsigned(s.ignored_disarmed),unsigned(s.ignored_refractory),(unsigned long long)s.last_event_us,unsigned(s.last_meta),unsigned(s.meta_sensor),unsigned(s.meta_value),unsigned(s.fifo_overflows),unsigned(s.sensor_errors),unsigned(s.resets),unsigned(s.errors),unsigned(s.bus_errors),unsigned(s.budget_errors),unsigned(s.ignored_stale),long(s.bus_rc),unsigned(s.bus_reg),unsigned(s.bus_len),unsigned(s.bus_write),(unsigned long long)s.bus_at_us,unsigned(s.bus_work_us),unsigned(s.bus_max_us),unsigned(s.pose_prepared),unsigned(s.pose_rejected),unsigned(s.bus_retries),unsigned(s.bus_recovered));
}
void n30_display_dual_frame(v12::ConsoleFrame& f,uint32_t fault_slot,uint32_t,uint64_t now){
 const auto hold=watch::holdover_status();const auto& h=hold.view;
 f.format("TWATCH_SOURCE version=2 refusal=%u source=%u switches=%u gaps=%u stable_us=%llu grace_us=500000 return_us=30000000 policy=PARALLEL_MOBILE presentation_only=1 p99_ready=%u p99_ms=%u p99_lower=%u p99_trials=%u p99_bad=%u p99_unknown=%u p99_probe_ms=%u p99_started=%u p99_completed=%u p99_aborted=%u p99_gaps=%u p99_resets=%u p99_ignored=%u p99_reason=%s p99_dropped=%u p99_drained=%u p99_queued=%u p99_queue_peak=%u p99_work_max_us=%u p99_root=%016llx p99_parent=%016llx p99_source=%u p99_train=%u p99_generation=%u p99_epoch=%u p99_policy=SAMPLED_PREFIX_1US",
 unsigned(ble_refusal_seen),unsigned(selection.source()),unsigned(selection.switches()),unsigned(selection.gaps()),(unsigned long long)selection.stable_us(now),
 unsigned(h.ready),unsigned(h.duration_ms),unsigned(h.lower_bound),unsigned(h.trials),unsigned(h.bad),unsigned(h.unknown),unsigned(h.probe_ms),unsigned(h.started),unsigned(h.completed),unsigned(h.aborted),unsigned(h.gaps),unsigned(hold.resets),unsigned(hold.ignored),hold.reason,
 unsigned(hold.dropped),unsigned(hold.drained),unsigned(hold.queued),unsigned(hold.peak),unsigned(hold.work_max_us),(unsigned long long)hold.root,(unsigned long long)hold.parent,unsigned(hold.source),unsigned(hold.train),unsigned(hold.generation),unsigned(hold.capture_epoch));
 const auto& v=hold.servo;
 f.format(" servo_trained=%u servo_rate_mppb=%ld servo_phase_ns=%lld servo_raw_rms_ns=%u servo_max_ns=%u servo_samples=%u servo_accepted=%u servo_refused=%u servo_predictions=%u servo_fit_points=%u servo_fit_span_ms=%u servo_fit_reason=%u servo_original_age_us=%u servo_service_left_us=%u servo_corrected_left_us=%u servo_replay_left_us=%u servo_refusal=%u",
 unsigned(v.trained),long(v.rate_mppb),(long long)v.phase_ns,unsigned(v.rms_ns),unsigned(v.max_ns),unsigned(v.samples),unsigned(v.accepted),unsigned(v.refused),unsigned(v.predictions),unsigned(v.fit_points),unsigned(v.fit_span_ms),unsigned(v.fit_reason),unsigned(v.original_age_us),unsigned(v.service_left_us),unsigned(v.corrected_left_us),unsigned(v.replay_left_us),unsigned(v.refusal));
 f.format(" ble_return_block=%u ble_return_resets=%u ble_return_reason=%u ble_switch_reason=%u ble_progress_age_us=%llu ble_gap_us=%llu ble_ignored_failures=%u ble_model_expiries=%u ble_window_misses=%u ble_setup_failures=%u",
 unsigned(selection.return_block()),unsigned(selection.return_resets()),unsigned(selection.return_reset_reason()),unsigned(selection.switch_reason()),
 (unsigned long long)selection.progress_age_us(now),(unsigned long long)selection.gap_us(),unsigned(selection.ignored_failures()),
 unsigned(selection.model_expiries()),unsigned(selection.window_misses()),unsigned(selection.setup_failures()));
 watch::radio_fault_fields(f,watch::radio_journal_status(fault_slot),v12::r2_watch_radio_recovery_status());
 f.format("\n");
}
void n30_display_trace_poll(){}
bool watch::display_sleep_request(){portENTER_CRITICAL(&mux);const bool ok=!stop_requested&&ui_task&&!stopped;if(ok){sleep_request=true;xTaskNotifyGive(ui_task);}portEXIT_CRITICAL(&mux);return ok;}
bool watch::display_select_face(unsigned face){if(face>=watch::FaceCount)return false;portENTER_CRITICAL(&mux);const bool ok=!stop_requested&&ui_task&&!stopped;if(ok){face_request=int(face);if(ui_task)xTaskNotifyGive(ui_task);}portEXIT_CRITICAL(&mux);return ok;}
bool n30_display_page(unsigned p){if(p>4)return false;portENTER_CRITICAL(&mux);page_request=int(p);portEXIT_CRITICAL(&mux);return true;}
bool n30_display_radio_toggle(unsigned index){return radio_request(index);}
bool n30_display_take_upstream_toggle(uint64_t){return false;}bool n30_display_take_lora_toggle(uint64_t){return radio_take(1);}bool n30_display_take_ble_toggle(uint64_t){return radio_take(0);}
void n30_display_action_result(n30::Action action,bool accepted){
 if(action!=n30::Action::ToggleBle&&action!=n30::Action::ToggleLoRa)return;
 const unsigned index=action==n30::Action::ToggleBle?0:1;const uint8_t bit=1u<<index;
 portENTER_CRITICAL(&mux);radio_busy&=~bit;if(!accepted)radio_failed|=bit;radio_retry_after[index]=now_us()+2000000;portEXIT_CRITICAL(&mux);
}bool n30_display_minimal(){return false;}bool n30_display_take_poweroff(){portENTER_CRITICAL(&mux);const bool take=poweroff_pending&&!stop_requested;poweroff_pending=false;portEXIT_CRITICAL(&mux);return take;}void n30_display_request_poweroff(){portENTER_CRITICAL(&mux);stop_requested=true;if(ui_task)xTaskNotifyGive(ui_task);portEXIT_CRITICAL(&mux);}bool n30_display_power_ready(){portENTER_CRITICAL(&mux);const bool s=stopped&&touch_stopped;portEXIT_CRITICAL(&mux);return s;}
void display_show(const DisplayState&){}bool display_set_scan(DisplayTiming::Profile){return false;}void display_update_indicators(bool,bool,bool){}void display_sleep(){}void display_wake(){}void display_blank(){}

bool watch::calendar::current(Snapshot& out){portENTER_CRITICAL(&mux);out=published_calendar;portEXIT_CRITICAL(&mux);return out.sampled_utc!=0;}

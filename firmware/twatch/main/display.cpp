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
#include "board.hpp"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
namespace {
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
n30::ClockSelection selection;n30::LockTimer lock_timer;
watch::Model published;uint32_t frames=0,errors=0,render_us=0;bool ready=false,stop_requested=false,stopped=false;int page_request=-1;uint8_t shown_face=0,sleep_seconds=90;bool shown_local=false,shown_clock=false;
uint64_t now_us(){return uint64_t(esp_timer_get_time());}
void fail(){const bool idle=watchboard::idle();portENTER_CRITICAL(&mux);++errors;ready=false;stopped=idle;portEXIT_CRITICAL(&mux);}
void worker(void*){
 watch::View view;watch::Settings saved;watch::PresentationClock presentation;watch::Gesture gesture;
 if(watchboard::settings_load(&saved,sizeof saved)){
  if(saved.version==1&&saved.face==0&&saved.brightness>=10&&saved.brightness<=100){saved.version=2;saved.timeout_s=90;view.dirty=true;}
  if(watch::valid(saved))view.settings=saved;
 }
 view.activity_us=now_us();bool down=gpio_get_level(GPIO_NUM_0)==0,sleeping=false;
 uint64_t last_frame=0,last_battery=0,button_edge=0;int battery=-1;bool previous_live=false,previous_local=false;
 auto* frame=static_cast<uint16_t*>(heap_caps_malloc(watch::Width*watch::Height*2,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!frame||!watchboard::display_init()){fail();vTaskDelete(nullptr);return;}
 uint8_t brightness=255;
 for(;;){uint64_t now=now_us();watch::Model m;int request;
  portENTER_CRITICAL(&mux);const bool stop=stop_requested;portEXIT_CRITICAL(&mux);
  if(stop){const bool ok=watchboard::sleep(true);portENTER_CRITICAL(&mux);stopped=ok;ready=false;if(!ok)++errors;portEXIT_CRITICAL(&mux);vTaskDelete(nullptr);return;}
  portENTER_CRITICAL(&mux);m=published;request=page_request;page_request=-1;portEXIT_CRITICAL(&mux);
  bool force=last_frame==0;if(request>=0){view.screen=static_cast<watch::Screen>(request);view.asleep=false;view.activity_us=now;force=true;}
  bool pressed=gpio_get_level(GPIO_NUM_0)==0;
  if(pressed!=down&&now-button_edge>=40000){down=pressed;button_edge=now;if(!pressed){view.button(now);force=true;}}
  int x=0,y=0;bool contact=watchboard::touch(x,y);if(gesture.update(view,contact,x,y,now))force=true;
  view.tick(now);
  if(view.dirty){view.save_failed=!watchboard::settings_save(&view.settings,sizeof view.settings);view.dirty=false;force=true;}
  if(sleeping!=view.asleep){if(!watchboard::sleep(view.asleep)){fail();vTaskDelete(nullptr);return;}sleeping=view.asleep;force=true;}
  if(brightness!=view.settings.brightness){if(!watchboard::brightness(view.settings.brightness)){fail();vTaskDelete(nullptr);return;}brightness=view.settings.brightness;}
  m=presentation.present(m,now);
  portENTER_CRITICAL(&mux);shown_face=view.settings.face;sleep_seconds=view.settings.timeout_s;shown_local=m.local_time;shown_clock=m.time_valid;portEXIT_CRITICAL(&mux);
  bool live=m.time_valid&&now>=m.sampled_us&&now-m.sampled_us<250000&&now<m.until_us;
  if(live!=previous_live||m.local_time!=previous_local){force=true;previous_live=live;previous_local=m.local_time;}
  if(!sleeping&&(force||now-last_frame>=1000000)){
   if(!last_battery||now-last_battery>=30000000){battery=watchboard::battery();last_battery=now;}m.battery=battery;m.uptime_s=now/1000000;
   const auto render_begin=now_us();watch::render(frame,m,view,now);const auto render_elapsed=now_us()-render_begin;bool ok=watchboard::flush(frame);last_frame=now;
   portENTER_CRITICAL(&mux);render_us=uint32_t(render_elapsed);ready=ok;if(ok)++frames;else ++errors;portEXIT_CRITICAL(&mux);
   if(!ok){const bool idle=watchboard::idle();portENTER_CRITICAL(&mux);stopped=idle;portEXIT_CRITICAL(&mux);vTaskDelete(nullptr);return;}
  }
  vTaskDelay(pdMS_TO_TICKS(sleeping?80:30));
 }
}
}
bool display_init(){return xTaskCreatePinnedToCore(worker,"watch_ui",6144,nullptr,1,nullptr,1)==pdPASS;}
void n30_display_observe_source(const v12::R2Status& s,uint64_t now,uint32_t refusal){selection.observe(refusal==0,s.root_identity,s.root_boot,s.source_epoch,s.g1_clock_origin,s.publication,s.g1_relative_quality.scope_generation,now,refusal);}
void n30_display_publish(const v12::R2Status& s,const DisplayState& d,uint64_t now,bool ble_parked){
 auto m=n30::model(s,d,now,ptob_registry_name(s.root_identity),ble_parked);n30_lora_view lora;n30_lora_snapshot(lora);
 static n30::DisplayTimeZone zone;zone.update(m,s.tz_received,s.joined_peer,s.joined_boot,lora,now);selection.select(m,lora,now,n30::ble_locked(s,now));
 const auto rc9=v12::r2_rc9_status();n30::select_rc9(m,rc9,now,ptob_registry_name(rc9.server),ptob_registry_name(rc9.root));lock_timer.update(m,now);
 watch::Model w;uint64_t utc=0;w.time_valid=n30::time(m,now,utc);w.utc_us=utc;w.sampled_us=now;w.until_us=m.until_us;w.locked=m.locked;w.holding=m.state==n30::State::Hold;w.rms_ns=m.rms_ns;w.lora_on=lora.active&&!lora.fault;
 switch(m.source){case n30::Source::Ble:w.source=watch::Source::BLE;break;case n30::Source::LoRa:w.source=watch::Source::LOR;break;case n30::Source::Rc1:w.source=watch::Source::RC1;break;case n30::Source::Rc2:w.source=watch::Source::RC2;break;case n30::Source::Rc9:w.source=watch::Source::RC9;break;default:break;}
 w.hold_valid=s.following&&s.model_available20&&s.hold_samples>=100&&s.hold_ms;w.hold_ms=s.hold_ms;w.hold_trials=s.hold_samples;w.hold_lower=false;
 if(r2_tz_valid(m.tz)&&now<m.tz_until_us)w.tz_minutes=m.tz.offset_min;
 portENTER_CRITICAL(&mux);published=w;portEXIT_CRITICAL(&mux);
}
void n30_display_status(v12::ConsoleFrame& f){portENTER_CRITICAL(&mux);auto n=frames,e=errors,duration=render_us;bool r=ready,local=shown_local,clock=shown_clock;auto face=shown_face,sleep=sleep_seconds;portEXIT_CRITICAL(&mux);f.format("TWATCH_UI version=1 width=410 height=502 frames=%u errors=%u ready=%u face=%u sleep_s=%u local=%u clock=%u render_us=%u GNSS=OFF precision=UNQUALIFIED\n",unsigned(n),unsigned(e),unsigned(r),unsigned(face),unsigned(sleep),unsigned(local),unsigned(clock),unsigned(duration));}
void n30_display_dual_frame(v12::ConsoleFrame& f,uint32_t,uint32_t,uint64_t now){const auto s=selection.view(now);f.format("TWATCH_SOURCE refusal=%u qualified=%u lora=%u remaining_us=%u\n",unsigned(s.refusal),unsigned(s.qualified),unsigned(s.lora),unsigned(s.remaining_us));}
void n30_display_trace_poll(){}
bool n30_display_page(unsigned p){if(p>3)return false;portENTER_CRITICAL(&mux);page_request=int(p);portEXIT_CRITICAL(&mux);return true;}
bool n30_display_take_upstream_toggle(uint64_t){return false;}bool n30_display_take_lora_toggle(uint64_t){return false;}bool n30_display_take_ble_toggle(uint64_t){return false;}
void n30_display_action_result(n30::Action,bool){}bool n30_display_minimal(){return false;}bool n30_display_take_poweroff(){return false;}void n30_display_request_poweroff(){portENTER_CRITICAL(&mux);stop_requested=true;portEXIT_CRITICAL(&mux);}bool n30_display_power_ready(){portENTER_CRITICAL(&mux);const bool s=stopped;portEXIT_CRITICAL(&mux);return s;}
void display_show(const DisplayState&){}bool display_set_scan(DisplayTiming::Profile){return false;}void display_update_indicators(bool,bool,bool){}void display_sleep(){}void display_wake(){}void display_blank(){}

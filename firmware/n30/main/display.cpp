// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// One low-priority display/button owner; no BLE APIs, authority or clock writes.
#include "n30_display.hpp"
#include "ui.hpp"
#include "status_adapter.hpp"
#include "display_phase.hpp"
#include "display_budget.hpp"
#include "native_device_binding.hpp"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstring>
namespace {
i2c_master_bus_handle_t bus=nullptr;i2c_master_dev_handle_t oled=nullptr;
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
n30::Model published; n30::Activity activity; int requested_page=-1;
n30::ActionMailbox upstream_toggle;
struct Stats {uint32_t frames=0,errors=0,max_us=0,max_phase_us=0;uint64_t sampled=0;uint8_t page=0;bool ready=false,help=false,saver=false,blank=false;} stats;
TaskHandle_t ui_task=nullptr;esp_timer_handle_t frame_timer=nullptr;
void frame_wake(void*){if(ui_task)xTaskNotifyGive(ui_task);}
uint8_t portrait[n30::FrameBytes],native[n30::FrameBytes],shadow[n30::FrameBytes];bool shadow_valid=false;
uint64_t now_us(){return uint64_t(esp_timer_get_time());}
bool command(uint8_t value){uint8_t data[]={0,value};return i2c_master_transmit(oled,data,sizeof data,10)==ESP_OK;}
bool send_before(const uint8_t* data,size_t size,uint64_t deadline){
 const int timeout=n30::transfer_timeout(deadline,now_us(),size);
 return timeout&&i2c_master_transmit(oled,data,size,timeout)==ESP_OK&&now_us()<deadline;
}
bool flush(uint64_t deadline){
 return n30::flush_clock_first(native,shadow,shadow_valid,PTOB_N30_ROTATE_CW,[&](unsigned page,unsigned first,const uint8_t* data,unsigned size){
  const uint8_t addr[]={0,uint8_t(0xb0|page),uint8_t(first&15),uint8_t(0x10|(first>>4))};
  uint8_t packet[129];packet[0]=0x40;std::memcpy(packet+1,data,size);
  return send_before(addr,sizeof addr,deadline)&&send_before(packet,size+1,deadline);
 });
}
void worker(void*){
 static n30::ClockHold clock_hold; // One display owner; framebuffer stays off stack.
 n30::Input input(gpio_get_level(GPIO_NUM_0)==0);n30::View view;n30::FrameCadence cadence;
 uint64_t armed_due=0;bool force=true,off=false,dim=false;
 for(;;){
  const auto now=now_us();n30::Model m;portENTER_CRITICAL(&mux);m=published;portEXIT_CRITICAL(&mux);
  int request;portENTER_CRITICAL(&mux);request=requested_page;requested_page=-1;portEXIT_CRITICAL(&mux);
  const auto prior=view;if(request>=0){view.page=uint8_t(request);view.help=false;view.blank=false;clock_hold.reset();force=true;}
  if(input.update(gpio_get_level(GPIO_NUM_0)==0,now,view)==n30::Action::ToggleRelayOnly){portENTER_CRITICAL(&mux);upstream_toggle.post(now);portEXIT_CRITICAL(&mux);}
  uint64_t utc;input.idle(now,m.locked&&m.source!=n30::Source::None&&m.state==n30::State::Follow&&n30::time(m,now,utc),view);
  clock_hold.update(m,view,now,input.saver_due(now,view));
  if(prior.page!=view.page||prior.help!=view.help||prior.saver!=view.saver||prior.blank!=view.blank||prior.clock_frozen!=view.clock_frozen)force=true;
  const auto phase=n30::frame_phase(m,view,now);const auto due=cadence.deadline(phase,now);
  if(force||now>=due){
   bool ok=true;if(off!=view.blank){ok=command(view.blank?0xae:0xaf);if(ok){off=view.blank;shadow_valid=false;}}
   if(ok&&dim!=view.saver){ok=command(0x81)&&command(view.saver?0x20:0x60);if(ok)dim=view.saver;}
   if(ok&&!view.blank){clock_hold.draw(portrait,m,view,now);n30::rotate(native,portrait,PTOB_N30_ROTATE_CW);ok=flush(now+50000);if(ok)clock_hold.presented(m,view,now,portrait);}
   if(!ok)clock_hold.reset();
   const auto end=now_us();portENTER_CRITICAL(&mux);stats.page=view.page;stats.help=view.help;stats.saver=view.saver;stats.blank=view.blank;stats.sampled=m.sampled_us;stats.ready=ok;
   if(ok)stats.frames++;else stats.errors++;stats.max_us=std::max(stats.max_us,uint32_t(end-now));
   if(!force&&phase.utc){
    stats.max_phase_us=std::max(stats.max_phase_us,uint32_t(std::min<uint64_t>(now-due,UINT32_MAX)));
   }
   portEXIT_CRITICAL(&mux);
   cadence.rendered(phase);force=false;
  }
  const auto end=now_us();const auto next=cadence.deadline(n30::frame_phase(m,view,end),end);
  if(next<=end)continue;
  if(next!=armed_due){
   if(armed_due)esp_timer_stop(frame_timer); // Already-expired timer is harmless.
   if(esp_timer_start_once(frame_timer,next-end)==ESP_OK)armed_due=next;
   else{armed_due=0;portENTER_CRITICAL(&mux);stats.errors++;portEXIT_CRITICAL(&mux);}
  }
  // Precise frame wake interrupts the ordinary10ms button poll. The timer
  // callback only notifies; all rendering/I2C remains on this low-priority core1.
  ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(10));
 }
}
}
bool display_init(){
 uint8_t factory[6]{};const auto* device=v12::native_device_binding();
 if(esp_read_mac(factory,ESP_MAC_EFUSE_FACTORY)!=ESP_OK||std::memcmp(factory,device->factory,6))return false;
 gpio_config_t c{};c.pin_bit_mask=(1ULL<<36)|(1ULL<<21);c.mode=GPIO_MODE_OUTPUT;
 if(gpio_config(&c)!=ESP_OK||gpio_set_level(GPIO_NUM_36,0)!=ESP_OK||gpio_set_level(GPIO_NUM_21,0)!=ESP_OK)return false;
 vTaskDelay(pdMS_TO_TICKS(10));if(gpio_set_level(GPIO_NUM_21,1)!=ESP_OK)return false;vTaskDelay(pdMS_TO_TICKS(100));
 i2c_master_bus_config_t b{};b.i2c_port=I2C_NUM_0;b.sda_io_num=GPIO_NUM_17;b.scl_io_num=GPIO_NUM_18;b.clk_source=I2C_CLK_SRC_DEFAULT;b.glitch_ignore_cnt=7;b.flags.enable_internal_pullup=true;
 if(i2c_new_master_bus(&b,&bus)!=ESP_OK)return false;
 i2c_device_config_t d{};d.dev_addr_length=I2C_ADDR_BIT_LEN_7;d.device_address=0x3c;d.scl_speed_hz=400000;
 if(i2c_master_probe(bus,0x3c,20)!=ESP_OK||i2c_master_bus_add_device(bus,&d,&oled)!=ESP_OK)return false;
 const uint8_t init[]={0xae,0xd5,0x80,0xa8,0x3f,0xd3,0,0x40,0x8d,0x14,0x20,0x02,0xa1,0xc8,0xda,0x12,0x81,0x60,0xd9,0xf1,0xdb,0x40,0xa4,0xa6,0x2e,0xaf};
 for(auto x:init)if(!command(x))return false;
 esp_timer_create_args_t timer{};timer.callback=frame_wake;timer.name="n30_frame";
 if(esp_timer_create(&timer,&frame_timer)!=ESP_OK)return false;
 if(xTaskCreatePinnedToCore(worker,"n30_ui",4096,nullptr,1,&ui_task,1)==pdPASS)return true;
 esp_timer_delete(frame_timer);frame_timer=nullptr;return false;
}
void n30_display_publish(const v12::R2Status& s,const DisplayState& d,uint64_t now){
 auto m=n30::model(s,d,now);const auto radio=v12::r2_activity();
 // Producer counters retain their actual meaning. RDY has no equivalent
 // packet/event in this backend and remains dark. TX is controller/API only.
 const uint32_t counts[]={s.g1_adv_callbacks,s.g1_group_records,s.pa_reports,s.join_accepts,s.g1_corrected_accepted,radio.tx,0,s.rejected+s.drops+s.api_errors};
 activity.update(m,counts,s.root_identity,s.root_boot,now,s.requested&&!s.fault,radio.tx_armed);
 portENTER_CRITICAL(&mux);published=m;portEXIT_CRITICAL(&mux);
}
void n30_display_status(v12::ConsoleFrame& f){Stats s;n30::Model m;portENTER_CRITICAL(&mux);s=stats;m=published;portEXIT_CRITICAL(&mux);
 f.format("N30_UI profile=RC GPS=ABSENT page=%u ready=%u help=%u saver=%u blank=%u frames=%lu errors=%lu max_frame_us=%lu model_us=%llu rotation=%s display=64x128 hardware=UNVALIDATED path=%s upstream=%s phase_us=%lu\n",unsigned(s.page),unsigned(s.ready),unsigned(s.help),unsigned(s.saver),unsigned(s.blank),(unsigned long)s.frames,(unsigned long)s.errors,(unsigned long)s.max_us,(unsigned long long)s.sampled,PTOB_N30_ROTATE_CW?"CW":"CCW",n30::source_label(m,now_us()),m.mode_pending?"PENDING":m.relay_only?"RELAY":"AUTO",(unsigned long)s.max_phase_us);
}
// Legacy presentation API retained only for the shared dashboard adapter.
void display_show(const DisplayState&){}
bool display_set_scan(DisplayTiming::Profile){return false;}
void display_update_indicators(bool,bool,bool){}
void display_sleep(){}void display_wake(){}void display_blank(){}

bool n30_display_page(unsigned page){if(page>1)return false;portENTER_CRITICAL(&mux);requested_page=int(page);portEXIT_CRITICAL(&mux);if(ui_task)xTaskNotifyGive(ui_task);return true;}
bool n30_display_take_upstream_toggle(uint64_t now){portENTER_CRITICAL(&mux);const bool take=upstream_toggle.take(now);portEXIT_CRITICAL(&mux);return take;}

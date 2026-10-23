// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// One low-priority display/button owner; no BLE APIs, authority or clock writes.
#include "n30_display.hpp"
#include "ui.hpp"
#include "status_adapter.hpp"
#include "display_phase.hpp"
#include "dual_time.hpp"
#include "rc9_time.hpp"
#include "fine_backend_api.hpp"
#include "relative_rate.hpp"
#include "n30_lora.hpp"
#include "ptob_registry.h"
#include "display_budget.hpp"
#include "native_device_binding.hpp"
#include "client_lifecycle.hpp"
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
n30::Model published; n30::Activity activity,lora_activity;n30::DualComparison comparison;n30::LockTimer precision_lock; int requested_page=-1;
n30::ActionMailbox upstream_toggle,lora_toggle;
struct Stats {uint32_t frames=0,errors=0,max_us=0,max_phase_us=0;uint64_t sampled=0;uint8_t page=0;bool ready=false,help=false,saver=false,blank=false,minimal=false;} stats;
TaskHandle_t ui_task=nullptr;esp_timer_handle_t frame_timer=nullptr;
bool minimal_wait=false;
struct SurfaceRecord {
 uint64_t at=0,end=0,loss=0,sampled=0,until=0;
 uint32_t sequence=0;uint8_t from=0,saver=0,minimal=0,page=0,due=0,valid=0,locked=0,state=0,source=0,written=0,ok=0;
};
v12::Ring<SurfaceRecord,4> surfaces;uint32_t surface_sequence=0;
void frame_wake(void*){if(ui_task)xTaskNotifyGive(ui_task);}
void button_wake(void*){
 BaseType_t higher=pdFALSE;
 if(ui_task)vTaskNotifyGiveFromISR(ui_task,&higher);
 if(higher)portYIELD_FROM_ISR();
}
uint8_t portrait[n30::FrameBytes],native[n30::FrameBytes],shadow[n30::FrameBytes];bool shadow_valid=false;
uint64_t now_us(){return uint64_t(esp_timer_get_time());}
bool command(uint8_t value){uint8_t data[]={0,value};return i2c_master_transmit(oled,data,sizeof data,10)==ESP_OK;}
bool send_before(const uint8_t* data,size_t size,uint64_t deadline){
 const int timeout=n30::transfer_timeout(deadline,now_us(),size);
 return timeout&&i2c_master_transmit(oled,data,size,timeout)==ESP_OK&&now_us()<deadline;
}
bool flush(uint64_t deadline,uint8_t* written){
 return n30::flush_clock_first(native,shadow,shadow_valid,PTOB_N30_ROTATE_CW,[&](unsigned page,unsigned first,const uint8_t* data,unsigned size){
  const uint8_t addr[]={0,uint8_t(0xb0|page),uint8_t(first&15),uint8_t(0x10|(first>>4))};
  uint8_t packet[129];packet[0]=0x40;std::memcpy(packet+1,data,size);
  const bool ok=send_before(addr,sizeof addr,deadline)&&send_before(packet,size+1,deadline);
  if(ok)*written|=uint8_t(1u<<page);
  return ok;
 });
}
void worker(void*){
 static n30::ClockHold clock_hold; // One display owner; framebuffer stays off stack.
 n30::BootSplash splash;bool splash_done=false;
 n30::Input input(gpio_get_level(GPIO_NUM_0)==0);n30::View view;n30::FrameCadence cadence;n30::SaverTimer saver_timer;
 uint64_t armed_due=0,exit_loss_us=0;bool force=true,off=false,dim=false,attempted=false,attempted_saver=false;
 for(;;){
  n30::Model m;portENTER_CRITICAL(&mux);m=published;portEXIT_CRITICAL(&mux);
  const auto now=now_us(); // Evaluate a copied snapshot at/after its publication.
  if(splash.active(m,now)){
   if(!splash.shown()){
    n30::render_splash(portrait);n30::rotate(native,portrait,PTOB_N30_ROTATE_CW);
    uint8_t written=0;const bool ok=flush(now+50000,&written);const auto end=now_us();
    if(ok)splash.presented(end);
    portENTER_CRITICAL(&mux);stats.ready=ok;if(ok)stats.frames++;else stats.errors++;
    stats.max_us=std::max(stats.max_us,uint32_t(end-now));portEXIT_CRITICAL(&mux);
   }
   // No radio owner, clock fitting or main task waits for this display deadline.
   ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(10));continue;
  }
  if(!splash_done){splash_done=true;input.activity(now);force=true;}
  int request;portENTER_CRITICAL(&mux);request=requested_page;requested_page=-1;portEXIT_CRITICAL(&mux);
  const auto prior=view;if(request>=0){view.page=uint8_t(request);view.help=false;view.blank=false;input.activity(now);clock_hold.reset();force=true;}
  const auto action=input.update(gpio_get_level(GPIO_NUM_0)==0,now,view);
  if(action==n30::Action::ToggleRelayOnly||action==n30::Action::ToggleLoRa){portENTER_CRITICAL(&mux);(action==n30::Action::ToggleRelayOnly?upstream_toggle:lora_toggle).post(now);portEXIT_CRITICAL(&mux);}
  const auto loss_deadline=saver_timer.deadline();
  n30::update_saver(input,clock_hold,saver_timer,m,view,now);
  if(prior.saver&&!view.saver)exit_loss_us=loss_deadline!=UINT64_MAX&&loss_deadline>=n30::ClockGraceUs&&now>=loss_deadline-n30::ClockGraceUs?now-(loss_deadline-n30::ClockGraceUs):0;
  portENTER_CRITICAL(&mux);minimal_wait=view.saver&&view.minimal;portEXIT_CRITICAL(&mux);
  if(prior.page!=view.page||prior.help!=view.help||prior.saver!=view.saver||prior.blank!=view.blank||prior.clock_frozen!=view.clock_frozen||prior.minimal!=view.minimal)force=true;
  const auto phase=n30::frame_phase(m,view,now);const auto due=force?cadence.forced_deadline(phase,now):cadence.deadline(phase,now);
  if(now>=due){
   bool ok=true;if(off!=view.blank){ok=command(view.blank?0xae:0xaf);if(ok){off=view.blank;shadow_valid=false;}}
   if(ok&&dim!=view.saver){ok=command(0x81)&&command(view.saver?n30::SaverContrast:n30::NormalContrast);if(ok)dim=view.saver;}
   uint8_t written=0;
   if(ok&&!view.blank){clock_hold.draw(portrait,m,view,now);n30::rotate(native,portrait,PTOB_N30_ROTATE_CW);ok=flush(now+50000,&written);if(ok)clock_hold.presented(m,view,now,portrait);}
   if(!ok)clock_hold.reset();
   const auto end=now_us();portENTER_CRITICAL(&mux);stats.page=view.page;stats.help=view.help;stats.saver=view.saver;stats.blank=view.blank;stats.minimal=view.minimal;stats.sampled=m.sampled_us;stats.ready=ok;
   if(ok)stats.frames++;else stats.errors++;stats.max_us=std::max(stats.max_us,uint32_t(end-now));
   if(!force&&phase.utc){
    stats.max_phase_us=std::max(stats.max_phase_us,uint32_t(std::min<uint64_t>(now-due,UINT32_MAX)));
   }
   portEXIT_CRITICAL(&mux);
   // Freeze attempted transitions, including partial writes. The main owner
   // formats them later: the display task must not carry the log formatter.
   if((attempted&&attempted_saver!=view.saver)||!ok){
    SurfaceRecord r{now,end,exit_loss_us,m.sampled_us,m.until_us,0,uint8_t(attempted_saver),uint8_t(view.saver),uint8_t(view.minimal),view.page,uint8_t(input.saver_due(now,view)),uint8_t(m.valid),uint8_t(m.locked),uint8_t(m.state),uint8_t(m.source),written,uint8_t(ok)};
    portENTER_CRITICAL(&mux);if(surface_sequence!=UINT32_MAX){r.sequence=++surface_sequence;surfaces.push(r);}portEXIT_CRITICAL(&mux);
   }
   if(!view.blank){attempted=true;attempted_saver=view.saver;}
   cadence.completed(phase,n30::frame_phase(m,view,end));force=false;
  }
  const auto end=now_us();const auto end_phase=n30::frame_phase(m,view,end);
  auto next=force?cadence.forced_deadline(end_phase,end):cadence.deadline(end_phase,end);
  if(view.minimal){
   uint64_t utc;
   if(n30::saver_time(m,end,utc))next=std::min(next,std::min(m.until_us,m.sampled_us+n30::SnapshotMaxUs));
   const auto grace=std::min(clock_hold.deadline(),saver_timer.deadline());if(grace>end)next=std::min(next,grace);
  }
  if(next<=end)continue;
  if(next!=armed_due){
   if(armed_due)esp_timer_stop(frame_timer); // Already-expired timer is harmless.
   if(esp_timer_start_once(frame_timer,next-end)==ESP_OK)armed_due=next;
   else{armed_due=0;portENTER_CRITICAL(&mux);stats.errors++;portEXIT_CRITICAL(&mux);}
  }
  // Precise frame wake interrupts the ordinary10ms button poll. The timer
  // callback only notifies; all rendering/I2C remains on this low-priority core1.
  // Minimal mode has no10ms idle polling. GPIO edges, fresh models and the
  // next pulse edge wake us; debounce/held gestures retain their old cadence.
  ulTaskNotifyTake(pdTRUE,view.minimal&&!input.polling()&&armed_due?portMAX_DELAY:pdMS_TO_TICKS(10));
 }
}
}
void n30_display_trace_poll(){
 SurfaceRecord r;uint32_t dropped;portENTER_CRITICAL(&mux);const bool present=surfaces.pop(r);dropped=surfaces.drops;portEXIT_CRITICAL(&mux);
 if(present)v12::logf("N30_UI_SURFACE seq=%lu dropped=%lu at_us=%llu end_us=%llu from_saver=%u saver=%u minimal=%u page=%u due=%u loss_us=%llu model_us=%llu until_us=%llu valid=%u locked=%u state=%u source=%u written=%02x ok=%u\n",
   (unsigned long)r.sequence,(unsigned long)dropped,(unsigned long long)r.at,(unsigned long long)r.end,unsigned(r.from),unsigned(r.saver),unsigned(r.minimal),unsigned(r.page),unsigned(r.due),(unsigned long long)r.loss,(unsigned long long)r.sampled,(unsigned long long)r.until,unsigned(r.valid),unsigned(r.locked),unsigned(r.state),unsigned(r.source),unsigned(r.written),unsigned(r.ok));
 static uint64_t rf_at=0;const auto now=now_us();
 if(now<rf_at){rf_at=now;return;}
 if(now-rf_at<1000000)return;
 rf_at=now;if(!v12::log_status().capture_active)return;
 n30::Signal signal;uint64_t sampled;bool lora;
 portENTER_CRITICAL(&mux);signal=published.signal;sampled=published.sampled_us;lora=published.source==n30::Source::LoRa;portEXIT_CRITICAL(&mux);
 const auto bars=!lora&&now>=sampled&&now-sampled<n30::SnapshotMaxUs?n30::signal_bars(signal,now):0;
 v12::logf("N30_UI_RF at_us=%llu peer=%016llx epoch=%lu sample_us=%llu rssi_dbm=%d bars=%u\n",(unsigned long long)now,(unsigned long long)signal.peer,(unsigned long)signal.epoch,(unsigned long long)signal.at,int(signal.dbm),unsigned(bars));
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
 const auto isr=gpio_install_isr_service(0);
 if((isr!=ESP_OK&&isr!=ESP_ERR_INVALID_STATE)||gpio_set_intr_type(GPIO_NUM_0,GPIO_INTR_ANYEDGE)!=ESP_OK||gpio_isr_handler_add(GPIO_NUM_0,button_wake,nullptr)!=ESP_OK)return false;
 if(xTaskCreatePinnedToCore(worker,"n30_ui",4096,nullptr,1,&ui_task,1)==pdPASS)return true;
 esp_timer_delete(frame_timer);frame_timer=nullptr;return false;
}
void n30_display_publish(const v12::R2Status& s,const DisplayState& d,uint64_t now){
 auto m=n30::model(s,d,now,ptob_registry_name(s.root_identity));
 // This is the boot-selected delivered counter, independent of raw diagnostics.
 // Fine delivery still rounds to50ns; the label reports counter tick size only.
 m.precision.counter_hz=n30_fine_backend_selected()?80000000:20000000;
 const auto rate=n30_relative_rate_snapshot(now);
 n30::relative_rate(m,rate.root,rate.boot,rate.source_epoch,rate.ppb,rate.valid,rate.sampled_us,now);
 precision_lock.update(m,now);
 n30_lora_view lora;n30_lora_snapshot(lora);now=now_us();m.sampled_us=now;
 n30::select_lora(m,lora,now);comparison.update(m,now);
 const auto rc9=v12::r2_rc9_status();now=now_us();m.sampled_us=now;
 n30::select_rc9(m,rc9,now,ptob_registry_name(rc9.server),ptob_registry_name(rc9.root));
 if(m.source==n30::Source::LoRa){const auto* name=ptob_registry_name(lora.emitter);if(name)std::snprintf(m.parent,sizeof m.parent,"%s",name);else std::snprintf(m.parent,sizeof m.parent,"%04X",unsigned(lora.emitter&65535));}
 const auto radio=v12::r2_activity();
 // Producer counters retain their actual meaning. RDY has no equivalent
 // packet/event in this backend and remains dark. TX is controller/API only.
 const uint32_t counts[]={s.g1_adv_callbacks,s.g1_group_records,s.pa_reports,s.join_accepts,s.g1_corrected_accepted,radio.tx,0,s.rejected+s.drops+s.api_errors};
 activity.update(m,counts,s.root_identity,s.root_boot,now,s.requested&&!s.fault,radio.tx_armed);
 n30::Model events;const uint32_t lora_counts[]={lora.metadata,lora.sequence,lora.rx_done,0,lora.accepted,0,lora.map_accepted,lora.rejected+lora.crc_bad+lora.spi_error+lora.capture_overflow};
 lora_activity.update(events,lora_counts,lora.root,lora.root_boot,now,lora.active&&!lora.fault,false);std::memcpy(m.lora_lamps,events.lamps,sizeof m.lora_lamps);
 portENTER_CRITICAL(&mux);published=m;const bool wake=minimal_wait;portEXIT_CRITICAL(&mux);
 if(wake&&ui_task)xTaskNotifyGive(ui_task); // Rephase/withdraw a lost source promptly.
}
void n30_display_status(v12::ConsoleFrame& f){Stats s;n30::Model m;portENTER_CRITICAL(&mux);s=stats;m=published;portEXIT_CRITICAL(&mux);
 f.format("N30_UI profile=RC GPS=ABSENT page=%u ready=%u help=%u saver=%u blank=%u frames=%lu errors=%lu max_frame_us=%lu model_us=%llu rotation=%s display=64x128 hardware=UNVALIDATED path=%s upstream=%s phase_us=%lu m=%u\n",unsigned(s.page),unsigned(s.ready),unsigned(s.help),unsigned(s.saver),unsigned(s.blank),(unsigned long)s.frames,(unsigned long)s.errors,(unsigned long)s.max_us,(unsigned long long)s.sampled,PTOB_N30_ROTATE_CW?"CW":"CCW",n30::source_label(m,now_us()),m.mode_pending?"PENDING":m.relay_only?"RELAY":"AUTO",(unsigned long)s.max_phase_us,unsigned(s.minimal));
}
// Legacy presentation API retained only for the shared dashboard adapter.
void display_show(const DisplayState&){}
bool display_set_scan(DisplayTiming::Profile){return false;}
void display_update_indicators(bool,bool,bool){}
void display_sleep(){}void display_wake(){}void display_blank(){}

bool n30_display_page(unsigned page){if(page>=n30::PageCount)return false;portENTER_CRITICAL(&mux);requested_page=int(page);portEXIT_CRITICAL(&mux);if(ui_task)xTaskNotifyGive(ui_task);return true;}
bool n30_display_take_upstream_toggle(uint64_t now){portENTER_CRITICAL(&mux);const bool take=upstream_toggle.take(now);portEXIT_CRITICAL(&mux);return take;}

bool n30_display_take_lora_toggle(uint64_t now){portENTER_CRITICAL(&mux);const bool take=lora_toggle.take(now);portEXIT_CRITICAL(&mux);return take;}

bool n30_display_minimal(){portENTER_CRITICAL(&mux);const bool value=minimal_wait;portEXIT_CRITICAL(&mux);return value;}

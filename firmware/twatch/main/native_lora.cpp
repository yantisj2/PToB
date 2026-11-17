// Selected N30 adapter; provenance in imports.json.
// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// SX1262 RX-only owner for LILYGO T-Watch Ultra. No SetTx opcode, BLE map update or
// listener transmission. Actual MCPWM DIO1 capture, never an ISR arrival time.
#include "native_lora.hpp"
#include "n30_lora.hpp"
#include "lora_core.h"
#include "lora_shadow.hpp"
#include "lora_shadow_input.h"
#include "lora_owner_gate.hpp"
#include "ptob_gnss_reference.hpp"
#include "lora_mapping.hpp"
#include "lora_native_clock.hpp"
#include "n30_lora_config.h"
#include "driver/spi_master.h"
#include "driver/mcpwm_cap.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstring>
#include <cerrno>
#include <cinttypes>
extern "C" bool n30_lora_reference_read(uint64_t*);
extern "C" bool n30_lora_reference_bracket(mcpwm_cap_channel_handle_t,uint64_t*,uint64_t*);
extern "C" int n30_lora_reference_current(mcpwm_cap_channel_handle_t,uint32_t*,uint64_t*);
extern "C" void watch_lora_shadow_event(uint8_t,const uint8_t*,uint64_t,uint32_t,uint64_t,uint64_t,bool);
namespace {
constexpr auto Nss=GPIO_NUM_36,Reset=GPIO_NUM_47,Busy=GPIO_NUM_48,Irq=GPIO_NUM_14;
constexpr uint16_t RxGainReg=0x08ac;
constexpr uint8_t RxGainBoosted=0x96;
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
v12::LoRaOwnerGate owner;v12::LoRaStatus basic,owned;n30_lora_view published,hardware;
n30::LoRaPolicy policy;n30::LoRaCounterMap mapping;ptob_gnss::CounterMap::Anchor qualified_map;
n30::LoRaNativeClock native_clock;
int native_error=0;
struct CurrentFailure {int32_t rc=0;uint8_t reason=0;uint32_t raw=0,previous_raw=0;uint64_t sampled=0,previous_us=0,available=0,epoch=0;bool set=false;} current_failure;
uint32_t previous_current_raw=0;uint64_t previous_current_us=0;
spi_device_handle_t spi=nullptr;spi_transaction_t transaction{};
DMA_ATTR uint8_t tx[132]{},rx[132]{};
mcpwm_cap_timer_handle_t cap_timer=nullptr;mcpwm_cap_channel_handle_t edge_channel=nullptr,map_channel=nullptr,current_channel=nullptr;
TaskHandle_t task=nullptr;
uint64_t ble_at=0,worker_published_us=0,last_map=0,map_pending=0,map_before=0,map_after=0,stop_at=0;
uint64_t hold_pending_id=0,hold_pending_us=0,hold_last_id=0;uint8_t hold_result=0;
uint32_t cancel_serial=0,active_serial=0,core_generation=0,map_serial=0,map_expected=0,map_capture=0,request_serial=0,published_serial=0;
uint32_t raw_capture=0,raw_generation=0,raw_serial=0;
uint32_t header_capture=0,header_generation=0,header_serial=0;uint64_t header_until=0;bool header_pending=false;
uint64_t capture_epoch=0;
uint8_t requested_mode=0;bool mode_pending=false,cycle_pending=false,ble_locked=false,allowed=false,ble_parked=false;
bool ble_lock_observed=false; // Worker-owned actual fresh policy input.
bool resources=false,capture_active=false,capture_ready=false,inflight=false,latched=false,failed=false,off_intent=false;
bool cleanup_failed=false,published_cleanup_fault=false;
bool recover_pending=false;uint64_t recover_requested_at=0;
uint32_t recover_owner_generation=0,recover_cancel_serial=0;
bool timer_enabled=false,timer_started=false,map_enabled=false,edge_enabled=false,current_enabled=false,emergency=false;
uint32_t capture_drops=0,capture_invalid=0;
watch::LoRaSleepView sleep_state,sleep_published;
bool radio_sleeping=false;
watch::LoRaShadow shadow;watch::LoRaShadow::View shadow_published;
uint64_t now(){return uint64_t(esp_timer_get_time());}
void inc(uint32_t& v){if(v<UINT32_MAX)++v;}
// LoRa owns an independent clock. Keep its short mapping brackets on the
// original XTAL GPTimer, outside the BLE fine receiver's lock and fence work.
bool local(uint64_t& tick){return n30_lora_reference_read(&tick)&&tick;}
[[gnu::noinline]] bool native_local(uint64_t& tick16,uint64_t* sampled_us=nullptr){
 tick16=0;native_error=-12288;
 if(!timer_enabled||!timer_started||!current_enabled)return false;
 uint32_t raw=0;uint64_t sampled=0;
 const int rc=n30_lora_reference_current(current_channel,&raw,&sampled);
 const bool valid=!rc&&native_clock.current(capture_epoch,raw,sampled,tick16);
 if(!valid){
  const uint8_t reason=rc?0:native_clock.fault();native_error=rc?-4096-rc:-8192-reason;
  if(!current_failure.set)current_failure={rc,reason,raw,previous_current_raw,sampled,previous_current_us,now(),capture_epoch,true};
  return false;
 }
 previous_current_raw=raw;previous_current_us=sampled;if(sampled_us)*sampled_us=sampled;
 native_error=0;return true;
}
bool cancelled(){portENTER_CRITICAL(&mux);const bool result=cancel_serial!=active_serial||!allowed;portEXIT_CRITICAL(&mux);return result;}
void reset_radio(){if(resources)gpio_set_level(Reset,0);}
// Called with mux held. Repeated OFF observations never renew its generation
// or original200ms drain; a new ON still requires worker retirement of this serial.
bool revoke_locked(uint64_t at,bool permission=true,bool barrier=false){
 const bool fence=barrier||owner.requested||(permission&&allowed);
 if(permission){allowed=false;off_intent=false;}
 mode_pending=cycle_pending=false;hold_pending_id=0;recover_pending=false;
 if(fence){
  if(cancel_serial==UINT32_MAX||!owner.request(false))emergency=true;
  else ++cancel_serial;
 }
 if(!stop_at&&!owner.drained())stop_at=at;
 reset_radio();return !emergency;
}
void fault(int error){n30_rc8_radio_fault();reset_radio();if(!failed)hardware.error=error;failed=true;hardware.fault=true;}
void cleanup_fault(int error){cleanup_failed=true;fault(error);}
bool status_ok(uint8_t value){const auto s=(value>>1)&7;return value&&value!=255&&s!=3&&s!=4&&s!=5;}
int busy(){
 const auto deadline=now()+30000;
 while(gpio_get_level(Busy)){
  if(cancelled())return -ECANCELED;
  if(now()>=deadline)return -ETIMEDOUT;
  vTaskDelay(pdMS_TO_TICKS(1));
 }
 return cancelled()?-ECANCELED:0;
}
int transfer(size_t size,bool entering_sleep=false){
 if(inflight||size>sizeof tx||!size)return -EBUSY;
 int result=busy();if(result)return result;
 std::memset(rx,0,size);transaction={};transaction.length=size*8;transaction.tx_buffer=tx;transaction.rx_buffer=rx;
 gpio_set_level(Nss,0);
 if(spi_device_queue_trans(spi,&transaction,pdMS_TO_TICKS(5))!=ESP_OK){gpio_set_level(Nss,1);inc(hardware.spi_error);return -EIO;}
 // Queue/result APIs accept finite waits. The polling-start API only permits
 // portMAX_DELAY on the pinned SDK and is deliberately not used.
 inflight=true;
 spi_transaction_t* completed=nullptr;
 if(spi_device_get_trans_result(spi,&completed,pdMS_TO_TICKS(5))!=ESP_OK||completed!=&transaction){inc(hardware.spi_error);return -ETIMEDOUT;}
 inflight=false;gpio_set_level(Nss,1);inc(hardware.spi_ok);
 if(cancelled())return -ECANCELED;
 // BUSY stays high throughout sleep: no ordinary post-command BUSY wait.
 if(entering_sleep)return 0;
 result=busy();return result;
}
int command(uint8_t op,const uint8_t* args,size_t size){
 hardware.opcode=op;tx[0]=op;if(size)std::memcpy(tx+1,args,size);return transfer(size+1);
}
int reg_write(uint16_t address,uint8_t value){const uint8_t args[]={uint8_t(address>>8),uint8_t(address),value};return command(0x0d,args,sizeof args);}
int reg_read(uint16_t address,uint8_t& value){const uint8_t args[]={uint8_t(address>>8),uint8_t(address),0,0};const int rc=command(0x1d,args,sizeof args);if(rc)return rc;if(!status_ok(rx[3]))return -EIO;value=rx[4];return 0;}
bool on_edge(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t* e,void* cookie){
 BaseType_t wake=pdFALSE;portENTER_CRITICAL_ISR(&mux);
 if(!capture_active||!e||uint32_t(reinterpret_cast<uintptr_t>(cookie))!=core_generation){inc(capture_invalid);}
 else if(latched){inc(capture_drops);}
 else{raw_capture=e->cap_value;raw_generation=core_generation;raw_serial=active_serial;latched=true;}
 portEXIT_CRITICAL_ISR(&mux);if(task)vTaskNotifyGiveFromISR(task,&wake);return wake==pdTRUE;
}
bool on_map(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t* e,void* cookie){
 portENTER_CRITICAL_ISR(&mux);if(capture_active&&e&&uint32_t(reinterpret_cast<uintptr_t>(cookie))==core_generation){map_capture=e->cap_value;map_serial++;}portEXIT_CRITICAL_ISR(&mux);return false;
}
void map_clear(){mapping.reset();qualified_map={};}
int capture_start(){
 if(!cap_timer){
  mcpwm_capture_timer_config_t cfg{};cfg.group_id=0;cfg.clk_src=MCPWM_CAPTURE_CLK_SRC_APB;
  uint32_t hz=0;
  if(mcpwm_new_capture_timer(&cfg,&cap_timer)!=ESP_OK||mcpwm_capture_timer_get_resolution(cap_timer,&hz)!=ESP_OK||hz!=80000000)return -EIO;
  mcpwm_capture_channel_config_t ch{};ch.gpio_num=Irq;ch.prescale=1;ch.flags.pos_edge=1;
  if(mcpwm_new_capture_channel(cap_timer,&ch,&edge_channel)!=ESP_OK)return -EIO;
  ch={};ch.gpio_num=-1;ch.prescale=1;
  if(mcpwm_new_capture_channel(cap_timer,&ch,&map_channel)!=ESP_OK)return -EIO;
  // CH2 is CURRENT-only, polled under the pinned driver lock. No callback,
  // IRQ, GPIO route or BLE/group1 owner is attached to this channel.
  if(mcpwm_new_capture_channel(cap_timer,&ch,&current_channel)!=ESP_OK)return -EIO;
  capture_ready=true;
 }
 if(capture_epoch==UINT64_MAX||!core_generation)return -EOVERFLOW;
 const auto cookie=reinterpret_cast<void*>(uintptr_t(core_generation));
 const mcpwm_capture_event_callbacks_t cb={.on_cap=on_edge},mb={.on_cap=on_map};
 if(mcpwm_capture_channel_register_event_callbacks(edge_channel,&cb,cookie)!=ESP_OK||mcpwm_capture_channel_register_event_callbacks(map_channel,&mb,cookie)!=ESP_OK)return -EIO;
 map_clear();native_clock.reset();last_map=map_pending=0;++capture_epoch;
 if(mcpwm_capture_timer_enable(cap_timer)!=ESP_OK)return -EIO;
 timer_enabled=true;
 if(mcpwm_capture_timer_start(cap_timer)!=ESP_OK)return -EIO;
 timer_started=true;
 if(mcpwm_capture_channel_enable(map_channel)!=ESP_OK)return -EIO;
 map_enabled=true;
 if(mcpwm_capture_channel_enable(current_channel)!=ESP_OK)return -EIO;
 current_enabled=true;
 if(mcpwm_capture_channel_enable(edge_channel)!=ESP_OK)return -EIO;
 edge_enabled=true;
 portENTER_CRITICAL(&mux);latched=false;capture_active=true;portEXIT_CRITICAL(&mux);return 0;
}
void capture_stop(){
 portENTER_CRITICAL(&mux);capture_active=false;portEXIT_CRITICAL(&mux);
 if(edge_enabled){if(mcpwm_capture_channel_disable(edge_channel)!=ESP_OK)cleanup_fault(-EIO);else edge_enabled=false;}
 if(map_enabled){if(mcpwm_capture_channel_disable(map_channel)!=ESP_OK)cleanup_fault(-EIO);else map_enabled=false;}
 if(current_enabled){if(mcpwm_capture_channel_disable(current_channel)!=ESP_OK)cleanup_fault(-EIO);else current_enabled=false;}
 if(timer_started){if(mcpwm_capture_timer_stop(cap_timer)!=ESP_OK)cleanup_fault(-EIO);else timer_started=false;}
 if(timer_enabled){if(mcpwm_capture_timer_disable(cap_timer)!=ESP_OK)cleanup_fault(-EIO);else timer_enabled=false;}
 map_clear();native_clock.reset();map_pending=0;header_pending=false;portENTER_CRITICAL(&mux);latched=false;portEXIT_CRITICAL(&mux);
}
void map_step(){
 uint64_t tick;if(!local(tick))return;
 uint32_t serial,cap;portENTER_CRITICAL(&mux);serial=map_serial;cap=map_capture;portEXIT_CRITICAL(&mux);
 if(map_pending&&serial!=map_expected){
  const ptob_gnss::CounterMap::Anchor candidate{map_before,map_after,tick,cap};
  const bool one=serial==map_expected+1;
  const bool observed=one&&mapping.observe(candidate);
  if(observed&&mapping.release(tick))qualified_map=candidate;
  else{
   // A rejected wide software bracket cannot erase a still-qualified map.
   // Keep the exact old anchors/expiry only after all continuity checks pass.
   if(one&&!observed&&n30::recoverable_map_bracket(candidate,qualified_map))inc(hardware.map_soft_refused);
   else{map_clear();inc(hardware.map_hard_reset);}
   inc(hardware.map_rejected);
  }
  map_pending=0;
 }else if(map_pending&&tick>=map_pending&&tick-map_pending>2000000){map_clear();map_pending=0;inc(hardware.map_rejected);inc(hardware.map_hard_reset);}
 if(!map_pending&&(!last_map||tick-last_map>=5000000)){
  portENTER_CRITICAL(&mux);map_expected=map_serial;
  const bool valid=n30_lora_reference_bracket(map_channel,&map_before,&map_after);portEXIT_CRITICAL(&mux);
  if(!valid){map_clear();inc(hardware.map_rejected);inc(hardware.map_hard_reset);return;}
  hardware.map_bracket_ns=map_after>=map_before&&map_after-map_before<=UINT32_MAX/50?uint32_t((map_after-map_before)*50):UINT32_MAX;
  last_map=map_pending=map_before;
 }
 hardware.map_accepted=mapping.accepted();
}
int resources_start(){
 if(resources)return 0;
 gpio_config_t outputs{};outputs.pin_bit_mask=(1ULL<<Nss)|(1ULL<<Reset);outputs.mode=GPIO_MODE_INPUT_OUTPUT;
 if(gpio_set_level(Nss,1)!=ESP_OK||gpio_set_level(Reset,0)!=ESP_OK||gpio_config(&outputs)!=ESP_OK)return -EIO;
 gpio_config_t inputs{};inputs.pin_bit_mask=(1ULL<<Busy)|(1ULL<<Irq);inputs.mode=GPIO_MODE_INPUT;
 if(gpio_config(&inputs)!=ESP_OK)return -EIO;
 spi_bus_config_t bus{};bus.mosi_io_num=34;bus.miso_io_num=33;bus.sclk_io_num=35;bus.quadwp_io_num=bus.quadhd_io_num=-1;bus.max_transfer_sz=sizeof tx;
 if(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO)!=ESP_OK)return -EIO;
 spi_device_interface_config_t device{};device.clock_speed_hz=2000000;device.spics_io_num=-1;device.queue_size=1;
 if(spi_bus_add_device(SPI2_HOST,&device,&spi)!=ESP_OK)return -EIO;
 portENTER_CRITICAL(&mux);resources=true;portEXIT_CRITICAL(&mux);return 0;
}
int rearm(){
 hardware.stage=7;const uint8_t standby[]={0},packet[]={0,12,0,128,1,0},mask[]={2,0x72,2,0x72,0,0,0,0},clear[]={0xff,0xff},continuous[]={0xff,0xff,0xff};
 int rc=command(0x80,standby,sizeof standby);if(!rc)rc=command(0x8c,packet,sizeof packet);if(!rc)rc=command(0x08,mask,sizeof mask);if(!rc)rc=command(0x02,clear,sizeof clear);
 if(rc)return rc;
 header_pending=false;portENTER_CRITICAL(&mux);latched=false;portEXIT_CRITICAL(&mux);
 if(gpio_get_level(Irq))return -EIO;
 return command(0x82,continuous,sizeof continuous);
}
int start(){
 hardware.stage=1;int rc=resources_start();if(rc)return rc;
 portENTER_CRITICAL(&mux);const bool permit=cancel_serial==active_serial&&allowed;
 if(permit)rc=gpio_set_level(Reset,1)==ESP_OK?0:-EIO;else rc=-ECANCELED;portEXIT_CRITICAL(&mux);
 if(rc)return rc;
 vTaskDelay(pdMS_TO_TICKS(10));
 // Same passive schedule8 fixed PHY as T096. RX-only, V3 TCXO1.8V/RF switch.
 const uint8_t standby[]={0},tcxo[]={2,0,1,64},cal[]={0x7f},image[]={0xe1,0xe9},regulator[]={1},type[]={1},switching[]={1},bases[]={0,0},freq[]={0x39,0xa6,0x66,0x66},mod[]={11,6,4,0},fallback[]={0x20};
 #define CMD(op,args) do{rc=command(op,args,sizeof args);if(rc)return rc;}while(0)
 CMD(0x80,standby);CMD(0x97,tcxo);CMD(0x89,cal);CMD(0x98,image);CMD(0x96,regulator);CMD(0x8a,type);CMD(0x9d,switching);CMD(0x8f,bases);CMD(0x86,freq);CMD(0x8b,mod);CMD(0x93,fallback);
 #undef CMD
 uint8_t value=0;
 if(!(rc=reg_write(0x08e7,0x38))&&!(rc=reg_read(0x0736,value)))rc=reg_write(0x0736,value|4u);
 // Semtech boosted RX gain; reapply on every reset and verify before RX.
 // Recheck boosted gain after each configuration-retaining sleep as well.
 if(!rc)rc=reg_write(RxGainReg,RxGainBoosted);
 if(!rc)rc=reg_read(RxGainReg,value);
 if(!rc&&value!=RxGainBoosted)rc=-EIO;
 if(!rc)rc=reg_write(0x0740,0x14);
 if(!rc)rc=reg_write(0x0741,0x24);
 if(!rc)rc=reg_read(0x0740,value);
 if(!rc&&value!=0x14)rc=-ENODEV;
 if(!rc)rc=reg_read(0x0741,value);
 if(!rc&&value!=0x24)rc=-ENODEV;
 if(rc)return rc;
 rc=capture_start();if(rc)return rc;
 return rearm();
}
// Sleep retains the radio configuration, not a source permission. The capture
// timer, protocol model, replay floors and epoch remain live throughout.
bool irq_pending(){portENTER_CRITICAL(&mux);const bool pending=latched;portEXIT_CRITICAL(&mux);return pending||gpio_get_level(Irq)||header_pending;}
int sleep_radio(){
 if(inflight||irq_pending())return 0;
 hardware.stage=8;
 const uint8_t standby[]={0};int rc=command(0x80,standby,sizeof standby);if(rc)return rc;
 // A header racing standby invalidates this forecast. Do not sleep on it.
 if(irq_pending()){shadow.lose(9);inc(sleep_state.refused);return rearm();}
 if(cancelled())return -ECANCELED;
 hardware.opcode=0x84;tx[0]=0x84;tx[1]=4; // warm start; RTC wake disabled
 rc=transfer(2,true);if(rc)return rc;
 radio_sleeping=true;sleep_state.last_transition=now();inc(sleep_state.sleeps);
 // Semtech requires >=500us with no further SPI after SetSleep. This also
 // bounds how soon an OFF notification can reuse/reset this owner.
 vTaskDelay(pdMS_TO_TICKS(2)+1);
 return cancelled()?-ECANCELED:0;
}
int wake_radio(){
 hardware.stage=9;
 if(cancelled())return -ECANCELED;
 // NSS wakes the chip while BUSY is high. Waiting for BUSY before NSS deadlocks.
 if(gpio_set_level(Nss,0)!=ESP_OK)return -EIO;
 vTaskDelay(pdMS_TO_TICKS(1)+1);
 const int wake=busy();gpio_set_level(Nss,1);if(wake)return wake;
 radio_sleeping=false;
 uint8_t dummy=0;int rc=command(0xc0,&dummy,1);
 if(!rc&&(!status_ok(rx[1])||(rx[1]&0x70)!=0x20))rc=-EIO;
 uint8_t gain=0;
 if(!rc)rc=reg_write(RxGainReg,RxGainBoosted);
 if(!rc)rc=reg_read(RxGainReg,gain);
 if(!rc&&gain!=RxGainBoosted)rc=-EIO;
 if(!rc)rc=rearm();
 if(!rc){inc(sleep_state.wakes);sleep_state.last_transition=now();}
 return rc;
}
int schedule_radio(){
 const auto at=now();
 if(shadow.v.last_us&&at>=shadow.v.last_us&&at-shadow.v.last_us>250000)shadow.lose(9);
 shadow.tick(at,owned.enabled&&!failed);
 uint64_t tick=0;
 const bool mapped=local(tick)&&qualified_map.available&&tick>=qualified_map.available&&tick-qualified_map.available<=10000000;
 if(shadow.v.tracking&&!mapped)shadow.lose(10);
 if(radio_sleeping&&irq_pending())shadow.lose(9);
 if(radio_sleeping)return shadow.receive_due(at)?wake_radio():0;
 return !shadow.receive_due(at)&&!irq_pending()?sleep_radio():0;
}
int receive(){
 uint32_t raw,gen,serial;bool have;portENTER_CRITICAL(&mux);have=latched;raw=raw_capture;gen=raw_generation;serial=raw_serial;portEXIT_CRITICAL(&mux);
 if(!have){if(header_pending&&now()>=header_until){inc(hardware.invalid_capture);return rearm();}return 0;}
 hardware.stage=5;const uint8_t dummy[]={0,0,0};int rc=command(0x12,dummy,sizeof dummy);if(rc)return rc;
 if(!status_ok(rx[1]))return -EIO;
 const uint16_t flags=(uint16_t(rx[2])<<8)|rx[3];
 if(flags&0x260){inc(hardware.crc_bad);return rearm();}
 if(flags&0x10){
  // HeaderValid is the physical timing event; preserve it across FIFO completion.
  // A duplicate header without an intervening RX completion is ambiguous.
  if(header_pending){inc(hardware.invalid_capture);return rearm();}
  header_capture=raw;header_generation=gen;header_serial=serial;header_until=now()+UINT64_C(1000000);header_pending=true;
 }
 if(!(flags&2)){
  if(!(flags&0x10))return -EIO;
  const uint8_t clear_header[]={0,0x10};rc=command(0x02,clear_header,sizeof clear_header);
  if(rc)return rc;
  portENTER_CRITICAL(&mux);latched=gpio_get_level(Irq)!=0;portEXIT_CRITICAL(&mux);return 0;
 }
 inc(hardware.rx_done);rc=command(0x13,dummy,sizeof dummy);if(rc)return rc;
 if(!status_ok(rx[1]))return -EIO;
 const unsigned size=rx[2],offset=rx[3];
 if((size!=112&&size!=24&&size!=32)||size>256-offset){inc(hardware.malformed);return rearm();}
 std::memset(tx,0,size+3);tx[0]=0x1e;tx[1]=offset;rc=transfer(size+3);if(rc)return rc;if(!status_ok(rx[2]))return -EIO;
 // Freeze original bytes and captured edge before IRQ/FIFO reuse.
 uint8_t wire[128];std::memcpy(wire,rx+3,size);
 uint64_t tick;const uint64_t before=now();const bool readable=local(tick);const uint64_t after=now();
 // Keep every original mapped-path admission, predecessor, enclosure and
 // two-second expiry check. Its phase is now a guard only, never model input.
 const auto edge=readable&&header_pending?mapping.convert(header_capture,tick):ptob_gnss::CounterMap::Edge{};
 uint64_t native_now=0,native_edge=0;
 const bool native_valid=edge.valid&&native_local(native_now)&&native_clock.event(capture_epoch,header_capture,native_edge);
 gen=header_generation;serial=header_serial;
 if(!edge.valid||!native_valid||gen!=core_generation||serial!=active_serial||tick<edge.tick||tick-edge.tick>UINT64_C(8000000)*20||after<before){inc(hardware.invalid_capture);}
 else{
  const uint64_t age=(tick-edge.tick+19)/20,mid=before+(after-before)/2;
  if(age>mid||edge.tick>UINT64_MAX/4)inc(hardware.invalid_capture);
  else{hardware.capture_bound_ns=edge.bracket_ns+uint32_t(std::min<uint64_t>((after-before)*1000,UINT32_MAX-edge.bracket_ns));
   (void)n30_rc8_receive(wire,size,native_edge,capture_epoch,mid-age,after,gen);}
 }
 rc=command(0x14,dummy,sizeof dummy);if(rc)return rc;
 if(status_ok(rx[1])){hardware.rssi_dbm=-int16_t(rx[2])/2;hardware.snr_quarter_db=int8_t(rx[3]);}
 return rearm();
}
void publish(){
 n30_lora_view view;uint64_t current=0,sampled=0;
 // Commit/estimate on the very same group0 hardware counter as HeaderValid.
 // GPTimer/esp_timer supply lifecycle/availability guards only, never phase.
 const bool readable=native_local(current,&sampled);const uint64_t available=now();
 if(readable)n30_rc8_clock_sample(current,capture_epoch,sampled,available);
 else if(owned.enabled){n30_rc8_clock_sample(0,capture_epoch,available,available);fault(native_error);}
 n30_rc8_snapshot_sample(&view,readable?sampled:available,available);
 // Worker-owned protocol state and hardware counters are merged outside the
 // short cross-core publication lock. Unknown uncertainty remains unknown.
 view.requested=false;view.active=owned.enabled;view.fault=failed;view.mode=uint8_t(policy.mode());view.transition_remaining_ms=policy.remaining_ms(now());view.ble_locked=ble_lock_observed;
 view.rx_done=hardware.rx_done;view.crc_bad=hardware.crc_bad;view.malformed=hardware.malformed;view.spi_ok=hardware.spi_ok;view.spi_error=hardware.spi_error;view.invalid_capture=hardware.invalid_capture;view.capture_overflow=0;view.map_accepted=hardware.map_accepted;view.map_rejected=hardware.map_rejected;view.map_soft_refused=hardware.map_soft_refused;view.map_hard_reset=hardware.map_hard_reset;view.capture_bound_ns=hardware.capture_bound_ns;view.rssi_dbm=hardware.rssi_dbm;view.snr_quarter_db=hardware.snr_quarter_db;view.error=hardware.error;view.stage=hardware.stage;view.opcode=hardware.opcode;view.work_max_us=hardware.work_max_us;view.heartbeat_ms=uint32_t(now()/1000);view.stack_unused=uxTaskGetStackHighWaterMark(nullptr);
 view.map_bracket_ns=hardware.map_bracket_ns;
 view.current_rc=current_failure.rc;view.current_reason=current_failure.reason;view.current_raw=current_failure.raw;view.current_prev_raw=current_failure.previous_raw;
 view.current_us=current_failure.sampled;view.current_prev_us=current_failure.previous_us;view.current_available_us=current_failure.available;view.current_epoch=current_failure.epoch;
 view.hold_requested_id=hold_last_id;view.hold_result=hold_result;
 view.spi_retired=!inflight;view.capture_released=!capture_active&&!edge_enabled&&!map_enabled&&!current_enabled&&!timer_enabled&&!timer_started;view.reset_asserted=resources&&gpio_get_level(Reset)==0;
 shadow.tick(available,owned.enabled&&!failed);
 if(sleep_state.at&&available>=sleep_state.at&&sleep_state.active){
  const auto dt=available-sleep_state.at;
  // Observed owner state residency, not a measured supply-current saving.
  (sleep_state.sleeping?sleep_state.sleep_us:sleep_state.rx_us)+=dt;
 }
 sleep_state.at=available;sleep_state.active=owned.enabled&&!failed;sleep_state.sleeping=radio_sleeping&&sleep_state.active;
 portENTER_CRITICAL(&mux);shadow_published=shadow.v;sleep_published=sleep_state;view.requested=owner.requested;view.invalid_capture+=capture_invalid;view.capture_overflow=capture_drops;view.request_generation=owner.generation;view.off_ack_generation=owner.off_ack;view.off_acknowledged=owner.drained();published=view;published_serial=active_serial;worker_published_us=available;published_cleanup_fault=cleanup_failed||owner.stuck;owned.failed=failed;owned.initialized=resources;owned.packets=hardware.rx_done;owned.crc_errors=hardware.crc_bad;owned.malformed=hardware.malformed;owned.status=int16_t(hardware.error);owned.radio_errors=hardware.spi_error;owned.work_max_us=hardware.work_max_us;basic=owned;portEXIT_CRITICAL(&mux);
}
void retire_capture(){
 reset_radio();owned.enabled=false;radio_sleeping=false;shadow.tick(now(),false);
 if(capture_active||edge_enabled||map_enabled||current_enabled||timer_enabled||timer_started)capture_stop();
 if(core_generation){(void)n30_rc8_switch(false);portENTER_CRITICAL(&mux);core_generation=0;portEXIT_CRITICAL(&mux);}
}
void finish_worker(uint32_t generation,uint64_t begin){
 portENTER_CRITICAL(&mux);
 if(!owner.requested&&!stop_at&&!owner.drained())stop_at=begin;
 const auto at=now();const bool timely=!stop_at||(at>=stop_at&&at-stop_at<200000);
 if(!timely&&!failed){cleanup_failed=true;failed=true;hardware.error=-ETIMEDOUT;}
 owner.finish(generation,timely&&!owned.enabled&&!capture_active&&!edge_enabled&&!map_enabled&&!current_enabled&&!timer_started&&!timer_enabled&&(!resources||gpio_get_level(Reset)==0),!inflight);
 if(owner.drained())stop_at=0;
 portEXIT_CRITICAL(&mux);
}
bool configured_receiver(){const bool ok=PTOB_TWATCH_LORA_DYNAMIC_RX&&n30_rc8_init(PTOB_N30_FACTORY_ID,0,PTOB_N30_GROUP_ID,PTOB_N30_KEY_EPOCH,PTOB_N30_GROUP_KEY);if(ok)watch_lora_shadow_set_observer(watch_lora_shadow_event);return ok;}
void worker_step(bool configured){
  const auto begin=now();bool permit,locked,cycle,change,parked;uint8_t mode;uint32_t generation,serial;bool tripped;uint64_t hold_id,hold_at;
  portENTER_CRITICAL(&mux);permit=allowed;parked=ble_parked;serial=cancel_serial;tripped=emergency;locked=ble_locked&&begin>=ble_at&&begin-ble_at<250000;cycle=cycle_pending&&permit&&request_serial==serial;change=mode_pending&&permit&&request_serial==serial;mode=requested_mode;cycle_pending=mode_pending=false;hold_id=hold_pending_id;hold_at=hold_pending_us;hold_pending_id=0;portEXIT_CRITICAL(&mux);
  if(active_serial!=serial){portENTER_CRITICAL(&mux);generation=owner.generation;portEXIT_CRITICAL(&mux);retire_capture();finish_worker(generation,begin);}
  if(tripped)fault(-ETIMEDOUT);
  ble_lock_observed=locked;policy.update(locked,begin,parked);if(cycle)policy.toggle();if(change)policy.set(n30::LoRaMode(mode));
  portENTER_CRITICAL(&mux);
  if(recover_pending){
   const bool retired=owner.drained()&&!owned.enabled&&!inflight&&!capture_active&&
    !edge_enabled&&!map_enabled&&!current_enabled&&!timer_enabled&&!timer_started&&
    resources&&(!cap_timer||capture_ready)&&gpio_get_level(Reset)==0;
   // This clears an operating latch only. Original cleanup failures, missing
   // DMA completions, dead workers and exhausted generations remain terminal.
   if(configured&&allowed&&permit&&!emergency&&!cleanup_failed&&failed&&retired&&!mode_pending&&!cycle_pending&&
      owner.generation==recover_owner_generation&&cancel_serial==recover_cancel_serial&&
      begin>=recover_requested_at&&begin-recover_requested_at<250000&&
      policy.mode()!=n30::LoRaMode::KeepOff&&owner.generation!=UINT32_MAX&&cancel_serial!=UINT32_MAX){
    failed=false;hardware.fault=false;hardware.error=0;
   }
   recover_pending=false;
  }
  portEXIT_CRITICAL(&mux);
  bool want=configured&&permit&&!failed&&policy.requested();
  portENTER_CRITICAL(&mux);want=want&&allowed&&serial==cancel_serial&&!owner.stuck&&!inflight;
  if(want!=owner.requested&&!owner.request(want)){failed=true;hardware.error=-EOVERFLOW;want=false;}
  generation=(want||owned.enabled||capture_active||inflight)?owner.begin():owner.generation;active_serial=serial;portEXIT_CRITICAL(&mux);
  if(want&&!owned.enabled){
   const auto next=n30_rc8_switch(true);portENTER_CRITICAL(&mux);core_generation=next;portEXIT_CRITICAL(&mux);const int rc=next?start():-EOVERFLOW;
   if(rc&&rc!=-ECANCELED)fault(rc);
   owned.enabled=rc==0;
  }
  if(owned.enabled&&!want){reset_radio();owned.enabled=false;}
  if(owned.enabled){map_step();int rc=radio_sleeping?0:receive();if(!rc)rc=schedule_radio();if(rc){if(rc!=-ECANCELED)fault(rc);reset_radio();owned.enabled=false;}}
  if(!owned.enabled)retire_capture();
  finish_worker(generation,begin);
  if(hold_id){const uint64_t at=now();hold_last_id=hold_id;hold_result=at>=hold_at&&at-hold_at<250000&&owned.enabled&&!failed&&!cancelled()&&n30_rc8_hold60(hold_id,at)?1:2;}
  hardware.work_max_us=std::max(hardware.work_max_us,uint32_t(now()-begin));publish();
 }
void worker(void*){const bool configured=configured_receiver();for(;;){worker_step(configured);ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(radio_sleeping?50:2));}}
}
namespace v12 {
bool lora_start_worker(){return xTaskCreatePinnedToCore(worker,"n30_lora",8192,nullptr,1,&task,1)==pdPASS;}
bool lora_request(bool enable,uint32_t){
 if(enable)return false; // Receiver control uses explicit AUTO/ON/OFF only.
 portENTER_CRITICAL(&mux);const bool ok=revoke_locked(now());portEXIT_CRITICAL(&mux);if(task)xTaskNotifyGive(task);return ok;
}
LoRaStatus lora_status(){portENTER_CRITICAL(&mux);auto result=basic;result.failed|=emergency;result.requested=owner.requested;result.worker_busy=owner.busy;result.request_generation=owner.generation;result.off_ack_generation=owner.off_ack;result.off_acknowledged=owner.drained();portEXIT_CRITICAL(&mux);return result;}
}
void n30_lora_control(bool locked,uint64_t sampled,bool permit,bool parked){
 portENTER_CRITICAL(&mux);ble_locked=locked;ble_at=sampled;ble_parked=parked&&permit;
 if(allowed&&!permit)(void)revoke_locked(sampled);
 // Operating faults still accept explicit mode intent and guarded recovery.
 allowed=permit&&!emergency;
 if(!emergency&&published.active&&sampled>=worker_published_us&&sampled-worker_published_us>=30000000){emergency=true;(void)revoke_locked(sampled);}
 portEXIT_CRITICAL(&mux);
}
bool n30_lora_mode(n30::LoRaMode mode){
 if(!PTOB_TWATCH_LORA_DYNAMIC_RX||uint8_t(mode)>2)return false;
 // Mode changes express operator intent. A fault remains latched even on ON;
 // only a later guarded recovery may admit new receiver/capture generations.
 portENTER_CRITICAL(&mux);bool ok=allowed&&!emergency;if(ok&&mode==n30::LoRaMode::KeepOff)ok=revoke_locked(now(),false,!off_intent);if(ok){off_intent=mode==n30::LoRaMode::KeepOff;requested_mode=uint8_t(mode);mode_pending=true;cycle_pending=false;request_serial=cancel_serial;}portEXIT_CRITICAL(&mux);if(ok&&task)xTaskNotifyGive(task);return ok;
}
bool n30_lora_cycle(){
 if(!PTOB_TWATCH_LORA_DYNAMIC_RX)return false;
 portENTER_CRITICAL(&mux);const bool ok=allowed&&!basic.failed&&!emergency&&!cycle_pending&&!mode_pending;if(ok){off_intent=false;cycle_pending=true;request_serial=cancel_serial;}portEXIT_CRITICAL(&mux);if(ok&&task)xTaskNotifyGive(task);return ok;
}
bool n30_lora_configured(){return PTOB_TWATCH_LORA_DYNAMIC_RX!=0;}
bool n30_lora_fault_recover(){
 const auto sampled=now();portENTER_CRITICAL(&mux);
 const bool fresh=sampled>=worker_published_us&&sampled-worker_published_us<250000;
 const bool ok=PTOB_TWATCH_LORA_DYNAMIC_RX&&allowed&&!emergency&&basic.failed&&!recover_pending&&
  !published_cleanup_fault&&!owner.stuck&&owner.generation!=UINT32_MAX&&cancel_serial!=UINT32_MAX&&
  !cycle_pending&&!mode_pending&&published.mode!=uint8_t(n30::LoRaMode::KeepOff)&&fresh&&
  owner.drained()&&published.off_acknowledged&&published.request_generation==owner.generation&&
  !published.active&&published.spi_retired&&published.capture_released&&published.reset_asserted;
 if(ok){recover_pending=true;recover_requested_at=sampled;recover_owner_generation=owner.generation;recover_cancel_serial=cancel_serial;}
 portEXIT_CRITICAL(&mux);if(ok&&task)xTaskNotifyGive(task);return ok;
}
unsigned n30_lora_off_proof(){
 portENTER_CRITICAL(&mux);
 const bool quiet=!emergency&&!published_cleanup_fault&&!owner.stuck&&owner.generation!=UINT32_MAX&&cancel_serial!=UINT32_MAX&&
  owner.drained()&&published.off_acknowledged&&published.request_generation==owner.generation&&published.off_ack_generation==owner.off_ack&&!published.active;
 const unsigned proof=unsigned(quiet)|(unsigned(published.spi_retired)<<1)|(unsigned(published.capture_released)<<2)|(unsigned(published.reset_asserted)<<3);
 portEXIT_CRITICAL(&mux);return proof;
}
bool n30_lora_cleanup_fault(){
 portENTER_CRITICAL(&mux);
 const bool result=published_cleanup_fault||owner.stuck||owner.generation==UINT32_MAX||cancel_serial==UINT32_MAX;
 portEXIT_CRITICAL(&mux);return result;
}
bool n30_lora_hold60(uint64_t id){
 const uint64_t at=now();portENTER_CRITICAL(&mux);
 // Mailbox admission is not a clock grant. Only the worker/core below may
 // bind a current model and its original deadlines; report its result apart.
 const bool ok=id&&allowed&&!emergency&&!basic.failed&&!hold_pending_id&&!published.hold_active;
 if(ok){hold_pending_id=id;hold_pending_us=at;}
 portEXIT_CRITICAL(&mux);if(ok&&task)xTaskNotifyGive(task);return ok;
}
void n30_lora_snapshot(n30_lora_view& out){portENTER_CRITICAL(&mux);out=published;out.requested=owner.requested&&allowed;out.request_generation=owner.generation;out.off_ack_generation=owner.off_ack;out.off_acknowledged=owner.drained();if(!out.requested||published_serial!=cancel_serial){out.utc_us=out.until_us=out.watch_utc_us=0;out.watch_valid=false;out.state=0;}if(emergency){out.fault=true;out.active=false;out.until_us=0;out.error=-ETIMEDOUT;out.spi_retired=out.capture_released=out.off_acknowledged=false;}portEXIT_CRITICAL(&mux);}

extern "C" void watch_lora_shadow_event(uint8_t kind,const uint8_t context[16],uint64_t epoch,uint32_t cursor,uint64_t at,uint64_t pair_at,bool valid){
 shadow.observe(kind,context,epoch,cursor,at,pair_at,valid);
}
watch::LoRaShadow::View watch_lora_shadow_snapshot(){
 portENTER_CRITICAL(&mux);const auto s=shadow_published;portEXIT_CRITICAL(&mux);return s;
}

watch::LoRaSleepView watch_lora_sleep_snapshot(){
 portENTER_CRITICAL(&mux);const auto s=sleep_published;portEXIT_CRITICAL(&mux);return s;
}

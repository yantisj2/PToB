// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// SX1262 RX-only owner for HeltecV3 N30. No SetTx opcode, BLE map update or
// listener transmission. Actual MCPWM DIO1 capture, never an ISR arrival time.
#include "native_lora.hpp"
#include "n30_lora.hpp"
#include "lora_core.h"
#include "lora_owner_gate.hpp"
#include "ptob_gnss_reference.hpp"
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
namespace {
constexpr auto Nss=GPIO_NUM_8,Reset=GPIO_NUM_12,Busy=GPIO_NUM_13,Irq=GPIO_NUM_14;
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
v12::LoRaOwnerGate owner;v12::LoRaStatus basic,owned;n30_lora_view published,hardware;
n30::LoRaPolicy policy;ptob_gnss::CounterMap mapping;
spi_device_handle_t spi=nullptr;spi_transaction_t transaction{};
DMA_ATTR uint8_t tx[132]{},rx[132]{};
mcpwm_cap_timer_handle_t cap_timer=nullptr;mcpwm_cap_channel_handle_t edge_channel=nullptr,map_channel=nullptr;
TaskHandle_t task=nullptr;
uint64_t ble_at=0,request_at=0,last_map=0,map_pending=0,map_before=0,map_after=0,stop_at=0;
uint32_t cancel_serial=0,active_serial=0,core_generation=0,map_serial=0,map_expected=0,map_capture=0;
uint32_t raw_capture=0,raw_generation=0,raw_serial=0;
uint64_t capture_epoch=0;
uint8_t requested_mode=0;bool mode_pending=false,cycle_pending=false,ble_locked=false,allowed=false;
bool ble_lock_observed=false; // Worker-owned actual fresh policy input.
bool resources=false,capture_active=false,inflight=false,latched=false,failed=false;
bool timer_enabled=false,timer_started=false,map_enabled=false,edge_enabled=false,emergency=false;
uint32_t capture_drops=0,capture_invalid=0;
uint64_t now(){return uint64_t(esp_timer_get_time());}
void inc(uint32_t& v){if(v<UINT32_MAX)++v;}
// LoRa owns an independent clock. Keep its short mapping brackets on the
// original XTAL GPTimer, outside the BLE fine receiver's lock and fence work.
bool local(uint64_t& tick){return n30_lora_reference_read(&tick)&&tick;}
bool cancelled(){portENTER_CRITICAL(&mux);const bool result=cancel_serial!=active_serial||!allowed;portEXIT_CRITICAL(&mux);return result;}
void reset_radio(){if(resources)gpio_set_level(Reset,0);}
void fault(int error){reset_radio();if(!failed)hardware.error=error;failed=true;hardware.fault=true;}
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
int transfer(size_t size){
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
 result=busy();return result;
}
int command(uint8_t op,const uint8_t* args,size_t size){
 hardware.opcode=op;tx[0]=op;if(size)std::memcpy(tx+1,args,size);return transfer(size+1);
}
int reg_write(uint16_t address,uint8_t value){const uint8_t args[]={uint8_t(address>>8),uint8_t(address),value};return command(0x0d,args,sizeof args);}
int reg_read(uint16_t address,uint8_t& value){const uint8_t args[]={uint8_t(address>>8),uint8_t(address),0,0};const int rc=command(0x1d,args,sizeof args);if(rc)return rc;if(!status_ok(rx[3]))return -EIO;value=rx[4];return 0;}
bool on_edge(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t* e,void*){
 BaseType_t wake=pdFALSE;portENTER_CRITICAL_ISR(&mux);
 if(!capture_active){inc(capture_invalid);}
 else if(latched){inc(capture_drops);}
 else{raw_capture=e->cap_value;raw_generation=core_generation;raw_serial=active_serial;latched=true;}
 portEXIT_CRITICAL_ISR(&mux);if(task)vTaskNotifyGiveFromISR(task,&wake);return wake==pdTRUE;
}
bool on_map(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t* e,void*){
 portENTER_CRITICAL_ISR(&mux);map_capture=e->cap_value;map_serial++;portEXIT_CRITICAL_ISR(&mux);return false;
}
int capture_start(){
 if(!cap_timer){
  mcpwm_capture_timer_config_t cfg{};cfg.group_id=0;cfg.clk_src=MCPWM_CAPTURE_CLK_SRC_APB;
  uint32_t hz=0;
  if(mcpwm_new_capture_timer(&cfg,&cap_timer)!=ESP_OK||mcpwm_capture_timer_get_resolution(cap_timer,&hz)!=ESP_OK||hz!=80000000)return -EIO;
  mcpwm_capture_channel_config_t ch{};ch.gpio_num=Irq;ch.prescale=1;ch.flags.pos_edge=1;
  const mcpwm_capture_event_callbacks_t cb={.on_cap=on_edge};
  if(mcpwm_new_capture_channel(cap_timer,&ch,&edge_channel)!=ESP_OK||mcpwm_capture_channel_register_event_callbacks(edge_channel,&cb,nullptr)!=ESP_OK)return -EIO;
  ch={};ch.gpio_num=-1;ch.prescale=1;const mcpwm_capture_event_callbacks_t mb={.on_cap=on_map};
  if(mcpwm_new_capture_channel(cap_timer,&ch,&map_channel)!=ESP_OK||mcpwm_capture_channel_register_event_callbacks(map_channel,&mb,nullptr)!=ESP_OK)return -EIO;
 }
 if(capture_epoch==UINT64_MAX)return -EOVERFLOW;
 mapping.reset();last_map=map_pending=0;++capture_epoch;
 if(mcpwm_capture_timer_enable(cap_timer)!=ESP_OK)return -EIO;
 timer_enabled=true;
 if(mcpwm_capture_timer_start(cap_timer)!=ESP_OK)return -EIO;
 timer_started=true;
 if(mcpwm_capture_channel_enable(map_channel)!=ESP_OK)return -EIO;
 map_enabled=true;
 if(mcpwm_capture_channel_enable(edge_channel)!=ESP_OK)return -EIO;
 edge_enabled=true;
 portENTER_CRITICAL(&mux);latched=false;capture_active=true;portEXIT_CRITICAL(&mux);return 0;
}
void capture_stop(){
 portENTER_CRITICAL(&mux);capture_active=false;portEXIT_CRITICAL(&mux);
 if(edge_enabled){if(mcpwm_capture_channel_disable(edge_channel)!=ESP_OK)fault(-EIO);else edge_enabled=false;}
 if(map_enabled){if(mcpwm_capture_channel_disable(map_channel)!=ESP_OK)fault(-EIO);else map_enabled=false;}
 if(timer_started){if(mcpwm_capture_timer_stop(cap_timer)!=ESP_OK)fault(-EIO);else timer_started=false;}
 if(timer_enabled){if(mcpwm_capture_timer_disable(cap_timer)!=ESP_OK)fault(-EIO);else timer_enabled=false;}
 mapping.reset();map_pending=0;portENTER_CRITICAL(&mux);latched=false;portEXIT_CRITICAL(&mux);
}
void map_step(){
 uint64_t tick;if(!local(tick))return;
 uint32_t serial,cap;portENTER_CRITICAL(&mux);serial=map_serial;cap=map_capture;portEXIT_CRITICAL(&mux);
 if(map_pending&&serial!=map_expected){
  if(serial!=map_expected+1||!mapping.observe({map_before,map_after,tick,cap})||!mapping.release(tick)){mapping.reset();inc(hardware.map_rejected);}
  map_pending=0;
 }else if(map_pending&&tick>=map_pending&&tick-map_pending>2000000){mapping.reset();map_pending=0;inc(hardware.map_rejected);}
 if(!map_pending&&(!last_map||tick-last_map>=5000000)){
  esp_err_t rc;portENTER_CRITICAL(&mux);map_expected=map_serial;
  if(!local(map_before)){portEXIT_CRITICAL(&mux);return;}
  rc=mcpwm_capture_channel_trigger_soft_catch(map_channel);const bool valid=local(map_after);portEXIT_CRITICAL(&mux);
  if(rc!=ESP_OK||!valid){mapping.reset();inc(hardware.map_rejected);return;}
  last_map=map_pending=map_before;
 }
 hardware.map_accepted=mapping.accepted;
}
int resources_start(){
 if(resources)return 0;
 gpio_config_t outputs{};outputs.pin_bit_mask=(1ULL<<Nss)|(1ULL<<Reset);outputs.mode=GPIO_MODE_INPUT_OUTPUT;
 if(gpio_set_level(Nss,1)!=ESP_OK||gpio_set_level(Reset,0)!=ESP_OK||gpio_config(&outputs)!=ESP_OK)return -EIO;
 gpio_config_t inputs{};inputs.pin_bit_mask=(1ULL<<Busy)|(1ULL<<Irq);inputs.mode=GPIO_MODE_INPUT;
 if(gpio_config(&inputs)!=ESP_OK)return -EIO;
 spi_bus_config_t bus{};bus.mosi_io_num=10;bus.miso_io_num=11;bus.sclk_io_num=9;bus.quadwp_io_num=bus.quadhd_io_num=-1;bus.max_transfer_sz=sizeof tx;
 if(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO)!=ESP_OK)return -EIO;
 spi_device_interface_config_t device{};device.clock_speed_hz=2000000;device.spics_io_num=-1;device.queue_size=1;
 if(spi_bus_add_device(SPI2_HOST,&device,&spi)!=ESP_OK)return -EIO;
 portENTER_CRITICAL(&mux);resources=true;portEXIT_CRITICAL(&mux);return 0;
}
int rearm(){
 hardware.stage=7;const uint8_t standby[]={0},packet[]={0,12,0,128,1,0},mask[]={2,0x62,2,0x62,0,0,0,0},clear[]={0xff,0xff},single[]={0,0,0};
 int rc=command(0x80,standby,sizeof standby);if(!rc)rc=command(0x8c,packet,sizeof packet);if(!rc)rc=command(0x08,mask,sizeof mask);if(!rc)rc=command(0x02,clear,sizeof clear);
 if(rc)return rc;
 portENTER_CRITICAL(&mux);latched=false;portEXIT_CRITICAL(&mux);
 if(gpio_get_level(Irq))return -EIO;
 return command(0x82,single,sizeof single);
}
int start(){
 hardware.stage=1;int rc=resources_start();if(rc)return rc;
 portENTER_CRITICAL(&mux);const bool permit=cancel_serial==active_serial&&allowed;
 if(permit)rc=gpio_set_level(Reset,1)==ESP_OK?0:-EIO;else rc=-ECANCELED;portEXIT_CRITICAL(&mux);
 if(rc)return rc;
 vTaskDelay(pdMS_TO_TICKS(10));
 // Same wire3 modem as the T096 parent. V3 DIO3 TCXO1.8V, DIO2 RF switch.
 const uint8_t standby[]={0},tcxo[]={2,0,1,64},cal[]={0x7f},image[]={0xe1,0xe9},regulator[]={1},type[]={1},switching[]={1},bases[]={0,0},freq[]={0x39,0x30,0,0},mod[]={10,6,2,0},fallback[]={0x20};
 #define CMD(op,args) do{rc=command(op,args,sizeof args);if(rc)return rc;}while(0)
 CMD(0x80,standby);CMD(0x97,tcxo);CMD(0x89,cal);CMD(0x98,image);CMD(0x96,regulator);CMD(0x8a,type);CMD(0x9d,switching);CMD(0x8f,bases);CMD(0x86,freq);CMD(0x8b,mod);CMD(0x93,fallback);
 #undef CMD
 uint8_t value=0;
 if(!(rc=reg_write(0x08e7,0x38))&&!(rc=reg_read(0x0736,value)))rc=reg_write(0x0736,value|4u);
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
int receive(){
 uint32_t raw,gen,serial;bool have;portENTER_CRITICAL(&mux);have=latched;raw=raw_capture;gen=raw_generation;serial=raw_serial;portEXIT_CRITICAL(&mux);
 if(!have)return 0;
 hardware.stage=5;const uint8_t dummy[]={0,0,0};int rc=command(0x12,dummy,sizeof dummy);if(rc)return rc;
 if(!status_ok(rx[1]))return -EIO;
 const uint16_t flags=(uint16_t(rx[2])<<8)|rx[3];
 if(flags&0x260){inc(hardware.crc_bad);return rearm();}
 if(!(flags&2))return -EIO;
 inc(hardware.rx_done);rc=command(0x13,dummy,sizeof dummy);if(rc)return rc;
 if(!status_ok(rx[1]))return -EIO;
 const unsigned size=rx[2],offset=rx[3];
 if((size!=80&&size!=128)||size>256-offset){inc(hardware.malformed);return rearm();}
 std::memset(tx,0,size+3);tx[0]=0x1e;tx[1]=offset;rc=transfer(size+3);if(rc)return rc;if(!status_ok(rx[2]))return -EIO;
 // Freeze original bytes and captured edge before IRQ/FIFO reuse.
 uint8_t wire[128];std::memcpy(wire,rx+3,size);
 uint64_t tick;const uint64_t before=now();const bool readable=local(tick);const uint64_t after=now();
 const auto edge=readable?mapping.convert(raw,tick):ptob_gnss::CounterMap::Edge{};
 if(!edge.valid||gen!=core_generation||serial!=active_serial||tick<edge.tick||tick-edge.tick>UINT64_C(8000000)*20||after<before){inc(hardware.invalid_capture);}
 else{
  const uint64_t age=(tick-edge.tick+19)/20,mid=before+(after-before)/2;
  if(age>mid||edge.tick>UINT64_MAX/4)inc(hardware.invalid_capture);
  else{hardware.capture_bound_ns=edge.bracket_ns+uint32_t(std::min<uint64_t>((after-before)*1000,UINT32_MAX-edge.bracket_ns));
   (void)n30_rc8_receive(wire,size,edge.tick*4/5,capture_epoch,mid-age,after,gen);}
 }
 rc=command(0x14,dummy,sizeof dummy);if(rc)return rc;
 if(status_ok(rx[1])){hardware.rssi_dbm=-int16_t(rx[2])/2;hardware.snr_quarter_db=int8_t(rx[3]);}
 return rearm();
}
void publish(){
 n30_lora_view view;n30_rc8_snapshot(&view,now());
 // Worker-owned protocol state and hardware counters are merged outside the
 // short cross-core publication lock. Unknown uncertainty remains unknown.
 view.requested=false;view.active=owned.enabled;view.fault=failed;view.mode=uint8_t(policy.mode());view.transition_remaining_ms=policy.remaining_ms(now());view.ble_locked=ble_lock_observed;
 view.rx_done=hardware.rx_done;view.crc_bad=hardware.crc_bad;view.malformed=hardware.malformed;view.spi_ok=hardware.spi_ok;view.spi_error=hardware.spi_error;view.invalid_capture=hardware.invalid_capture;view.capture_overflow=0;view.map_accepted=hardware.map_accepted;view.map_rejected=hardware.map_rejected;view.capture_bound_ns=hardware.capture_bound_ns;view.rssi_dbm=hardware.rssi_dbm;view.snr_quarter_db=hardware.snr_quarter_db;view.error=hardware.error;view.stage=hardware.stage;view.opcode=hardware.opcode;view.work_max_us=hardware.work_max_us;view.heartbeat_ms=uint32_t(now()/1000);view.stack_unused=uxTaskGetStackHighWaterMark(nullptr);
 view.spi_retired=!inflight;view.capture_released=!capture_active&&!edge_enabled&&!map_enabled&&!timer_enabled&&!timer_started;view.reset_asserted=resources&&gpio_get_level(Reset)==0;
 portENTER_CRITICAL(&mux);view.requested=owner.requested;view.invalid_capture+=capture_invalid;view.capture_overflow=capture_drops;view.request_generation=owner.generation;view.off_ack_generation=owner.off_ack;view.off_acknowledged=owner.drained();published=view;owned.failed=failed;owned.initialized=resources;owned.packets=hardware.rx_done;owned.crc_errors=hardware.crc_bad;owned.malformed=hardware.malformed;owned.status=int16_t(hardware.error);owned.radio_errors=hardware.spi_error;owned.work_max_us=hardware.work_max_us;basic=owned;portEXIT_CRITICAL(&mux);
}
bool configured_receiver(){return PTOB_N30_LORA_TX_FACTORY&&n30_rc8_init(PTOB_N30_FACTORY_ID,PTOB_N30_LORA_TX_FACTORY,PTOB_N30_GROUP_ID,PTOB_N30_KEY_EPOCH,PTOB_N30_GROUP_KEY);}
void worker_step(bool configured){
  const auto begin=now();bool permit,locked,cycle,change;uint8_t mode;uint32_t generation,serial;bool tripped;
  portENTER_CRITICAL(&mux);permit=allowed;serial=cancel_serial;tripped=emergency;locked=ble_locked&&begin>=ble_at&&begin-ble_at<250000;cycle=cycle_pending&&begin>=request_at&&begin-request_at<250000;change=mode_pending&&begin>=request_at&&begin-request_at<250000;mode=requested_mode;cycle_pending=mode_pending=false;portEXIT_CRITICAL(&mux);
  if(tripped)fault(-ETIMEDOUT);
  ble_lock_observed=locked;policy.update(locked,begin);if(cycle)policy.toggle();if(change)policy.set(n30::LoRaMode(mode));
  bool want=configured&&permit&&!failed&&policy.requested();
  portENTER_CRITICAL(&mux);want=want&&allowed&&serial==cancel_serial;
  if(want!=owner.requested&&!owner.request(want)){failed=true;hardware.error=-EOVERFLOW;want=false;}
  generation=(want||owned.enabled||capture_active||inflight)?owner.begin():owner.generation;active_serial=serial;portEXIT_CRITICAL(&mux);
  if(want&&!owned.enabled){
   const auto next=n30_rc8_switch(true);portENTER_CRITICAL(&mux);core_generation=next;portEXIT_CRITICAL(&mux);const int rc=start();
   if(rc&&rc!=-ECANCELED)fault(rc);
   owned.enabled=rc==0;
  }
  if(owned.enabled&&!want){reset_radio();owned.enabled=false;}
  if(owned.enabled){map_step();const int rc=receive();if(rc){if(rc!=-ECANCELED)fault(rc);reset_radio();owned.enabled=false;}}
  if(!owned.enabled){
   if(capture_active||edge_enabled||map_enabled||timer_enabled||timer_started)capture_stop();
   if(core_generation){(void)n30_rc8_switch(false);portENTER_CRITICAL(&mux);core_generation=0;portEXIT_CRITICAL(&mux);}
   reset_radio();
  }
  portENTER_CRITICAL(&mux);if(!owner.requested&&!stop_at)stop_at=begin;
  owner.finish(generation,!owned.enabled&&!capture_active&&!edge_enabled&&!map_enabled&&!timer_started&&!timer_enabled&&(!resources||gpio_get_level(Reset)==0),!inflight);
  if(owner.drained())stop_at=0;
  if(stop_at&&now()>=stop_at&&now()-stop_at>=200000){failed=true;hardware.error=-ETIMEDOUT;}
  portEXIT_CRITICAL(&mux);
  hardware.work_max_us=std::max(hardware.work_max_us,uint32_t(now()-begin));publish();
 }
void worker(void*){const bool configured=configured_receiver();for(;;){worker_step(configured);ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(2));}}
}
namespace v12 {
bool lora_start_worker(){return xTaskCreatePinnedToCore(worker,"n30_lora",8192,nullptr,1,&task,1)==pdPASS;}
bool lora_request(bool enable,uint32_t){
 if(enable)return false; // Receiver control uses explicit AUTO/ON/OFF only.
 portENTER_CRITICAL(&mux);allowed=false;if(cancel_serial!=UINT32_MAX)cancel_serial++;else emergency=true;const bool ok=owner.request(false);stop_at=now();reset_radio();portEXIT_CRITICAL(&mux);if(task)xTaskNotifyGive(task);return ok;
}
LoRaStatus lora_status(){portENTER_CRITICAL(&mux);auto result=basic;result.failed|=emergency;result.requested=owner.requested;result.worker_busy=owner.busy;result.request_generation=owner.generation;result.off_ack_generation=owner.off_ack;result.off_acknowledged=owner.drained();portEXIT_CRITICAL(&mux);return result;}
}
void n30_lora_control(bool locked,uint64_t sampled,bool permit){
 portENTER_CRITICAL(&mux);ble_locked=locked;ble_at=sampled;
 if(allowed&&!permit){if(cancel_serial!=UINT32_MAX)cancel_serial++;else emergency=true;(void)owner.request(false);stop_at=sampled;reset_radio();}
 allowed=permit;
 if(published.active&&sampled/1000>=published.heartbeat_ms&&sampled/1000-published.heartbeat_ms>=30000){reset_radio();emergency=true;allowed=false;if(cancel_serial!=UINT32_MAX)cancel_serial++;(void)owner.request(false);stop_at=sampled;}
 portEXIT_CRITICAL(&mux);
}
bool n30_lora_mode(n30::LoRaMode mode){
 if(!PTOB_N30_LORA_TX_FACTORY||uint8_t(mode)>2)return false;
 portENTER_CRITICAL(&mux);const bool ok=allowed&&!basic.failed;if(ok){requested_mode=uint8_t(mode);mode_pending=true;cycle_pending=false;request_at=now();}portEXIT_CRITICAL(&mux);if(ok&&task)xTaskNotifyGive(task);return ok;
}
bool n30_lora_cycle(){
 if(!PTOB_N30_LORA_TX_FACTORY)return false;
 portENTER_CRITICAL(&mux);const bool ok=allowed&&!basic.failed&&!cycle_pending&&!mode_pending;if(ok){cycle_pending=true;request_at=now();}portEXIT_CRITICAL(&mux);if(ok&&task)xTaskNotifyGive(task);return ok;
}
void n30_lora_snapshot(n30_lora_view& out){portENTER_CRITICAL(&mux);out=published;if(emergency){out.fault=true;out.active=false;out.until_us=0;out.error=-ETIMEDOUT;out.spi_retired=out.capture_released=out.off_acknowledged=false;}portEXIT_CRITICAL(&mux);}

// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// LR2021 RX-only owner for W12. No SetTx opcode, BLE map update or
// listener transmission. Actual MCPWM DIO1 capture, never an ISR arrival time.
#include "native_lora.hpp"
#include "w12_lora.hpp"
#include "lora_core.h"
#include "lora_owner_gate.hpp"
#include "w12_fine_clock.hpp"
#include "w12_lora_board.hpp"
#include <modules/LR2021/LR2021.h>
#include "w12_lora_config.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <initializer_list>
#include <cstring>
#include <cerrno>
#include <cinttypes>
namespace {
constexpr auto Nss=GPIO_NUM_8,Reset=GPIO_NUM_12,Busy=GPIO_NUM_13,Irq=gpio_num_t(w12::lora_board::IrqGpio);
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
v12::LoRaOwnerGate owner;v12::LoRaStatus basic,owned;w12_lora_view published,hardware;
w12::LoRaPolicy policy;
spi_device_handle_t spi=nullptr;spi_transaction_t transaction{};
DMA_ATTR uint8_t tx[512]{},rx[512]{};
TaskHandle_t task=nullptr;
uint64_t ble_at=0,request_at=0,stop_at=0;
uint64_t rc9_client_until=0,rc9_source_until=0;
uint64_t rc9_hold_observed_until=0;
uint32_t cancel_serial=0,active_serial=0,core_generation=0;
uint64_t raw_capture=0;uint32_t raw_generation=0,raw_serial=0;
uint64_t capture_epoch=0;
bool ble_lock_observed=false;
uint8_t requested_mode=0;bool mode_pending=false,cycle_pending=false,ble_locked=false,allowed=false;
bool resources=false,capture_active=false,inflight=false,latched=false,failed=false;
bool edge_enabled=false,emergency=false;
uint32_t capture_drops=0,capture_invalid=0;
uint64_t now(){return uint64_t(esp_timer_get_time());}
void inc(uint32_t& v){if(v<UINT32_MAX)++v;}
bool local(uint64_t& tick){return w12_fine_lora_current(&tick)&&tick;}
bool cancelled(){portENTER_CRITICAL(&mux);const bool result=cancel_serial!=active_serial||!allowed;portEXIT_CRITICAL(&mux);return result;}
void reset_radio(){if(resources){gpio_set_level(Reset,0);gpio_set_level(GPIO_NUM_3,0);gpio_set_level(GPIO_NUM_4,0);}}
void fault(int error){reset_radio();if(!failed)hardware.error=error;failed=true;hardware.fault=true;}
// Dedicated DMA buffers outlive every queued transaction, including timeout.
class Hal final:public RadioLibHal {
 public:
 Hal():RadioLibHal(GPIO_MODE_INPUT,GPIO_MODE_OUTPUT,0,1,GPIO_INTR_POSEDGE,GPIO_INTR_NEGEDGE){}
 bool ready=true;
 void init()override{
  if(resources)return;
  gpio_config_t outputs{};outputs.pin_bit_mask=(1ULL<<Nss)|(1ULL<<Reset)|(1ULL<<3)|(1ULL<<4);outputs.mode=GPIO_MODE_OUTPUT;
  if(gpio_set_level(Nss,1)!=ESP_OK||gpio_set_level(Reset,0)!=ESP_OK||gpio_config(&outputs)!=ESP_OK){ready=false;return;}
  gpio_config_t inputs{};inputs.pin_bit_mask=(1ULL<<Busy)|(1ULL<<Irq);inputs.mode=GPIO_MODE_INPUT;
  if(gpio_config(&inputs)!=ESP_OK){ready=false;return;}
  spi_bus_config_t bus{};bus.mosi_io_num=10;bus.miso_io_num=11;bus.sclk_io_num=9;bus.quadwp_io_num=bus.quadhd_io_num=-1;bus.max_transfer_sz=sizeof tx;
  if(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO)!=ESP_OK){ready=false;return;}
  spi_device_interface_config_t device{};device.clock_speed_hz=2000000;device.spics_io_num=-1;device.queue_size=1;
  if(spi_bus_add_device(SPI2_HOST,&device,&spi)!=ESP_OK){ready=false;return;}
  portENTER_CRITICAL(&mux);resources=true;portEXIT_CRITICAL(&mux);
 }
 void term()override{} // Retain owned descriptors; an outstanding DMA never gets freed.
 void pinMode(uint32_t p,uint32_t mode)override{
  if(p==RADIOLIB_NC)return;
  if(p!=Nss&&p!=Reset&&p!=Busy&&p!=Irq){ready=false;return;}
  if(gpio_set_direction(gpio_num_t(p),gpio_mode_t(mode))!=ESP_OK)ready=false;
 }
 void digitalWrite(uint32_t p,uint32_t level)override{
  if(p==RADIOLIB_NC)return;
  if(p==Reset&&level&&cancelled())return;
  if((p!=Nss&&p!=Reset)||gpio_set_level(gpio_num_t(p),level)!=ESP_OK)ready=false;
 }
 uint32_t digitalRead(uint32_t p)override{
  if(p==RADIOLIB_NC||(p==Busy&&cancelled()))return 0;
  if(p!=Busy&&p!=Irq&&p!=Reset&&p!=Nss){ready=false;return 0;}
  return gpio_get_level(gpio_num_t(p));
 }
 void attachInterrupt(uint32_t,void(*)(void),uint32_t)override{}
 void detachInterrupt(uint32_t)override{}
 void delay(RadioLibTime_t ms)override{vTaskDelay(pdMS_TO_TICKS(ms));}
 void delayMicroseconds(RadioLibTime_t us)override{const auto deadline=now()+us;while(now()<deadline){};}
 RadioLibTime_t millis()override{return RadioLibTime_t(now()/1000);}
 RadioLibTime_t micros()override{return RadioLibTime_t(now());}
 long pulseIn(uint32_t,uint32_t,RadioLibTime_t)override{return 0;}
 void spiBegin()override{}void spiEnd()override{}void spiBeginTransaction()override{}void spiEndTransaction()override{}
 void spiTransfer(uint8_t* input,size_t size,uint8_t* output)override{
  if(cancelled()){if(output)std::memset(output,0,size);return;}
  if(!ready||!resources||inflight||!size||size>sizeof tx){ready=false;if(output&&size<=sizeof rx)std::memset(output,0,size);return;}
  // LR2021 uses a16bit command. Physically refuse SetTx and TX test commands.
  if(size>=2&&input&&input[0]==2&&(input[1]==0x0d||input[1]==0x0e||input[1]==0x11)){ready=false;return;}
  if(input)std::memcpy(tx,input,size);else std::memset(tx,0,size);
  std::memset(rx,0,size);transaction={};transaction.length=size*8;transaction.tx_buffer=tx;transaction.rx_buffer=rx;
  if(spi_device_queue_trans(spi,&transaction,pdMS_TO_TICKS(5))!=ESP_OK){ready=false;inc(hardware.spi_error);return;}
  inflight=true;spi_transaction_t* completed=nullptr;
  if(spi_device_get_trans_result(spi,&completed,pdMS_TO_TICKS(5))!=ESP_OK||completed!=&transaction){ready=false;inc(hardware.spi_error);return;}
  inflight=false;inc(hardware.spi_ok);if(output)std::memcpy(output,rx,size);
 }
 void yield()override{vTaskDelay(pdMS_TO_TICKS(1));}
};
Hal hal;Module module(&hal,Nss,Irq,Reset,Busy);LR2021 radio(&module);
bool on_edge(uint64_t tick){
 BaseType_t wake=pdFALSE;portENTER_CRITICAL_ISR(&mux);
 if(!capture_active||!tick)inc(capture_invalid);
 else if(latched)inc(capture_drops);
 else{raw_capture=tick;raw_generation=core_generation;raw_serial=active_serial;latched=true;}
 portEXIT_CRITICAL_ISR(&mux);if(task)vTaskNotifyGiveFromISR(task,&wake);return wake==pdTRUE;
}
int capture_start(){
 if(capture_epoch==UINT64_MAX)return -EOVERFLOW;
 edge_enabled=true; // Claim before enable, including partial failure cleanup.
 ++capture_epoch;portENTER_CRITICAL(&mux);latched=false;capture_active=true;portEXIT_CRITICAL(&mux);
 if(!w12_fine_lora_start(on_edge)){capture_active=false;return -EIO;}
 return 0;
}
void capture_stop(){
 portENTER_CRITICAL(&mux);capture_active=false;portEXIT_CRITICAL(&mux);
 if(w12_fine_lora_stop())edge_enabled=false;else fault(-EIO);
 portENTER_CRITICAL(&mux);latched=false;portEXIT_CRITICAL(&mux);
}
int checked(int16_t rc){if(cancelled())return -ECANCELED;return rc==RADIOLIB_ERR_NONE&&hal.ready?0:(hal.ready?int(rc):-EIO);}
int rearm(){
 hardware.stage=7;int rc=checked(radio.standby());if(rc)return rc;
 rc=checked(radio.clearIrqFlags(RADIOLIB_LR2021_IRQ_ALL));if(rc)return rc;
 portENTER_CRITICAL(&mux);latched=false;portEXIT_CRITICAL(&mux);
 if(gpio_get_level(Irq))return -EIO;
 return checked(radio.startReceive());
}
int start(){
 hardware.stage=1;hal.init();if(cancelled())return -ECANCELED;if(!hal.ready)return -EIO;
 portENTER_CRITICAL(&mux);const bool permit=allowed&&cancel_serial==active_serial;
 if(permit){gpio_set_level(GPIO_NUM_3,0);gpio_set_level(GPIO_NUM_4,1);}portEXIT_CRITICAL(&mux);
 if(!permit)return -ECANCELED;
 // GC1109 rail settling, with cancellation observed on every10ms interval.
 for(unsigned i=0;i<10;++i){if(cancelled())return -ECANCELED;vTaskDelay(pdMS_TO_TICKS(10));}
 module.spiConfig.timeout=30;radio.irqDioNum=w12::lora_board::IrqDio;
 // Same T096 RC8 wire3 modem; XTAL0V, explicit header, CRC2, private sync0x12.
 int rc=checked(radio.begin(915.0f,500.0f,10,6,0x12,10,12,0.0f));
 // RadioLib's table convenience API discards status. Check every DIO command.
 for(const uint8_t dio:w12::lora_board::SwitchDios){
  const uint8_t function[]={dio,uint8_t(RADIOLIB_LR2021_DIO_FUNCTION_RF_SWITCH|(dio==5?RADIOLIB_LR2021_DIO_SLEEP_PULL_UP:RADIOLIB_LR2021_DIO_SLEEP_PULL_AUTO))};
  const uint8_t modes[]={dio,uint8_t(dio==w12::lora_board::RxSwitchDio?1u<<(LR2021::MODE_RX-1):0)};
  if(!rc)rc=checked(module.SPIwriteStream(RADIOLIB_LR2021_CMD_SET_DIO_FUNCTION,function,sizeof function));
  if(!rc)rc=checked(module.SPIwriteStream(RADIOLIB_LR2021_CMD_SET_DIO_RF_SWITCH_CONFIG,modes,sizeof modes));
 }
 if(!rc)rc=checked(radio.setCRC(2));
 if(!rc)rc=checked(radio.explicitHeader());
 if(!rc)rc=capture_start();
 return rc?rc:rearm();
}
int receive(){
 uint64_t edge;uint32_t gen,serial;bool have;
 portENTER_CRITICAL(&mux);have=latched;edge=raw_capture;gen=raw_generation;serial=raw_serial;portEXIT_CRITICAL(&mux);
 if(!have)return 0;
 hardware.stage=5;const uint32_t flags=radio.getIrqFlags();if(!hal.ready)return -EIO;
 if(flags&(RADIOLIB_LR2021_IRQ_CRC_ERROR|RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR)){inc(hardware.crc_bad);return rearm();}
 if(!(flags&RADIOLIB_LR2021_IRQ_RX_DONE))return -EIO;
 inc(hardware.rx_done);const size_t size=radio.getPacketLength();if(!hal.ready)return -EIO;
 if(size!=80&&size!=128){inc(hardware.malformed);return rearm();}
 uint8_t wire[128];int rc=checked(radio.readData(wire,size));if(rc)return rc;
 uint64_t tick=0;const uint64_t before=now();const bool readable=local(tick);const uint64_t after=now();
 if(!readable||tick<edge||tick-edge>5000000||gen!=core_generation||serial!=active_serial||after<before)inc(hardware.invalid_capture);
 else{
  const uint64_t age=(tick-edge+19)/20,mid=before+(after-before)/2;
  if(age>mid||edge>UINT64_MAX/4)inc(hardware.invalid_capture);
  else{hardware.capture_bound_ns=50+uint32_t(std::min<uint64_t>((after-before)*1000,UINT32_MAX-50));
   (void)w12_rc8_receive(wire,size,edge*4/5,capture_epoch,mid-age,after,gen);}
 }
 hardware.rssi_dbm=int16_t(radio.getRSSI());hardware.snr_quarter_db=int16_t(radio.getSNR()*4);
 if(!hal.ready)return -EIO;
 return rearm();
}
void publish(){
 w12_lora_view view;w12_rc8_snapshot(&view,now());
 // Worker-owned protocol state and hardware counters are merged outside the
 // short cross-core publication lock. Unknown uncertainty remains unknown.
 view.requested=false;view.active=owned.enabled;view.fault=failed;view.mode=uint8_t(policy.mode());view.transition_remaining_ms=policy.remaining_ms(now());
 view.ble_locked=ble_lock_observed;
 const auto observed_now=now();
 view.rc9_hold_remaining_ms=policy.mode()!=w12::LoRaMode::KeepOff&&observed_now<rc9_hold_observed_until?
  uint32_t((rc9_hold_observed_until-observed_now+999)/1000):0;
 view.rx_done=hardware.rx_done;view.crc_bad=hardware.crc_bad;view.malformed=hardware.malformed;view.spi_ok=hardware.spi_ok;view.spi_error=hardware.spi_error;view.invalid_capture=hardware.invalid_capture;view.capture_overflow=0;view.map_accepted=hardware.map_accepted;view.map_rejected=hardware.map_rejected;view.capture_bound_ns=hardware.capture_bound_ns;view.rssi_dbm=hardware.rssi_dbm;view.snr_quarter_db=hardware.snr_quarter_db;view.error=hardware.error;view.stage=hardware.stage;view.opcode=hardware.opcode;view.work_max_us=hardware.work_max_us;view.heartbeat_ms=uint32_t(now()/1000);view.stack_unused=uxTaskGetStackHighWaterMark(nullptr);
 view.spi_retired=!inflight;view.capture_released=!capture_active&&!edge_enabled;view.reset_asserted=resources&&gpio_get_level(Reset)==0;
 portENTER_CRITICAL(&mux);view.requested=owner.requested;view.invalid_capture+=capture_invalid;view.capture_overflow=capture_drops;view.request_generation=owner.generation;view.off_ack_generation=owner.off_ack;view.off_acknowledged=owner.drained();published=view;owned.failed=failed;owned.initialized=resources;owned.packets=hardware.rx_done;owned.crc_errors=hardware.crc_bad;owned.malformed=hardware.malformed;owned.status=int16_t(hardware.error);owned.radio_errors=hardware.spi_error;owned.work_max_us=hardware.work_max_us;basic=owned;portEXIT_CRITICAL(&mux);
}
bool configured_receiver(){return PTOB_W12_LORA_TX_FACTORY&&w12_rc8_init(PTOB_W12_FACTORY_ID,PTOB_W12_LORA_TX_FACTORY,PTOB_W12_GROUP_ID,PTOB_W12_KEY_EPOCH,PTOB_W12_GROUP_KEY);}
void worker_step(bool configured){
  const auto begin=now();bool permit,locked,cycle,change,held,rc9_current;uint8_t mode;uint32_t generation,serial;bool tripped;
  portENTER_CRITICAL(&mux);permit=allowed;serial=cancel_serial;tripped=emergency;locked=ble_locked&&begin>=ble_at&&begin-ble_at<250000;cycle=cycle_pending&&begin>=request_at&&begin-request_at<250000;change=mode_pending&&begin>=request_at&&begin-request_at<250000;mode=requested_mode;cycle_pending=mode_pending=false;portEXIT_CRITICAL(&mux);
  portENTER_CRITICAL(&mux);rc9_hold_observed_until=permit?rc9_client_until:0;held=begin<rc9_hold_observed_until;rc9_current=begin<rc9_source_until;portEXIT_CRITICAL(&mux);
  if(tripped)fault(-ETIMEDOUT);
  if(w12_fine_fault())fault(-EIO);
  ble_lock_observed=locked;policy.update(locked||rc9_current,begin,held);if(cycle)policy.toggle();if(change)policy.set(w12::LoRaMode(mode));
  bool want=configured&&permit&&!failed&&policy.requested();
  portENTER_CRITICAL(&mux);want=want&&allowed&&serial==cancel_serial;
  if(want!=owner.requested&&!owner.request(want)){failed=true;hardware.error=-EOVERFLOW;want=false;}
  generation=(want||owned.enabled||capture_active||inflight)?owner.begin():owner.generation;active_serial=serial;portEXIT_CRITICAL(&mux);
  if(want&&!owned.enabled){
   const auto next=w12_rc8_switch(true);portENTER_CRITICAL(&mux);core_generation=next;portEXIT_CRITICAL(&mux);const int rc=start();
   if(rc&&rc!=-ECANCELED)fault(rc);
   owned.enabled=rc==0;
  }
  if(owned.enabled&&!want){reset_radio();owned.enabled=false;}
  // Extend CH1's independent counter even during packet silence/wraps. A lost
  // counter cannot fall back to BLE coordinates or invent a capture timestamp.
  if(owned.enabled){uint64_t tick=0;const int rc=local(tick)?receive():-EIO;if(rc){if(rc!=-ECANCELED)fault(rc);reset_radio();owned.enabled=false;}}
  if(!owned.enabled){
   if(capture_active||edge_enabled)capture_stop();
   if(core_generation){(void)w12_rc8_switch(false);portENTER_CRITICAL(&mux);core_generation=0;portEXIT_CRITICAL(&mux);}
   reset_radio();
  }
  portENTER_CRITICAL(&mux);if(!owner.requested&&!stop_at)stop_at=begin;
  owner.finish(generation,!owned.enabled&&!capture_active&&!edge_enabled&&(!resources||gpio_get_level(Reset)==0),!inflight);
  if(owner.drained())stop_at=0;
  if(stop_at&&now()>=stop_at&&now()-stop_at>=200000){failed=true;hardware.error=-ETIMEDOUT;}
  portEXIT_CRITICAL(&mux);
  hardware.work_max_us=std::max(hardware.work_max_us,uint32_t(now()-begin));publish();
 }
void worker(void*){const bool configured=configured_receiver();for(;;){worker_step(configured);ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(2));}}
}
namespace v12 {
bool lora_start_worker(){return xTaskCreatePinnedToCore(worker,"w12_lora",8192,nullptr,1,&task,1)==pdPASS;}
bool lora_request(bool enable,uint32_t){
 if(enable)return false; // Receiver control uses explicit AUTO/ON/OFF only.
 portENTER_CRITICAL(&mux);allowed=false;if(cancel_serial!=UINT32_MAX)cancel_serial++;else emergency=true;const bool ok=owner.request(false);stop_at=now();reset_radio();portEXIT_CRITICAL(&mux);if(task)xTaskNotifyGive(task);return ok;
}
LoRaStatus lora_status(){portENTER_CRITICAL(&mux);auto result=basic;result.failed|=emergency;result.requested=owner.requested;result.worker_busy=owner.busy;result.request_generation=owner.generation;result.off_ack_generation=owner.off_ack;result.off_acknowledged=owner.drained();portEXIT_CRITICAL(&mux);return result;}
}
void w12_lora_control(bool locked,uint64_t sampled,bool permit,uint64_t client_until,uint64_t source_until){
 portENTER_CRITICAL(&mux);ble_locked=locked;ble_at=sampled;
 // Retain only original bounded leases/source deadlines, never a poll-based
 // extension. Explicit OFF/maintenance and faults still override radio hold.
 rc9_client_until=permit&&client_until>sampled&&client_until-sampled<=360000000?client_until:0;
 rc9_source_until=permit&&source_until>sampled&&source_until-sampled<=12000000?source_until:0;
 if(allowed&&!permit){if(cancel_serial!=UINT32_MAX)cancel_serial++;else emergency=true;(void)owner.request(false);stop_at=sampled;reset_radio();}
 allowed=permit;
 if(published.active&&sampled/1000>=published.heartbeat_ms&&sampled/1000-published.heartbeat_ms>=30000){reset_radio();emergency=true;allowed=false;if(cancel_serial!=UINT32_MAX)cancel_serial++;(void)owner.request(false);stop_at=sampled;}
 portEXIT_CRITICAL(&mux);
}
bool w12_lora_mode(w12::LoRaMode mode){
 if(!PTOB_W12_LORA_TX_FACTORY||uint8_t(mode)>2)return false;
 portENTER_CRITICAL(&mux);const bool ok=allowed&&!basic.failed;if(ok){requested_mode=uint8_t(mode);mode_pending=true;cycle_pending=false;request_at=now();}portEXIT_CRITICAL(&mux);if(ok&&task)xTaskNotifyGive(task);return ok;
}
bool w12_lora_cycle(){
 if(!PTOB_W12_LORA_TX_FACTORY)return false;
 portENTER_CRITICAL(&mux);const bool ok=allowed&&!basic.failed&&!cycle_pending&&!mode_pending;if(ok){cycle_pending=true;request_at=now();}portEXIT_CRITICAL(&mux);if(ok&&task)xTaskNotifyGive(task);return ok;
}
void w12_lora_snapshot(w12_lora_view& out){portENTER_CRITICAL(&mux);out=published;if(emergency){out.fault=true;out.active=false;out.until_us=0;out.error=-ETIMEDOUT;out.spi_retired=out.capture_released=out.off_acknowledged=false;}portEXIT_CRITICAL(&mux);}

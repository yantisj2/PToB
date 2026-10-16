#include "native_lora.hpp"
#ifdef V12_LORA_FORCED_OFF
namespace v12 {
namespace {LoRaStatus inactive;}
bool lora_start_worker(){return true;} // This reviewed C3 BLE-only image owns no LoRa task/SPI/pins.
bool lora_request(bool enable,uint32_t){
 if(enable||inactive.request_generation==UINT32_MAX)return false;
 ++inactive.request_generation;inactive.off_ack_generation=inactive.request_generation;return true;
}
LoRaStatus lora_status(){return inactive;}
}
#else
#include "bounded_log.hpp"
#include "lora_owner_gate.hpp"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <RadioLib.h>
#include <cstring>
namespace v12 {
namespace {
constexpr uint32_t Nss=8,Irq=7,Reset=12,Busy=13;
constexpr uint32_t BusyTimeoutMs=5;
portMUX_TYPE radio_mux=portMUX_INITIALIZER_UNLOCKED;
LoRaStatus owned,snapshot;LoRaOwnerGate owner;uint64_t deadline=0;TaskHandle_t task=nullptr;
void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
class Hal final:public RadioLibHal {
public:
  Hal():RadioLibHal(GPIO_MODE_INPUT,GPIO_MODE_OUTPUT,0,1,GPIO_INTR_POSEDGE,GPIO_INTR_NEGEDGE){}
  void init()override{
    if(ready_)return;
    spi_bus_config_t b{};b.mosi_io_num=10;b.miso_io_num=11;b.sclk_io_num=9;
    b.quadwp_io_num=-1;b.quadhd_io_num=-1;b.max_transfer_sz=256;
    if(spi_bus_initialize(SPI2_HOST,&b,SPI_DMA_CH_AUTO)!=ESP_OK)return;
    spi_device_interface_config_t d{};d.clock_speed_hz=2000000;d.spics_io_num=-1;d.queue_size=1;
    if(spi_bus_add_device(SPI2_HOST,&d,&spi_)!=ESP_OK){spi_bus_free(SPI2_HOST);return;}ready_=true;
  }
  void term()override{if(spi_){spi_bus_remove_device(spi_);spi_bus_free(SPI2_HOST);spi_=nullptr;}ready_=false;}
  void pinMode(uint32_t p,uint32_t m)override{if(p==RADIOLIB_NC)return;gpio_config_t c{};
    c.pin_bit_mask=1ULL<<p;c.mode=static_cast<gpio_mode_t>(m);if(gpio_config(&c)!=ESP_OK)ready_=false;}
  void digitalWrite(uint32_t p,uint32_t l)override{if(p!=RADIOLIB_NC&&gpio_set_level(static_cast<gpio_num_t>(p),l)!=ESP_OK)ready_=false;}
  uint32_t digitalRead(uint32_t p)override{return p==RADIOLIB_NC?0:gpio_get_level(static_cast<gpio_num_t>(p));}
  void attachInterrupt(uint32_t,void(*)(void),uint32_t)override{}
  void detachInterrupt(uint32_t)override{}
  void delay(RadioLibTime_t ms)override{vTaskDelay(pdMS_TO_TICKS(ms));}
  void delayMicroseconds(RadioLibTime_t us)override{const int64_t start=esp_timer_get_time();while(esp_timer_get_time()-start<int64_t(us)){};}
  RadioLibTime_t millis()override{return RadioLibTime_t(esp_timer_get_time()/1000);}
  RadioLibTime_t micros()override{return RadioLibTime_t(esp_timer_get_time());}
  long pulseIn(uint32_t,uint32_t,RadioLibTime_t)override{return 0;}
  void spiBegin(){}void spiEnd(){}void spiBeginTransaction(){}void spiEndTransaction(){}
  void spiTransfer(uint8_t* tx,size_t n,uint8_t* rx)override{if(!ready_||!n||n>256){ready_=false;return;}
    spi_transaction_t t{};t.length=n*8;t.tx_buffer=tx;t.rx_buffer=rx;
    if(spi_device_polling_start(spi_,&t,pdMS_TO_TICKS(5))!=ESP_OK){ready_=false;return;}
    if(spi_device_polling_end(spi_,pdMS_TO_TICKS(5))!=ESP_OK){ready_=false;spi_outstanding_=true;}}
  void yield()override{vTaskDelay(pdMS_TO_TICKS(1));}
  bool ready()const{return ready_;}
  bool spi_returned()const{return !spi_outstanding_;}
private:spi_device_handle_t spi_=nullptr;bool ready_=false,spi_outstanding_=false;
};
Hal hal;Module module(&hal,Nss,Irq,Reset,Busy);LR2021 radio(&module);
void rails_off(){gpio_set_level(GPIO_NUM_3,0);gpio_set_level(GPIO_NUM_4,0);}
bool command(int16_t rc){owned.status=rc;if(rc==RADIOLIB_ERR_NONE&&hal.ready())return true;
  inc(owned.radio_errors);owned.failed=true;owned.enabled=false;rails_off();return false;}
bool initialize(){
  gpio_config_t rails{};rails.pin_bit_mask=(1ULL<<3)|(1ULL<<4);rails.mode=GPIO_MODE_OUTPUT;
  if(gpio_set_level(GPIO_NUM_3,0)!=ESP_OK||gpio_set_level(GPIO_NUM_4,0)!=ESP_OK||gpio_config(&rails)!=ESP_OK)return false;
  if(gpio_set_level(GPIO_NUM_4,1)!=ESP_OK)return false;
  module.spiConfig.timeout=BusyTimeoutMs;radio.irqDioNum=11;
  static const uint32_t pins[]={RADIOLIB_LR2021_DIO5,RADIOLIB_LR2021_DIO8,RADIOLIB_LR2021_DIO9,RADIOLIB_NC,RADIOLIB_NC};
  static const Module::RfSwitchMode_t modes[]={
    {LR2021::MODE_STBY,{0,0,0}},{LR2021::MODE_RX,{0,0,1}},
    {LR2021::MODE_TX,{0,0,0}},{LR2021::MODE_RX_HF,{0,0,0}},
    {LR2021::MODE_TX_HF,{0,0,0}},END_OF_MODE_TABLE};
  if(!command(radio.begin(915.0f,500.0f,10,6,0x12,10,12,0.0f))||
     !command(radio.setRfSwitchTable(pins,modes))||!command(radio.setCRC(2))||
     !command(radio.explicitHeader())||!command(radio.startReceive()))return false;
  owned.initialized=owned.enabled=true;return true;
}
void worker(void*){
  bool last=false;
  while(true){
    bool want;uint64_t expiry;uint32_t generation;portENTER_CRITICAL(&radio_mux);
    // An acknowledged, never-initialized OFF radio has no GPIO/SPI work. Do
    // not manufacture an owner-busy interval on every10ms idle task wakeup.
    if(owner.idle_uninitialized(owned.initialized)){portEXIT_CRITICAL(&radio_mux);vTaskDelay(pdMS_TO_TICKS(10));continue;}
    want=owner.requested;expiry=deadline;generation=owner.begin();portEXIT_CRITICAL(&radio_mux);
    const uint64_t begin=uint64_t(esp_timer_get_time());
    if(want&&begin>=expiry)want=false;
    if(want&&!last&&!owned.failed){
      if(owned.initialized){gpio_set_level(GPIO_NUM_4,1);owned.enabled=command(radio.startReceive());}
      else if(!initialize()){owned.failed=true;inc(owned.radio_errors);rails_off();}
    }
    if(!want){if(owned.enabled)command(radio.sleep());owned.enabled=false;
      if(owned.initialized)rails_off();}
    if(owned.enabled){
      if(gpio_get_level(GPIO_NUM_13)){owned.failed=true;owned.enabled=false;inc(owned.radio_errors);rails_off();}
      else{const uint32_t irq=radio.getIrqFlags();
        if(!hal.ready()){owned.failed=true;owned.enabled=false;inc(owned.radio_errors);rails_off();}
        else if(irq&RADIOLIB_LR2021_IRQ_CRC_ERROR){inc(owned.crc_errors);if(command(radio.clearIrqFlags(RADIOLIB_LR2021_IRQ_ALL)))command(radio.startReceive());}
        else if(irq&RADIOLIB_LR2021_IRQ_RX_DONE){
          const size_t length=radio.getPacketLength();uint8_t wire[64]{};
          if(length!=64)inc(owned.malformed);
          else if(command(radio.readData(wire,sizeof(wire)))){
            inc(owned.packets);char hex[129];static const char digits[]="0123456789abcdef";
            for(unsigned i=0;i<64;++i){hex[2*i]=digits[wire[i]>>4];hex[2*i+1]=digits[wire[i]&15];}hex[128]=0;
            logf("W12_V12_LORA callback_us=%llu packet=%lu bytes=64 hex=%s transport=RAW64_FUNCTIONAL_RX_ONLY"
              " physical_timestamp=UNAVAILABLE authentication=UNQUALIFIED authority=NONE BLE_update=0\n",
              (unsigned long long)begin,(unsigned long)owned.packets,hex);
          }
          if(!owned.failed&&command(radio.clearIrqFlags(RADIOLIB_LR2021_IRQ_ALL)))command(radio.startReceive());
        }
      }
    }
    const uint64_t work=uint64_t(esp_timer_get_time())-begin;if(work>owned.work_max_us)owned.work_max_us=work;
    portENTER_CRITICAL(&radio_mux);
    if(generation==owner.generation&&owner.requested&&begin>=expiry)owner.request(false);
    owner.finish(generation,!owned.enabled,hal.spi_returned());snapshot=owned;
    portEXIT_CRITICAL(&radio_mux);
    last=want;vTaskDelay(pdMS_TO_TICKS(10));
  }
}
}
bool lora_start_worker(){return xTaskCreatePinnedToCore(worker,"v12_lora",6144,nullptr,1,&task,1)==pdPASS;}
bool lora_request(bool enable,uint32_t duration_ms){
  if(enable&&(std::strcmp(V12_UNIT,"C1")||duration_ms<1000||duration_ms>10000))return false;
  portENTER_CRITICAL(&radio_mux);const bool admitted=(!enable||!snapshot.failed)&&owner.request(enable);
  if(admitted)deadline=uint64_t(esp_timer_get_time())+uint64_t(duration_ms)*1000;
  portEXIT_CRITICAL(&radio_mux);return admitted;
}
LoRaStatus lora_status(){portENTER_CRITICAL(&radio_mux);auto s=snapshot;s.requested=owner.requested;s.worker_busy=owner.busy;
  s.request_generation=owner.generation;s.off_ack_generation=owner.off_ack;s.off_acknowledged=owner.drained();portEXIT_CRITICAL(&radio_mux);return s;}
}

#endif

// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual owner and DMA adapter with deterministic SDK/RadioLib call boundaries.
#include "native_lora.hpp"
#include "lora_core.h"
#include "driver/spi_master.h"
#include "freertos/task.h"
#include <array>
#include <cassert>
#include <cstring>
namespace fake {uint64_t us=1000000;std::array<int,50> gpio{};unsigned starts=0,ends=0,inputs=0;bool dma_fail=false,capture=false,cancel=false,capture_fail=false;spi_transaction_t* pending=nullptr;uint32_t gen=0;}
int64_t esp_timer_get_time(){return int64_t(fake::us);}
void vTaskDelay(TickType_t t){fake::us+=uint64_t(t)*1000;}
uint32_t ulTaskNotifyTake(BaseType_t,TickType_t){return 0;}
void xTaskNotifyGive(TaskHandle_t){}void vTaskNotifyGiveFromISR(TaskHandle_t,BaseType_t*){}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t){return 6144;}
BaseType_t xTaskCreatePinnedToCore(void(*)(void*),const char*,uint32_t s,void*,unsigned p,TaskHandle_t* o,int c){assert(s==8192&&p==1&&c==1);*o=(void*)1;return pdPASS;}
esp_err_t gpio_config(const gpio_config_t* c){assert(!(c->pin_bit_mask&((1ULL<<39)|(1ULL<<41)|(1ULL<<48)|(1ULL<<16))));return 0;}
esp_err_t gpio_set_level(gpio_num_t p,int v){assert(p==3||p==4||p==8||p==12);fake::gpio[p]=v;return 0;}
int gpio_get_level(gpio_num_t p){assert(p==14||p==13||p==12||p==8);return fake::gpio[p];}
int gpio_set_direction(gpio_num_t p,int){assert(p==14||p==13||p==12||p==8);return 0;}
esp_err_t spi_bus_initialize(int h,const spi_bus_config_t* c,int){assert(h==2&&c->sclk_io_num==9&&c->mosi_io_num==10&&c->miso_io_num==11);return 0;}
esp_err_t spi_bus_add_device(int,const spi_device_interface_config_t*,spi_device_handle_t* o){*o=(void*)1;return 0;}
esp_err_t spi_device_queue_trans(spi_device_handle_t,spi_transaction_t* t,TickType_t wait){assert(wait==5&&!fake::pending);fake::pending=t;++fake::starts;return 0;}
esp_err_t spi_device_get_trans_result(spi_device_handle_t,spi_transaction_t** o,TickType_t wait){assert(wait==5&&fake::pending);if(fake::dma_fail)return -1;*o=fake::pending;std::memset((*o)->rx_buffer,0,(*o)->length/8);fake::pending=nullptr;++fake::ends;if(fake::cancel){fake::cancel=false;assert(v12::lora_request(false,0));}return 0;}
extern "C" bool clock_reference_read(uint64_t* t){*t=fake::us*20;return true;}
bool w12_fine_lora_start(bool(*)(uint64_t)){fake::capture=true;return !fake::capture_fail;}
bool w12_fine_fault(){return false;}
bool w12_fine_lora_stop(){fake::capture=false;return true;}
extern "C" bool w12_rc8_init(uint64_t f,uint64_t t,uint64_t,uint32_t,const uint8_t*){return f==2&&t==1;}
extern "C" uint32_t w12_rc8_switch(bool){return ++fake::gen;}
extern "C" bool w12_rc8_receive(const uint8_t*,size_t n,uint64_t t,uint64_t e,uint64_t l,uint64_t now,uint32_t g){assert(n==80&&t&&e&&g&&l<=now);++fake::inputs;return false;}
extern "C" void w12_rc8_snapshot(w12_lora_view* o,uint64_t n){*o={};o->sampled_us=n;}
#include "../main/w12_lora_radio.cpp"
void step(bool locked,uint64_t advance=2000){fake::us+=advance;w12_lora_control(locked,fake::us,true);worker_step(true);}
int main(int argc,char** argv){
 assert(argc==2&&configured_receiver());const auto mode=argv[1];
 if(!std::strcmp(mode,"capture"))fake::capture_fail=true;
 step(false,0);assert(!resources&&owner.drained());step(false,179999999);assert(!resources);step(false,1);
 if(!std::strcmp(mode,"capture")){worker_step(true);assert(failed&&!owned.enabled&&!fake::capture&&owner.drained()&&!fake::gpio[12]&&!fake::gpio[4]);return 0;}
 assert(test_irq_dio==8&&test_irq_gpio==14&&test_dio_modes[11]==2&&test_dio_modes[9]==0&&test_dio_modes[10]==0&&test_dio_modes[5]==0&&test_dio_modes[6]==0);
 assert(owned.enabled&&fake::capture&&fake::gpio[4]&&!fake::gpio[3]);
 if(!std::strcmp(mode,"normal")){
  on_edge(fake::us*20);test_irq=2;step(false);assert(fake::inputs==1);
  on_edge(fake::us*20);test_irq=4;step(false);assert(hardware.crc_bad==1&&fake::inputs==1);
  test_size=17;on_edge(fake::us*20);test_irq=2;step(false);assert(hardware.malformed==1);
  for(unsigned i=0;i<1801;i++)step(true,100000);
  assert(!owned.enabled&&owner.drained()&&!fake::capture&&fake::gpio[4]==0);
 }else if(!std::strcmp(mode,"dma")){
  fake::dma_fail=true;uint8_t t[]={1,0},r[2]{};hal.spiTransfer(t,2,r);assert(inflight&&!hal.ready&&fake::pending);
  assert(v12::lora_request(false,0));worker_step(true);const auto starts=fake::starts;
  for(unsigned i=0;i<5;i++)worker_step(true);
  assert(!owner.drained()&&fake::pending&&inflight&&fake::starts==starts&&!fake::gpio[12]&&!fake::gpio[4]);
 }else if(!std::strcmp(mode,"cancel")){
  assert(v12::lora_request(false,0));worker_step(true);w12_lora_control(true,fake::us,true);assert(w12_lora_mode(w12::LoRaMode::KeepOn));fake::cancel=true;worker_step(true);worker_step(true);
  assert(!failed&&owner.drained()&&!fake::gpio[12]&&!fake::gpio[4]&&!fake::capture);
 }else if(!std::strcmp(mode,"tx")){
  const auto starts=fake::starts;uint8_t t[]={2,0x0d},r[2]{};hal.spiTransfer(t,2,r);assert(!hal.ready&&fake::starts==starts);
 }else assert(false);
}

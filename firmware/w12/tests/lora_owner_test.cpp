// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual owner and DMA adapter with deterministic SDK/RadioLib call boundaries.
#include "native_lora.hpp"
#include "lora_core.h"
#include "driver/spi_master.h"
#include "freertos/task.h"
#include <array>
#include <cassert>
#include <cstring>
extern uint32_t test_irq;
namespace fake {uint64_t us=1000000;std::array<int,50> gpio{};unsigned starts=0,ends=0,inputs=0,fine_reads=0,lora_reads=0;bool dma_fail=false,capture=false,cancel=false,capture_fail=false,clock_ok=true;spi_transaction_t* pending=nullptr;uint32_t gen=0;}
int64_t esp_timer_get_time(){return int64_t(fake::us);}
void vTaskDelay(TickType_t t){fake::us+=uint64_t(t)*1000;}
uint32_t ulTaskNotifyTake(BaseType_t,TickType_t){return 0;}
void xTaskNotifyGive(TaskHandle_t){}void vTaskNotifyGiveFromISR(TaskHandle_t,BaseType_t*){}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t){return 6144;}
BaseType_t xTaskCreatePinnedToCore(void(*)(void*),const char*,uint32_t s,void*,unsigned p,TaskHandle_t* o,int c){assert(s==8192&&p==1&&c==1);*o=(void*)1;return pdPASS;}
esp_err_t gpio_config(const gpio_config_t* c){assert(!(c->pin_bit_mask&((1ULL<<39)|(1ULL<<41)|(1ULL<<48)|(1ULL<<16))));return 0;}
esp_err_t gpio_set_level(gpio_num_t p,int v){assert(p==3||p==4||p==8||p==12);fake::gpio[p]=v;return 0;}
int gpio_get_level(gpio_num_t p){assert(p==14||p==13||p==12||p==8);return p==14?test_irq!=0:fake::gpio[p];}
int gpio_set_direction(gpio_num_t p,int){assert(p==14||p==13||p==12||p==8);return 0;}
esp_err_t spi_bus_initialize(int h,const spi_bus_config_t* c,int){assert(h==2&&c->sclk_io_num==9&&c->mosi_io_num==10&&c->miso_io_num==11);return 0;}
esp_err_t spi_bus_add_device(int,const spi_device_interface_config_t*,spi_device_handle_t* o){*o=(void*)1;return 0;}
esp_err_t spi_device_queue_trans(spi_device_handle_t,spi_transaction_t* t,TickType_t wait){assert(wait==5&&!fake::pending);fake::pending=t;++fake::starts;return 0;}
esp_err_t spi_device_get_trans_result(spi_device_handle_t,spi_transaction_t** o,TickType_t wait){assert(wait==5&&fake::pending);if(fake::dma_fail)return -1;*o=fake::pending;std::memset((*o)->rx_buffer,0,(*o)->length/8);fake::pending=nullptr;++fake::ends;if(fake::cancel){fake::cancel=false;assert(v12::lora_request(false,0));}return 0;}
extern "C" bool clock_reference_read(uint64_t* t){++fake::fine_reads;fake::us+=50;*t=9000000+fake::us*20;return true;}
bool w12_fine_lora_current(uint64_t* t){++fake::lora_reads;*t=fake::us*20;return fake::clock_ok;}
bool w12_fine_lora_start(bool(*)(uint64_t)){fake::capture=true;return !fake::capture_fail;}
bool w12_fine_fault(){return false;}
bool w12_fine_lora_stop(){fake::capture=false;return true;}
#ifndef PTOB_W12_TEST_REAL_PASSIVE
extern "C" bool w12_rc8_init(uint64_t f,uint64_t t,uint64_t,uint32_t,const uint8_t*){return f==2&&t==1;}
extern "C" uint32_t w12_rc8_switch(bool){return ++fake::gen;}
extern "C" void w12_rc8_clock(uint64_t t,uint64_t e,uint64_t n){assert(t&&e&&n);}
extern "C" bool w12_rc8_receive(const uint8_t*,size_t n,uint64_t t,uint64_t e,uint64_t l,uint64_t now,uint32_t g){assert((n==24||n==32||n==112)&&t&&e&&g&&l<=now);++fake::inputs;return false;}
extern "C" void w12_rc8_snapshot(w12_lora_view* o,uint64_t n){*o={};o->sampled_us=n;}
#else
extern "C" void w12_test_packet(uint8_t*,unsigned,uint32_t,uint64_t);
#endif
#include "../main/w12_lora_radio.cpp"
unsigned wired_edges=0;
void irq(uint32_t flags){const bool high=gpio_get_level(14);test_irq|=flags;if(!high&&gpio_get_level(14)){++wired_edges;on_edge(fake::us*20);}}
void step(bool locked,uint64_t advance=2000){fake::us+=advance;w12_lora_control(locked,fake::us,true);worker_step(true);}
int main(int argc,char** argv){
 assert(argc==2&&configured_receiver());const auto mode=argv[1];
 if(!std::strcmp(mode,"capture"))fake::capture_fail=true;
 step(false,0);
 if(!std::strcmp(mode,"capture")){worker_step(true);assert(failed&&!owned.enabled&&!fake::capture&&owner.drained()&&!fake::gpio[12]&&!fake::gpio[4]);return 0;}
 assert(test_freq==922.4f&&test_bw==500.0f&&test_sf==11&&test_cr==8&&test_preamble==12&&test_sync==0x12&&test_irq_mask==RADIOLIB_IRQ_RX_DEFAULT_FLAGS);
 assert(test_irq_dio==8&&test_irq_gpio==14&&test_dio_modes[11]==2&&test_dio_modes[9]==0&&test_dio_modes[10]==0&&test_dio_modes[5]==0&&test_dio_modes[6]==0);
 assert(owned.enabled&&fake::capture&&fake::gpio[4]&&!fake::gpio[3]);
#ifdef PTOB_W12_TEST_REAL_PASSIVE
 if(!std::strcmp(mode,"wire8")){
  constexpr uint64_t base=1700000000000000000;
  auto packet=[&](unsigned kind,uint32_t cursor,uint64_t ns){
   w12_test_packet(test_packet,kind,cursor,ns);test_size=kind==2?112:kind==3?24:32;
   irq(RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID);step(false);assert(gpio_get_level(14)==1);
   fake::us+=kind==2?700000:100000;const auto edges=wired_edges;irq(RADIOLIB_LR2021_IRQ_RX_DONE);assert(wired_edges==edges);step(false);
  };
  packet(2,0,base+fake::us*1000);
  for(unsigned i=0;i<9;++i){
   const auto at=fake::us;packet(3,2*i,0);packet(4,2*i,base+at*1000-99328000);step(false);
   if(i<8)fake::us=at+30000000;
  }
  w12_lora_view view;w12_lora_snapshot(view);
  assert(view.state==2&&view.trained&&view.model_valid&&view.accepted==9&&view.link_samples==9&&view.link_rms_ns<=1);
  assert(view.root==1&&view.emitter==1&&view.root_boot==22&&view.capture_epoch==1&&view.utc_us>0);
  assert(view.authority==0&&view.assurance==0&&view.created_ns==0&&view.expires_ns==0);
  assert(wired_edges==19&&!hardware.invalid_capture&&!hardware.crc_bad&&!hardware.spi_error&&!capture_drops);return 0;
 }
#endif
 if(!std::strcmp(mode,"clock")){
  fake::clock_ok=false;step(false);step(false);assert(failed&&!owned.enabled&&owner.drained()&&!fake::capture&&!fake::fine_reads);
 }else if(!std::strcmp(mode,"normal")){
  const auto before_reads=fake::lora_reads;
  // Keep the independent counter alive through six minutes of RF silence.
  for(unsigned i=0;i<3600;++i)step(false,100000);
  assert(fake::lora_reads>=before_reads+3600&&!fake::fine_reads&&owned.enabled);
  irq(RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID);step(false);assert(fake::inputs==0&&header_pending&&gpio_get_level(14)==1);
  const auto frozen=header_capture;fake::us+=200000;const auto edges=wired_edges;irq(RADIOLIB_LR2021_IRQ_RX_DONE);assert(wired_edges==edges);step(false);assert(fake::inputs==1&&header_capture==frozen&&!header_pending&&fake::lora_reads>0&&!fake::fine_reads);
  irq(RADIOLIB_LR2021_IRQ_RX_DONE);step(false);assert(fake::inputs==1&&hardware.invalid_capture==1);
  irq(RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID);step(false);on_edge(fake::us*20+1);step(false);assert(hardware.invalid_capture==2&&!header_pending&&fake::inputs==1);
  irq(RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID);step(false);step(false,1000001);assert(hardware.invalid_capture==3&&!header_pending&&fake::inputs==1);
  irq(RADIOLIB_LR2021_IRQ_CRC_ERROR);step(false);assert(hardware.crc_bad==1&&fake::inputs==1);
  irq(RADIOLIB_LR2021_IRQ_LORA_HDR_CRC_ERROR);step(false);assert(hardware.crc_bad==2&&fake::inputs==1);
  test_size=17;irq(RADIOLIB_LR2021_IRQ_LORA_HEADER_VALID|RADIOLIB_LR2021_IRQ_RX_DONE);step(false);assert(hardware.malformed==1&&!failed);
  for(unsigned i=0;i<1801;i++)step(true,100000);
  assert(owned.enabled&&!owner.drained()&&fake::capture);assert(w12_lora_mode(w12::LoRaMode::KeepOff));step(true);assert(!owned.enabled&&owner.drained()&&!fake::capture&&fake::gpio[4]==0);
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

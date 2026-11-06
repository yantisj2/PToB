// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Drive the actual production owner with deterministic peripheral boundaries.
#include "native_lora.hpp"
#include "lora_core.h"
#include "driver/spi_master.h"
#include "driver/mcpwm_cap.h"
#include "freertos/task.h"
#include <array>
#include <cassert>
#include <cstring>
#include <vector>
namespace fake {
uint64_t us=1000000;std::array<int,50> gpio{};std::array<uint8_t,65536> regs{};
unsigned writes=0,bus=0,starts=0,ends=0,core_inputs=0,high_after_cancel=0;
unsigned lora_reference_reads=0,fine_reference_reads=0;
uint32_t epoch=0,core_gen=0,raw_offset=0xfedc0000;
std::vector<uint8_t> ops;spi_transaction_t* pending=nullptr;
CaptureCallback physical=nullptr,software=nullptr;
bool capture_timer=false,timer_running=false,physical_enabled=false,software_enabled=false;
bool current_enabled=false,current_fail=false;unsigned allocated_channels=0;
bool dma_fail=false,cancel=false,cancelled=false,wide_map_once=false;int fail_enable=0,fail_disable=0;
bool gain_bad_readback=false;
uint16_t irq=0;unsigned packet_size=112;std::array<uint8_t,128> packet{};
uint32_t capture(){return uint32_t(us*80)+raw_offset;}
}
int64_t esp_timer_get_time(){return int64_t(fake::us);}
void vTaskDelay(TickType_t ticks){fake::us+=uint64_t(ticks)*1000;}
uint32_t ulTaskNotifyTake(BaseType_t,TickType_t){return 0;}
void xTaskNotifyGive(TaskHandle_t){}
void vTaskNotifyGiveFromISR(TaskHandle_t,BaseType_t*){}
unsigned uxTaskGetStackHighWaterMark(TaskHandle_t){return 6144;}
BaseType_t xTaskCreatePinnedToCore(void(*)(void*),const char*,uint32_t stack,void*,unsigned priority,TaskHandle_t* out,int core){assert(stack==8192&&priority==1&&core==1);*out=reinterpret_cast<void*>(1);return pdPASS;}
esp_err_t gpio_config(const gpio_config_t*){return 0;}
esp_err_t gpio_set_level(gpio_num_t pin,int level){fake::writes++;fake::gpio[pin]=level;if(pin==12&&level&&fake::cancelled)fake::high_after_cancel++;return 0;}
int gpio_get_level(gpio_num_t pin){return fake::gpio[pin];}
esp_err_t spi_bus_initialize(int host,const spi_bus_config_t* c,int){assert(host==2&&c->mosi_io_num==10&&c->miso_io_num==11&&c->sclk_io_num==9);fake::bus++;return 0;}
esp_err_t spi_bus_add_device(int,const spi_device_interface_config_t*,spi_device_handle_t* out){*out=reinterpret_cast<void*>(1);return 0;}
esp_err_t spi_device_queue_trans(spi_device_handle_t,spi_transaction_t* t,TickType_t wait){assert(wait==5&&!fake::pending);fake::pending=t;fake::starts++;return 0;}
esp_err_t spi_device_get_trans_result(spi_device_handle_t,spi_transaction_t** completed,TickType_t wait){
 assert(wait==5);
 auto* t=fake::pending;assert(t);fake::ends++;const auto* tx=static_cast<const uint8_t*>(t->tx_buffer);auto* rx=static_cast<uint8_t*>(t->rx_buffer);const size_t n=t->length/8;fake::ops.push_back(tx[0]);assert(tx[0]!=0x83);
 if(fake::dma_fail)return -1;
 std::memset(rx,0,n);if(n>1)rx[1]=0x24;
 if(tx[0]==0x0d)fake::regs[(unsigned(tx[1])<<8)|tx[2]]=tx[3];
 if(tx[0]==0x1d){rx[3]=0x24;rx[4]=fake::gain_bad_readback&&tx[1]==0x08&&tx[2]==0xac?0x94:fake::regs[(unsigned(tx[1])<<8)|tx[2]];}
 if(tx[0]==0x02){fake::irq&=~uint16_t((unsigned(tx[1])<<8)|tx[2]);fake::gpio[14]=fake::irq!=0;}
 if(tx[0]==0x86)assert(n==5&&tx[1]==0x39&&tx[2]==0xa6&&tx[3]==0x66&&tx[4]==0x66);
 if(tx[0]==0x8b)assert(n==5&&tx[1]==11&&tx[2]==6&&tx[3]==4&&tx[4]==0);
 if(tx[0]==0x82){assert(n==4&&tx[1]==255&&tx[2]==255&&tx[3]==255);assert(fake::regs[0x08ac]==0x96);}
 if(tx[0]==0x97)assert(n==5&&tx[1]==2&&tx[2]==0&&tx[3]==1&&tx[4]==64);
 if(tx[0]==0x9d)assert(n==2&&tx[1]==1);
 if(tx[0]==0x12){rx[2]=uint8_t(fake::irq>>8);rx[3]=uint8_t(fake::irq);}
 if(tx[0]==0x13){rx[2]=uint8_t(fake::packet_size);rx[3]=0;}
 if(tx[0]==0x1e){rx[2]=0x24;std::memcpy(rx+3,fake::packet.data(),n-3);}
 if(tx[0]==0x14){rx[2]=140;rx[3]=24;}
 *completed=t;fake::pending=nullptr;
 if(fake::cancel&&tx[0]==0x8b){fake::cancel=false;fake::cancelled=true;assert(v12::lora_request(false,0));}
 return 0;
}
esp_err_t mcpwm_new_capture_timer(const mcpwm_capture_timer_config_t* c,mcpwm_cap_timer_handle_t* out){assert(c->group_id==0);*out=reinterpret_cast<void*>(1);return 0;}
esp_err_t mcpwm_capture_timer_get_resolution(mcpwm_cap_timer_handle_t,uint32_t* out){*out=80000000;return 0;}
esp_err_t mcpwm_new_capture_channel(mcpwm_cap_timer_handle_t,const mcpwm_capture_channel_config_t* c,mcpwm_cap_channel_handle_t* out){assert(c->gpio_num==14||c->gpio_num==-1);*out=reinterpret_cast<void*>(uintptr_t(++fake::allocated_channels));return 0;}
esp_err_t mcpwm_capture_channel_register_event_callbacks(mcpwm_cap_channel_handle_t c,const mcpwm_capture_event_callbacks_t* cb,void*){(c==reinterpret_cast<void*>(1)?fake::physical:fake::software)=cb->on_cap;return 0;}
esp_err_t mcpwm_capture_timer_enable(mcpwm_cap_timer_handle_t){fake::capture_timer=true;return 0;}
esp_err_t mcpwm_capture_timer_start(mcpwm_cap_timer_handle_t){fake::timer_running=true;return 0;}
esp_err_t mcpwm_capture_channel_enable(mcpwm_cap_channel_handle_t c){const auto i=int(reinterpret_cast<uintptr_t>(c));if(fake::fail_enable==i)return -1;(i==1?fake::physical_enabled:i==2?fake::software_enabled:fake::current_enabled)=true;return 0;}
esp_err_t mcpwm_capture_channel_disable(mcpwm_cap_channel_handle_t c){if(fake::fail_disable==int(reinterpret_cast<uintptr_t>(c)))return -1;(c==reinterpret_cast<void*>(1)?fake::physical_enabled:c==reinterpret_cast<void*>(2)?fake::software_enabled:fake::current_enabled)=false;return 0;}
esp_err_t mcpwm_capture_timer_stop(mcpwm_cap_timer_handle_t){if(fake::fail_disable==4)return -1;fake::timer_running=false;return 0;}
esp_err_t mcpwm_capture_timer_disable(mcpwm_cap_timer_handle_t){if(fake::fail_disable==5)return -1;fake::capture_timer=false;return 0;}
esp_err_t mcpwm_capture_channel_trigger_soft_catch(mcpwm_cap_channel_handle_t c){mcpwm_capture_event_data_t e{fake::capture()};fake::software(c,&e,nullptr);if(fake::wide_map_once){fake::us+=21;fake::wide_map_once=false;}return 0;}
extern "C" bool n30_lora_reference_bracket(mcpwm_cap_channel_handle_t c,uint64_t* before,uint64_t* after){*before=fake::us*20;const auto rc=mcpwm_capture_channel_trigger_soft_catch(c);*after=fake::us*20;return rc==ESP_OK;}
extern "C" bool n30_lora_reference_read(uint64_t* out){++fake::lora_reference_reads;*out=fake::us*20;return true;}
extern "C" int n30_lora_reference_current(mcpwm_cap_channel_handle_t c,uint32_t* out,uint64_t* sampled){assert(c==reinterpret_cast<void*>(3));if(!fake::current_enabled||!fake::timer_running||fake::current_fail)return 0x107;*out=fake::capture();*sampled=fake::us;return 0;}
// A selected fine clock can spend longer than the LoRa map's20us enclosure
// in cross-core receiver/fence work. It must never supply LoRa's brackets.
extern "C" bool clock_reference_read(uint64_t* out){++fake::fine_reference_reads;fake::us+=50;*out=1000000+fake::us*20;return true;}
// Separate codec/policy tests exercise real authenticated RC8. These stubs only
// let the peripheral test observe whether the owner invents or loses an edge.
#ifndef PTOB_N30_TEST_REAL_RC8
extern "C" bool n30_rc8_init(uint64_t f,uint64_t t,uint64_t,uint32_t,const uint8_t*){return f==2&&t==1;}
extern "C" uint32_t n30_rc8_switch(bool){return ++fake::core_gen;}
extern "C" bool n30_rc8_receive(const uint8_t*,size_t n,uint64_t tick,uint64_t epoch,uint64_t local,uint64_t now,uint32_t gen){assert((n==112||n==24||n==32)&&epoch&&gen&&tick<=fake::us*16&&local<=now);fake::core_inputs++;return false;}
extern "C" void n30_rc8_clock(uint64_t,uint64_t,uint64_t){}
extern "C" void n30_rc8_clock_sample(uint64_t,uint64_t,uint64_t,uint64_t){}
extern "C" bool n30_rc8_hold60(uint64_t,uint64_t){return false;}
extern "C" void n30_rc8_radio_fault(void){}
extern "C" void n30_rc8_snapshot(n30_lora_view* out,uint64_t now){*out={};out->sampled_us=now;out->generation=fake::core_gen;}
extern "C" void n30_rc8_snapshot_sample(n30_lora_view* out,uint64_t sampled,uint64_t){n30_rc8_snapshot(out,sampled);}
#endif
#ifndef N30_LORA_OWNER_SOURCE
#define N30_LORA_OWNER_SOURCE "../main/native_lora.cpp"
#endif
#include N30_LORA_OWNER_SOURCE
namespace {
void cold(){
 fake::us=1000000;fake::gpio={};fake::regs={};fake::writes=fake::bus=fake::starts=fake::ends=fake::core_inputs=fake::high_after_cancel=0;fake::ops.clear();fake::pending=nullptr;fake::physical=fake::software=nullptr;fake::capture_timer=fake::timer_running=fake::physical_enabled=fake::software_enabled=false;fake::dma_fail=fake::cancel=fake::cancelled=fake::wide_map_once=false;fake::fail_enable=fake::fail_disable=0;fake::irq=0;fake::core_gen=0;fake::packet_size=112;
 owner={};basic={};owned={};published={};hardware={};policy={};mapping={};qualified_map={};spi=cap_timer=edge_channel=map_channel=nullptr;task=nullptr;ble_at=request_at=last_map=map_pending=map_before=map_after=stop_at=0;cancel_serial=active_serial=core_generation=map_serial=map_expected=map_capture=raw_capture=raw_generation=raw_serial=0;capture_epoch=0;header_capture=header_generation=header_serial=0;header_until=0;header_pending=false;requested_mode=0;mode_pending=cycle_pending=ble_locked=ble_lock_observed=allowed=ble_parked=false;resources=capture_active=inflight=latched=failed=timer_enabled=timer_started=map_enabled=edge_enabled=emergency=false;capture_drops=capture_invalid=0;
 fake::gain_bad_readback=false;
 hold_pending_id=hold_pending_us=hold_last_id=0;hold_result=0;
 cleanup_failed=published_cleanup_fault=false;current_failure={};previous_current_raw=0;previous_current_us=0;native_error=0;current_channel=nullptr;current_enabled=false;native_clock.reset();fake::current_enabled=fake::current_fail=false;fake::allocated_channels=0;
 assert(configured_receiver());
}
void step(bool locked,uint64_t advance=2000){fake::us+=advance;n30_lora_control(locked,fake::us,true);worker_step(true);}
[[maybe_unused]] void force_on(){n30_lora_control(true,fake::us,true);assert(n30_lora_mode(n30::LoRaMode::KeepOn));worker_step(true);}
}
#ifdef PTOB_N30_TEST_REAL_RC8
#include "lora_owner_interop.inc"
#else
int main(){
 cold();fake::gain_bad_readback=true;force_on();worker_step(true);
 assert(failed&&hardware.error==-EIO&&!owned.enabled&&fake::gpio[12]==0&&owner.drained());
 for(auto op:fake::ops)assert(op!=0x82);
 cold();n30_lora_control(false,fake::us,true,true);worker_step(true);
 assert(policy.mode()==n30::LoRaMode::Auto&&owner.requested&&owned.enabled&&!failed);
 assert(n30_lora_mode(n30::LoRaMode::KeepOff));worker_step(true);assert(!owner.requested&&!owned.enabled&&owner.drained());
 n30_lora_control(false,fake::us,true,true);worker_step(true);assert(policy.mode()==n30::LoRaMode::KeepOff&&!owner.requested);
 fake::regs[0x08ac]=0x94; // A fresh reset must restore boost before RX.
 assert(n30_lora_mode(n30::LoRaMode::KeepOn));worker_step(true);assert(owner.requested&&owned.enabled&&fake::regs[0x08ac]==0x96);
 n30_lora_control(false,fake::us,false,true);worker_step(true);assert(!owner.requested&&!owned.enabled&&owner.drained());
 cold();step(true,0);assert(owner.requested&&owned.enabled&&fake::bus==1);for(unsigned i=0;i<1800;i++)step(true,100000);assert(owner.requested&&owned.enabled&&!hardware.rx_done&&!fake::core_inputs);
 cold();force_on();assert(owned.enabled);step(true);step(true);fake::irq=0x10;fake::gpio[14]=1;mcpwm_capture_event_data_t header{fake::capture()};on_edge(edge_channel,&header,nullptr);step(true);assert(header_pending&&fake::core_inputs==0);fake::us+=300000;fake::irq=2;fake::gpio[14]=1;mcpwm_capture_event_data_t e{fake::capture()};on_edge(edge_channel,&e,nullptr);step(true);assert(fake::core_inputs==1&&hardware.rx_done==1&&hardware.rssi_dbm==-70&&!header_pending);
 assert(fake::lora_reference_reads>0&&fake::fine_reference_reads==0);
 fake::irq=0x42;fake::gpio[14]=1;e.cap_value=fake::capture();on_edge(edge_channel,&e,nullptr);step(true);assert(fake::core_inputs==1&&hardware.crc_bad==1&&!failed);
 fake::packet_size=17;fake::irq=2;fake::gpio[14]=1;e.cap_value=fake::capture();on_edge(edge_channel,&e,nullptr);step(true);assert(hardware.malformed==1&&fake::core_inputs==1);
 assert(v12::lora_request(false,0));worker_step(true);worker_step(true);assert(owner.drained()&&!owned.enabled&&fake::gpio[12]==0);
 // A single >20us software bracket cannot discard the old physical map.
 cold();force_on();for(unsigned i=0;i<10;i++){step(true,250000);step(true,2000);}
 const auto accepted_map=hardware.map_accepted;assert(accepted_map>=8);
 fake::wide_map_once=true;step(true,250000);step(true,2000);
 assert(hardware.map_soft_refused==1&&hardware.map_hard_reset==0&&hardware.map_rejected==1&&hardware.map_accepted==accepted_map);
 fake::irq=0x10;fake::gpio[14]=1;e.cap_value=fake::capture();on_edge(edge_channel,&e,nullptr);step(true);
 fake::us+=100000;fake::irq=2;fake::gpio[14]=1;e.cap_value=fake::capture();on_edge(edge_channel,&e,nullptr);step(true);
 assert(fake::core_inputs==1&&hardware.invalid_capture==0&&!failed);
 cold();fake::cancel=true;force_on();worker_step(true);assert(!failed&&owner.drained()&&!owned.enabled&&fake::high_after_cancel==0);
 for(int channel=1;channel<=3;channel++){
  cold();fake::fail_enable=channel;force_on();worker_step(true);assert(failed&&!owned.enabled&&owner.drained()&&!fake::capture_timer&&!fake::timer_running&&!fake::software_enabled&&!fake::current_enabled);
 }
 cold();force_on();fake::current_fail=true;worker_step(true);worker_step(true);assert(failed&&!owned.enabled&&owner.drained()&&!fake::current_enabled&&!published.model_valid);
 assert(hardware.error==-4359&&published.current_rc==0x107&&published.current_reason==0&&published.current_prev_us&&published.current_epoch==1);
 const auto capsule=published;worker_step(true);assert(published.current_rc==capsule.current_rc&&published.current_prev_raw==capsule.current_prev_raw&&published.current_prev_us==capsule.current_prev_us&&published.current_available_us==capsule.current_available_us);
 assert(v12::lora_status().failed&&!n30_lora_cleanup_fault());
 assert(v12::lora_request(false,0));assert(!v12::lora_status().off_acknowledged); // staleack/stalledworker is not ready
 worker_step(true);assert(v12::lora_status().off_acknowledged&&v12::lora_status().failed&&!n30_lora_cleanup_fault());
 for(int boundary=1;boundary<=5;boundary++){
  cold();force_on();fake::fail_disable=boundary;assert(v12::lora_request(false,0));worker_step(true);
  assert(n30_lora_cleanup_fault()&&!owner.drained());fake::fail_disable=0;worker_step(true);
  assert(owner.drained()&&n30_lora_cleanup_fault()&&v12::lora_status().failed); // failed cleanup remains a fault
 }
 cold();fake::dma_fail=true;force_on();const auto count=fake::starts;for(unsigned i=0;i<5;i++)worker_step(true);assert(failed&&inflight&&!owner.drained()&&fake::starts==count&&fake::pending&&fake::gpio[12]==0);assert(n30_lora_cleanup_fault());
 cold();force_on();fake::us+=31000000;n30_lora_control(true,fake::us,true);assert(emergency&&fake::gpio[12]==0&&!allowed);n30_lora_view view;n30_lora_snapshot(view);assert(view.fault&&!view.active&&!view.until_us&&!view.off_acknowledged);worker_step(true);assert(failed);
}

#endif

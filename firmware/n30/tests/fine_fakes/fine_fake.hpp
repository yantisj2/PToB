// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstddef>
#include <cassert>
using esp_err_t=int;constexpr int ESP_OK=0,GPIO_MODE_INPUT=1,GPIO_MODE_INPUT_OUTPUT=3,SIG_GPIO_OUT_IDX=256,GPIO_MATRIX_CONST_ZERO_INPUT=60;
using gpio_num_t=int;using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define portENTER_CRITICAL_ISR(x) ((void)(x))
#define portEXIT_CRITICAL_ISR(x) ((void)(x))
#define PTOB_N30_FINE_FACTORY UINT64_C(0x020000003001)
#define PTOB_N30_FINE_PINS 0
#define PTOB_N30_FINE_RX_GPIO -1
#define PTOB_N30_FINE_TX_GPIO -1
#define SYNCFOUND_FLAG_IDX 230
#define PKT_TX_ON_IDX 235
constexpr int MCPWM_CAPTURE_CLK_SRC_APB=1,MCPWM_CAP_TIMER_FSM_ENABLE=1,MCPWM_CAP_CHAN_FSM_ENABLE=1;
inline uint64_t fake_now=1000000;inline uint32_t fake_raw=10000,fake_cycles=0,fake_cycle_step=32;
inline bool fake_complete=true,fake_stale=false,fake_clear_ok=true,fake_disable_ok=true;
inline uint64_t fake_start_delay=0;
inline unsigned fake_group=99,fake_callbacks=0,fake_edges=0,fake_current_irq=0;
inline int64_t esp_timer_get_time(){return int64_t(fake_now);}
inline uint32_t esp_cpu_get_cycle_count(){fake_cycles+=fake_cycle_step;return fake_cycles;}
void fake_clear(uint32_t);
struct WriteClear {void operator=(uint32_t x){fake_clear(x);}};
struct FakeDevice {
 struct Reg {uint32_t val=0;};Reg int_raw,int_ena;
 struct Config {bool cap_timer_en=false;}cap_timer_cfg;
 struct {WriteClear val;}int_clr;
 struct {bool capn_en=false;}cap_chn_cfg[3];
};
inline FakeDevice fake_device;
inline void fake_clear(uint32_t x){assert(x==(1u<<29));if(fake_clear_ok)fake_device.int_raw.val&=~x;}
struct FakeHal {FakeDevice* dev=&fake_device;};struct FakeGroup {int group_id=1;FakeHal hal;};
struct mcpwm_cap_timer_t {FakeGroup* group=nullptr;int fsm=0;};
struct mcpwm_cap_channel_t;
struct mcpwm_capture_event_data_t {uint32_t cap_value=0,cap_edge=1;};
using mcpwm_cap_timer_handle_t=mcpwm_cap_timer_t*;using mcpwm_cap_channel_handle_t=mcpwm_cap_channel_t*;
using CapCb=bool(*)(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t*,void*);
struct mcpwm_cap_channel_t {int cap_chan_id=0;mcpwm_cap_timer_handle_t cap_timer=nullptr;int fsm=0;CapCb cb=nullptr;void* context=nullptr;};
struct mcpwm_capture_timer_config_t {int group_id=0,clk_src=0;};
struct mcpwm_capture_channel_config_t {int gpio_num=0;unsigned prescale=0;struct {unsigned pos_edge=0,neg_edge=0;}flags;};
struct mcpwm_capture_event_callbacks_t {CapCb on_cap=nullptr;};
inline mcpwm_cap_channel_t fake_channels[3];inline FakeGroup fake_owner;inline mcpwm_cap_timer_t fake_timer;inline unsigned fake_allocated=0;
inline esp_err_t mcpwm_new_capture_timer(const mcpwm_capture_timer_config_t* c,mcpwm_cap_timer_handle_t* out){fake_group=c->group_id;assert(fake_group==1);fake_timer.group=&fake_owner;*out=&fake_timer;return 0;}
inline esp_err_t mcpwm_capture_timer_get_resolution(mcpwm_cap_timer_handle_t,uint32_t* out){*out=80000000;return 0;}
inline esp_err_t mcpwm_new_capture_channel(mcpwm_cap_timer_handle_t t,const mcpwm_capture_channel_config_t* c,mcpwm_cap_channel_handle_t* out){
 assert(c->gpio_num==-1&&c->prescale==1&&fake_allocated<3);const unsigned i=fake_allocated++;
 assert(c->flags.pos_edge==0&&c->flags.neg_edge==0);fake_channels[i]={int(i),t,0,nullptr,nullptr};*out=&fake_channels[i];return 0;
}
inline esp_err_t mcpwm_capture_channel_register_event_callbacks(mcpwm_cap_channel_handle_t c,const mcpwm_capture_event_callbacks_t* cb,void* context){assert(c->cap_chan_id<2&&cb->on_cap);c->cb=cb->on_cap;c->context=context;++fake_callbacks;return 0;}
inline esp_err_t mcpwm_capture_timer_enable(mcpwm_cap_timer_handle_t t){t->fsm=1;fake_now+=fake_start_delay;return 0;}
inline esp_err_t mcpwm_capture_timer_disable(mcpwm_cap_timer_handle_t t){if(!fake_disable_ok)return -1;t->fsm=0;return 0;}
inline esp_err_t mcpwm_capture_timer_start(mcpwm_cap_timer_handle_t){fake_device.cap_timer_cfg.cap_timer_en=true;return 0;}
inline esp_err_t mcpwm_capture_timer_stop(mcpwm_cap_timer_handle_t){if(!fake_disable_ok)return -1;fake_device.cap_timer_cfg.cap_timer_en=false;return 0;}
inline esp_err_t mcpwm_capture_channel_enable(mcpwm_cap_channel_handle_t c){c->fsm=1;fake_device.cap_chn_cfg[c->cap_chan_id].capn_en=true;return 0;}
inline esp_err_t mcpwm_capture_channel_disable(mcpwm_cap_channel_handle_t c){if(!fake_disable_ok)return -1;c->fsm=0;fake_device.cap_chn_cfg[c->cap_chan_id].capn_en=false;return 0;}
inline void mcpwm_ll_trigger_soft_capture(FakeDevice* dev,int i){assert(i==2&&!fake_current_irq);++fake_now;if(!fake_stale)fake_raw+=80;if(fake_complete)dev->int_raw.val|=1u<<29;}
inline uint32_t mcpwm_ll_capture_get_value(FakeDevice*,int i){assert(i==2);return fake_raw;}

inline struct {struct {struct {unsigned cap_sig=0;}captures[3]{{0},{1},{2}};}groups[2];}mcpwm_periph_signals;
inline void esp_rom_gpio_connect_in_signal(int gpio,unsigned signal,bool inverted){assert(gpio==GPIO_MATRIX_CONST_ZERO_INPUT&&signal<3&&!inverted);}
inline void mcpwm_ll_intr_enable(FakeDevice* dev,uint32_t bits,bool enable){assert(bits==(3u<<27));if(enable)dev->int_ena.val|=bits;else dev->int_ena.val&=~bits;}
inline void mcpwm_ll_capture_enable_channel(FakeDevice* dev,int i,bool enable){assert(i<2);dev->cap_chn_cfg[i].capn_en=enable;}

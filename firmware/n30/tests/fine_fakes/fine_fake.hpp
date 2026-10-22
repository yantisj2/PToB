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
#ifndef PTOB_N30_FINE_PINS
#define PTOB_N30_FINE_PINS 0
#endif
#if PTOB_N30_FINE_PINS
#define PTOB_N30_FINE_RX_GPIO 4
#define PTOB_N30_FINE_TX_GPIO 5
#else
#define PTOB_N30_FINE_RX_GPIO -1
#define PTOB_N30_FINE_TX_GPIO -1
#endif
#define SYNCFOUND_FLAG_IDX 230
#define PKT_TX_ON_IDX 235
constexpr int MCPWM_CAPTURE_CLK_SRC_APB=1,MCPWM_CAP_TIMER_FSM_ENABLE=1,MCPWM_CAP_CHAN_FSM_ENABLE=1;
inline uint64_t fake_now=1000000;inline uint32_t fake_raw=10000,fake_cycles=0,fake_cycle_step=32;
inline bool fake_complete=true,fake_stale=false,fake_clear_ok=true,fake_disable_ok=true;
inline uint64_t fake_start_delay=0;
inline int fake_enable_fail=-1;inline void (*fake_enable_inject)(unsigned)=nullptr;
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
 assert(c->flags.pos_edge==unsigned(i<2&&PTOB_N30_FINE_PINS)&&c->flags.neg_edge==unsigned(i<2&&PTOB_N30_FINE_PINS));fake_channels[i]={int(i),t,0,nullptr,nullptr};*out=&fake_channels[i];return 0;
}
inline esp_err_t mcpwm_capture_channel_register_event_callbacks(mcpwm_cap_channel_handle_t c,const mcpwm_capture_event_callbacks_t* cb,void* context){assert(c->cap_chan_id<2&&cb->on_cap);c->cb=cb->on_cap;c->context=context;++fake_callbacks;return 0;}
inline esp_err_t mcpwm_capture_timer_enable(mcpwm_cap_timer_handle_t t){t->fsm=1;fake_now+=fake_start_delay;return 0;}
inline esp_err_t mcpwm_capture_timer_disable(mcpwm_cap_timer_handle_t t){if(!fake_disable_ok)return -1;t->fsm=0;return 0;}
inline esp_err_t mcpwm_capture_timer_start(mcpwm_cap_timer_handle_t){fake_device.cap_timer_cfg.cap_timer_en=true;return 0;}
inline esp_err_t mcpwm_capture_timer_stop(mcpwm_cap_timer_handle_t){if(!fake_disable_ok)return -1;fake_device.cap_timer_cfg.cap_timer_en=false;return 0;}
inline esp_err_t mcpwm_capture_channel_enable(mcpwm_cap_channel_handle_t c){if(c->cap_chan_id==fake_enable_fail)return -1;c->fsm=1;fake_device.cap_chn_cfg[c->cap_chan_id].capn_en=true;if(c->cb)fake_device.int_ena.val|=1u<<(27+c->cap_chan_id);if(fake_enable_inject)fake_enable_inject(unsigned(c->cap_chan_id));return 0;}
inline esp_err_t mcpwm_capture_channel_disable(mcpwm_cap_channel_handle_t c){if(!fake_disable_ok)return -1;c->fsm=0;fake_device.cap_chn_cfg[c->cap_chan_id].capn_en=false;fake_device.int_ena.val&=~(1u<<(27+c->cap_chan_id));return 0;}
inline void mcpwm_ll_trigger_soft_capture(FakeDevice* dev,int i){assert(i==2&&!fake_current_irq);++fake_now;if(!fake_stale)fake_raw+=80;if(fake_complete)dev->int_raw.val|=1u<<29;}
inline uint32_t mcpwm_ll_capture_get_value(FakeDevice*,int i){assert(i==2);return fake_raw;}

struct FakeSignals {struct {struct {unsigned cap_sig=0;}captures[3];}groups[2];};
inline FakeSignals mcpwm_periph_signals=[](){FakeSignals v{};for(unsigned i=0;i<3;++i){v.groups[0].captures[i].cap_sig=166+i;v.groups[1].captures[i].cap_sig=175+i;}return v;}();
constexpr unsigned PIN_FUNC_GPIO=1;
struct FakeGpio {struct Reg {uint32_t val=0;};uint32_t enable=0;Reg func_in_sel_cfg[256],func_out_sel_cfg[49];bool oe[49]{},ie[49]{};};
inline FakeGpio GPIO;
inline int fake_gpio_failure=0,fake_gpio_failure_pin=-1;inline unsigned fake_gpio_calls=0,fake_output_disables[49]{};
inline bool gpio_failure(int operation,int pin){return fake_gpio_failure==operation&&fake_gpio_failure_pin==pin;}
struct gpio_io_config_t {uint32_t sig_out=0,fun_sel=PIN_FUNC_GPIO;bool ie=false,oe=false,oe_ctrl_by_periph=false,oe_inv=false;};
inline int gpio_set_direction(gpio_num_t pin,int mode){assert(pin==4||pin==5);++fake_gpio_calls;if(gpio_failure(1,pin))return -1;GPIO.ie[pin]=true;GPIO.oe[pin]=mode==GPIO_MODE_INPUT_OUTPUT;if(GPIO.oe[pin])GPIO.enable|=1u<<pin;else GPIO.enable&=~(1u<<pin);GPIO.func_out_sel_cfg[pin].val=SIG_GPIO_OUT_IDX|(1u<<10);return 0;}
inline int gpio_pullup_dis(gpio_num_t pin){++fake_gpio_calls;return gpio_failure(2,pin)?-1:0;}
inline int gpio_pulldown_dis(gpio_num_t pin){++fake_gpio_calls;return gpio_failure(3,pin)?-1:0;}
inline int gpio_reset_pin(gpio_num_t pin){++fake_gpio_calls;if(gpio_failure(4,pin))return -1;GPIO.oe[pin]=GPIO.ie[pin]=false;GPIO.enable&=~(1u<<pin);GPIO.func_out_sel_cfg[pin].val=SIG_GPIO_OUT_IDX|(1u<<10);return 0;}
inline int gpio_get_io_config(gpio_num_t pin,gpio_io_config_t* io){++fake_gpio_calls;if(gpio_failure(5,pin))return -1;*io={GPIO.func_out_sel_cfg[pin].val&511,PIN_FUNC_GPIO,GPIO.ie[pin],GPIO.oe[pin],!(GPIO.func_out_sel_cfg[pin].val&(1u<<10)),bool(GPIO.func_out_sel_cfg[pin].val&(1u<<11))};if(gpio_failure(6,pin))io->sig_out=999;return 0;}
inline void esp_rom_gpio_connect_in_signal(int gpio,unsigned signal,bool inverted){assert(signal>=175&&signal<=177&&!inverted&&(gpio==GPIO_MATRIX_CONST_ZERO_INPUT||(PTOB_N30_FINE_PINS&&(gpio==4||gpio==5))));GPIO.func_in_sel_cfg[signal].val=(1u<<7)|unsigned(gpio);if(gpio_failure(7,gpio))GPIO.func_in_sel_cfg[signal].val^=1;}
inline void esp_rom_gpio_connect_out_signal(int pin,int signal,bool inverted,bool oe_inverted){assert((pin==4||pin==5)&&!inverted&&!oe_inverted);if(signal==230||signal==235)assert(GPIO.ie[pin]&&GPIO.oe[pin]);GPIO.func_out_sel_cfg[pin].val=unsigned(signal);if(gpio_failure(8,pin))GPIO.func_out_sel_cfg[pin].val^=1;}
inline void gpio_ll_set_output_enable_ctrl(FakeGpio* dev,int pin,bool ctrl_by_periph,bool oen_inv){assert(dev==&GPIO&&(pin==4||pin==5));dev->func_out_sel_cfg[pin].val=(dev->func_out_sel_cfg[pin].val&~(3u<<10))|(uint32_t(!ctrl_by_periph)<<10)|(uint32_t(oen_inv)<<11);}
inline void gpio_ll_output_disable(FakeGpio* dev,int pin){assert(dev==&GPIO&&(pin==4||pin==5));dev->oe[pin]=false;dev->enable&=~(1u<<pin);++fake_output_disables[pin];}
inline void mcpwm_ll_intr_enable(FakeDevice* dev,uint32_t bits,bool enable){assert(bits==(3u<<27));if(enable)dev->int_ena.val|=bits;else dev->int_ena.val&=~bits;}
inline void mcpwm_ll_capture_enable_channel(FakeDevice* dev,int i,bool enable){assert(i<2);dev->cap_chn_cfg[i].capn_en=enable;}

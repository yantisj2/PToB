// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "lora_counter.h"
#include "gptimer_priv.h"
#include "mcpwm_private.h"
#include <assert.h>
#include <stdio.h>
static bool internal=true;static unsigned depth,latches,reads,catches;static int catch_error;
static struct fake_timer_hw hw={.tick=7000000};
static struct gptimer_t timer={.hal={&hw,1},.resolution_hz=20000000,.clk_src=GPTIMER_CLK_SRC_XTAL,.direction=GPTIMER_COUNT_UP,.fsm=GPTIMER_FSM_RUN};
static mcpwm_dev_t cap_hw={.cap_timer_cfg={true},.raw=0xfedc1234};
static mcpwm_group_t cap_group={.group_id=0,.hal={&cap_hw}};
static mcpwm_cap_timer_t cap_timer={.group=&cap_group,.resolution_hz=80000000,.fsm=MCPWM_CAP_TIMER_FSM_ENABLE};
static struct mcpwm_cap_channel_t cap_channel={.cap_chan_id=2,.cap_timer=&cap_timer,.fsm=MCPWM_CAP_CHAN_FSM_ENABLE};
uint32_t fake_cpu_cycles;
int64_t esp_timer_get_time(void){assert(depth==1);return 123456789;}
void fake_enter(int* lock){assert((lock==&timer.spinlock||lock==&cap_timer.spinlock)&&depth==0);++depth;}
void fake_exit(int* lock){assert((lock==&timer.spinlock||lock==&cap_timer.spinlock)&&depth==1);--depth;}
bool esp_ptr_internal(const void* p){assert(p==&timer||p==&cap_timer||p==&cap_channel);return internal;}
void timer_ll_trigger_soft_capture(struct fake_timer_hw* p,unsigned id){assert(p==&hw&&id==1&&depth==1);++latches;hw.captured=hw.tick;}
uint64_t timer_ll_get_counter_value(struct fake_timer_hw* p,unsigned id){assert(p==&hw&&id==1&&depth==1);++reads;return hw.captured;}
esp_err_t mcpwm_capture_channel_trigger_soft_catch(mcpwm_cap_channel_handle_t channel){assert(channel==(void*)2&&depth==1&&reads==latches);++catches;hw.tick+=8;return catch_error;}
int main(void){
 uint64_t before=0,after=0;
 assert(!n30_lora_counter_bracket(&timer,(void*)2,&before,&after));
 assert(before==7000000&&after==7000008&&latches==2&&reads==2&&catches==1&&depth==0);
 for(unsigned bad=0;bad<5;bad++){
  timer.resolution_hz=bad==0?80000000:20000000;timer.clk_src=bad==1?9:GPTIMER_CLK_SRC_XTAL;
  timer.direction=bad==2?0:GPTIMER_COUNT_UP;atomic_store(&timer.fsm,bad==3?0:GPTIMER_FSM_RUN);internal=bad!=4;
  assert(n30_lora_counter_bracket(&timer,(void*)2,&before,&after)==ESP_ERR_INVALID_STATE);
  assert(!before&&!after&&latches==2&&catches==1&&depth==0);
 }
 internal=true;timer.resolution_hz=20000000;timer.clk_src=GPTIMER_CLK_SRC_XTAL;timer.direction=GPTIMER_COUNT_UP;atomic_store(&timer.fsm,GPTIMER_FSM_RUN);
 assert(n30_lora_counter_bracket(0,(void*)2,&before,&after)==ESP_ERR_INVALID_ARG);
 assert(n30_lora_counter_bracket(&timer,0,&before,&after)==ESP_ERR_INVALID_ARG);
 assert(n30_lora_counter_bracket(&timer,(void*)2,0,&after)==ESP_ERR_INVALID_ARG);
 assert(n30_lora_counter_bracket(&timer,(void*)2,&before,0)==ESP_ERR_INVALID_ARG);
 catch_error=-7;assert(n30_lora_counter_bracket(&timer,(void*)2,&before,&after)==-7&&depth==0&&latches==4&&catches==2);
 uint32_t raw=0;uint64_t sampled=0;
 cap_hw.int_raw.val=UINT32_C(1)<<27;cap_hw.int_ena.val=UINT32_C(1)<<27;
 assert(!n30_lora_counter_current(&cap_channel,&raw,&sampled)&&cap_hw.int_raw.val==(UINT32_C(1)<<27)&&cap_hw.int_ena.val==(UINT32_C(1)<<27));
 cap_hw.int_raw.val=cap_hw.int_ena.val=0;
 assert(!n30_lora_counter_current(&cap_channel,&raw,&sampled)&&raw==cap_hw.raw&&sampled==123456789&&depth==0&&cap_hw.int_raw.val==0);
 for(unsigned bad=0;bad<8;bad++){
  cap_group.group_id=bad==0?1:0;cap_timer.resolution_hz=bad==1?20000000:80000000;
  cap_timer.fsm=bad==2?0:MCPWM_CAP_TIMER_FSM_ENABLE;cap_channel.cap_chan_id=bad==3?1:2;
  cap_channel.fsm=bad==4?0:MCPWM_CAP_CHAN_FSM_ENABLE;cap_channel.on_cap=bad==5?(void*)1:0;
  cap_channel.intr=bad==6?(void*)1:0;cap_hw.int_ena.val=bad==7?UINT32_C(1)<<29:0;
  raw=123;assert(n30_lora_counter_current(&cap_channel,&raw,&sampled)==ESP_ERR_INVALID_STATE&&raw==0&&depth==0);
 }
 cap_hw.int_ena.val=0;cap_hw.missing=true;assert(n30_lora_counter_current(&cap_channel,&raw,&sampled)==ESP_ERR_TIMEOUT&&depth==0);
 cap_hw.missing=false;cap_hw.stuck=true;cap_hw.int_raw.val=UINT32_C(1)<<29;
 assert(n30_lora_counter_current(&cap_channel,&raw,&sampled)==ESP_ERR_TIMEOUT&&depth==0);
 cap_hw.stuck=false;assert(!n30_lora_counter_current(&cap_channel,&raw,&sampled)&&raw==cap_hw.raw&&depth==0);
 cap_hw.cap_timer_cfg.cap_timer_en=false;assert(n30_lora_counter_current(&cap_channel,&raw,&sampled)==ESP_ERR_INVALID_STATE&&depth==0);cap_hw.cap_timer_cfg.cap_timer_en=true;
 assert(n30_lora_counter_current(0,&raw,&sampled)==ESP_ERR_INVALID_ARG&&n30_lora_counter_current(&cap_channel,0,&sampled)==ESP_ERR_INVALID_ARG);
 assert(n30_lora_counter_current(&cap_channel,&raw,0)==ESP_ERR_INVALID_ARG);
 puts("PASS actual N30 locked XTAL20/MCPWM bracket, physical latch/catch order, one lock, source/resolution/direction/lifecycle/null/API refusal; synthetic counters only");
}

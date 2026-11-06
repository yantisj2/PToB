// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Pinned SDK ABI, same independent XTAL GPTimer coordinate. No BLE estimator.
#include "lora_counter.h"
#include "gptimer_priv.h"
#include "esp_memory_utils.h"
#include "esp_attr.h"
#include "esp_cpu.h"
#include "esp_timer.h"
#include "mcpwm_private.h"
static inline __attribute__((always_inline)) uint64_t latch(gptimer_handle_t timer){
 timer_ll_trigger_soft_capture(timer->hal.dev,timer->hal.timer_id);
 return timer_ll_get_counter_value(timer->hal.dev,timer->hal.timer_id);
}
esp_err_t IRAM_ATTR n30_lora_counter_bracket(gptimer_handle_t timer,mcpwm_cap_channel_handle_t channel,uint64_t* before,uint64_t* after){
 if(!timer||!channel||!before||!after)return ESP_ERR_INVALID_ARG;
 *before=*after=0;
 if(!esp_ptr_internal(timer)||timer->resolution_hz!=20000000||timer->clk_src!=GPTIMER_CLK_SRC_XTAL||
    timer->direction!=GPTIMER_COUNT_UP||atomic_load(&timer->fsm)!=GPTIMER_FSM_RUN)return ESP_ERR_INVALID_STATE;
 // Task context, as before. The public MCPWM call is not claimed IRAM-safe.
 // One original timer lock encloses both hardware latches and the real catch;
 // driver/HAL call overhead no longer shifts a single midpoint between reads.
 portENTER_CRITICAL_SAFE(&timer->spinlock);
 *before=latch(timer);
 const esp_err_t rc=mcpwm_capture_channel_trigger_soft_catch(channel);
 *after=latch(timer);
 portEXIT_CRITICAL_SAFE(&timer->spinlock);
 return rc;
}
esp_err_t IRAM_ATTR n30_lora_counter_current(mcpwm_cap_channel_handle_t channel,uint32_t* raw,uint64_t* sampled_us){
 if(!channel||!raw||!sampled_us)return ESP_ERR_INVALID_ARG;
 *raw=0;*sampled_us=0;
 if(!esp_ptr_internal(channel)||!channel->cap_timer||!esp_ptr_internal(channel->cap_timer))return ESP_ERR_INVALID_STATE;
 mcpwm_cap_timer_t* timer=channel->cap_timer;
 if(!timer->group||timer->group->group_id!=0||timer->resolution_hz!=80000000||
    timer->fsm!=MCPWM_CAP_TIMER_FSM_ENABLE||channel->cap_chan_id!=2||
    channel->fsm!=MCPWM_CAP_CHAN_FSM_ENABLE||channel->on_cap||channel->intr)return ESP_ERR_INVALID_STATE;
 mcpwm_dev_t* dev=timer->group->hal.dev;const uint32_t mask=UINT32_C(1)<<29;
 portENTER_CRITICAL_SAFE(&timer->spinlock);
 esp_err_t rc=ESP_ERR_TIMEOUT;const uint32_t begin=esp_cpu_get_cycle_count();
 if(dev->cap_timer_cfg.cap_timer_en&&!(dev->int_ena.val&mask)){
  mcpwm_ll_intr_clear_status(dev,mask);
  unsigned clear=0;for(;clear<128;++clear)if(!(dev->int_raw.val&mask))break;
  if(clear<128){
   mcpwm_ll_trigger_soft_capture(dev,2);
   unsigned poll=0;for(;poll<128;++poll)if(dev->int_raw.val&mask)break;
   if(poll<128){
    const uint32_t value=mcpwm_ll_capture_get_value(dev,2);
    // Pair the coarse health witness before releasing the same lock. A caller
    // delayed after return must not compare old hardware time to a new now().
    // This timestamp never supplies native phase or frequency.
    const uint64_t sampled=(uint64_t)esp_timer_get_time();
    if((uint32_t)(esp_cpu_get_cycle_count()-begin)<=4096){*raw=value;*sampled_us=sampled;rc=ESP_OK;}
   }
  }
  mcpwm_ll_intr_clear_status(dev,mask);
 }else rc=ESP_ERR_INVALID_STATE;
 portEXIT_CRITICAL_SAFE(&timer->spinlock);return rc;
}

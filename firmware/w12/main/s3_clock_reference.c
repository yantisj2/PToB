#include "s3_clock_reference.h"
#ifdef V12_R2_RX_SHADOW
#include "gptimer_priv.h"
#include "esp_memory_utils.h"
extern w12_ble_time_t r_rwip_time_get(void);
_Static_assert(sizeof(w12_ble_time_t)==8,"Pinned ROM return ABI changed");
static inline __attribute__((always_inline)) uint64_t latch(gptimer_handle_t timer){
    portENTER_CRITICAL_SAFE(&timer->spinlock);
    timer_ll_trigger_soft_capture(timer->hal.dev,timer->hal.timer_id);
    const uint64_t value=timer_ll_get_counter_value(timer->hal.dev,timer->hal.timer_id);
    portEXIT_CRITICAL_SAFE(&timer->spinlock);
    return value;
}
#if defined(PTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC) && PTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC
#if !W12_GNSS_VERIFIED_INPUTS || W12_COPY_BOARD_PROFILE != 12
#error "Fast GNSS mapping requires the explicit verified W12 L76K profile"
#endif
// Task context only: the public MCPWM soft-catch implementation is not claimed
// to be IRAM-safe. Its complete latency remains inside these sampled bounds.
esp_err_t w12_s3_gnss_map_bracket(gptimer_handle_t timer,mcpwm_cap_channel_handle_t channel,uint64_t* before,uint64_t* after){
    if(!timer||!channel||!before||!after)return ESP_ERR_INVALID_ARG;
    *before=0;*after=0;
    if(xPortGetCoreID()!=CONFIG_BT_CTRL_PINNED_TO_CORE||!esp_ptr_internal(timer)||
       timer->resolution_hz!=20000000||timer->clk_src!=GPTIMER_CLK_SRC_XTAL||
       timer->direction!=GPTIMER_COUNT_UP||atomic_load(&timer->fsm)!=GPTIMER_FSM_RUN)return ESP_ERR_INVALID_STATE;
    const UBaseType_t saved=portSET_INTERRUPT_MASK_FROM_ISR();
    *before=latch(timer);
    const esp_err_t result=mcpwm_capture_channel_trigger_soft_catch(channel);
    *after=latch(timer);
    portCLEAR_INTERRUPT_MASK_FROM_ISR(saved);
    return result;
}
#endif
esp_err_t IRAM_ATTR w12_s3_ble_time_bracket(gptimer_handle_t timer,uint64_t* before,uint64_t* after,w12_ble_time_t* time){
    if(!timer||!before||!after||!time)return ESP_ERR_INVALID_ARG;
    if(xPortGetCoreID()!=CONFIG_BT_CTRL_PINNED_TO_CORE||!esp_ptr_internal(timer)||
       timer->resolution_hz!=20000000||timer->clk_src!=GPTIMER_CLK_SRC_XTAL||
       timer->direction!=GPTIMER_COUNT_UP||atomic_load(&timer->fsm)!=GPTIMER_FSM_RUN)return ESP_ERR_INVALID_STATE;
    // Preserve the local IRQ level. Release each driver lock before ROM,
    // which acquires the controller global interrupt mux in this SDK.
    const UBaseType_t saved=portSET_INTERRUPT_MASK_FROM_ISR();
    *before=latch(timer);*time=r_rwip_time_get();*after=latch(timer);
    portCLEAR_INTERRUPT_MASK_FROM_ISR(saved);
    return ESP_OK;
}
#else
#if defined(PTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC) && PTOB_W12_GNSS_FAST_MAP_DIAGNOSTIC
#error "Fast GNSS mapping requires the existing pinned RX-shadow driver ABI guard"
#endif
esp_err_t w12_s3_ble_time_bracket(gptimer_handle_t timer,uint64_t* before,uint64_t* after,w12_ble_time_t* time){
    (void)timer;(void)before;(void)after;(void)time;return ESP_ERR_NOT_SUPPORTED;
}
#endif

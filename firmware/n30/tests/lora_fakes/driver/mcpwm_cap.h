// Synthetic host API boundary; no hardware access. MIT, Jonathan Yantis.
#pragma once
#include "gpio.h"
using mcpwm_cap_timer_handle_t=void*;using mcpwm_cap_channel_handle_t=void*;
constexpr int MCPWM_CAPTURE_CLK_SRC_APB=1;
struct mcpwm_capture_event_data_t{uint32_t cap_value;};
using CaptureCallback=bool(*)(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t*,void*);
struct mcpwm_capture_event_callbacks_t{CaptureCallback on_cap=nullptr;};
struct mcpwm_capture_timer_config_t{int group_id=0,clk_src=0;};
struct mcpwm_capture_channel_config_t{int gpio_num=0,prescale=0;struct{bool pos_edge=false;}flags;};
esp_err_t mcpwm_new_capture_timer(const mcpwm_capture_timer_config_t*,mcpwm_cap_timer_handle_t*);
esp_err_t mcpwm_capture_timer_get_resolution(mcpwm_cap_timer_handle_t,uint32_t*);
esp_err_t mcpwm_new_capture_channel(mcpwm_cap_timer_handle_t,const mcpwm_capture_channel_config_t*,mcpwm_cap_channel_handle_t*);
esp_err_t mcpwm_capture_channel_register_event_callbacks(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_callbacks_t*,void*);
esp_err_t mcpwm_capture_timer_enable(mcpwm_cap_timer_handle_t);
esp_err_t mcpwm_capture_timer_start(mcpwm_cap_timer_handle_t);
esp_err_t mcpwm_capture_channel_enable(mcpwm_cap_channel_handle_t);
esp_err_t mcpwm_capture_channel_disable(mcpwm_cap_channel_handle_t);
esp_err_t mcpwm_capture_timer_stop(mcpwm_cap_timer_handle_t);
esp_err_t mcpwm_capture_timer_disable(mcpwm_cap_timer_handle_t);
esp_err_t mcpwm_capture_channel_trigger_soft_catch(mcpwm_cap_channel_handle_t);

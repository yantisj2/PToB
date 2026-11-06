// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "gptimer_priv.h"
typedef struct {struct{uint32_t val;}int_raw,int_ena;struct{bool cap_timer_en;}cap_timer_cfg;uint32_t raw,captured;bool stuck,missing;}mcpwm_dev_t;
typedef struct {int group_id;struct{mcpwm_dev_t* dev;}hal;}mcpwm_group_t;
typedef struct mcpwm_cap_timer_t {mcpwm_group_t* group;int spinlock;uint32_t resolution_hz;int fsm;}mcpwm_cap_timer_t;
struct mcpwm_cap_channel_t {int cap_chan_id;mcpwm_cap_timer_t* cap_timer;int fsm;void* on_cap;void* intr;};
enum{MCPWM_CAP_TIMER_FSM_ENABLE=1,MCPWM_CAP_CHAN_FSM_ENABLE=1};
static inline void mcpwm_ll_intr_clear_status(mcpwm_dev_t* d,uint32_t mask){if(!d->stuck)d->int_raw.val&=~mask;}
static inline void mcpwm_ll_trigger_soft_capture(mcpwm_dev_t* d,int ch){d->captured=d->raw;if(!d->missing)d->int_raw.val|=UINT32_C(1)<<(27+ch);}
static inline uint32_t mcpwm_ll_capture_get_value(mcpwm_dev_t* d,int ch){(void)ch;return d->captured;}

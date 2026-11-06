// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdatomic.h>
typedef int esp_err_t;
enum {ESP_OK=0,ESP_ERR_INVALID_ARG=-1,ESP_ERR_INVALID_STATE=-2,ESP_ERR_TIMEOUT=-3,GPTIMER_CLK_SRC_XTAL=3,GPTIMER_COUNT_UP=1,GPTIMER_FSM_RUN=4};
struct fake_timer_hw {uint64_t tick,captured;};
struct gptimer_t {struct {struct fake_timer_hw* dev;unsigned timer_id;}hal;unsigned resolution_hz,clk_src,direction;atomic_int fsm;int spinlock;};
typedef struct gptimer_t* gptimer_handle_t;
typedef struct mcpwm_cap_channel_t* mcpwm_cap_channel_handle_t;
void fake_enter(int*);void fake_exit(int*);
#define portENTER_CRITICAL_SAFE(p) fake_enter(p)
#define portEXIT_CRITICAL_SAFE(p) fake_exit(p)
void timer_ll_trigger_soft_capture(struct fake_timer_hw*,unsigned);
uint64_t timer_ll_get_counter_value(struct fake_timer_hw*,unsigned);

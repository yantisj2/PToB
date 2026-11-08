// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "counter.h"
#include "ref_timebase.h"
#include "esp_attr.h"

#if REF_CLOCK_HZ == 80000000
#include "driver/mcpwm_cap.h"
#include "esp_timer.h"
#include "mcpwm_private.h"
#include "radio_math.h"

static mcpwm_cap_timer_handle_t timer;
static mcpwm_cap_channel_handle_t current;
static mcpwm_dev_t *registers;
static int channel;
static uint32_t event_mask;
static portMUX_TYPE counter_lock = portMUX_INITIALIZER_UNLOCKED;
static RefCaptureCounter extension;
static bool enabled, running, channel_enabled;

bool ref_counter_port_begin(void)
{
    // One dedicated capture timer. The CURRENT channel has no GPIO and no ISR.
    const mcpwm_capture_timer_config_t config = {.group_id = 1, .clk_src = MCPWM_CAPTURE_CLK_SRC_APB};
    const mcpwm_capture_channel_config_t latch = {.gpio_num = -1, .prescale = 1};
    uint32_t rate;
    if (mcpwm_new_capture_timer(&config, &timer) != ESP_OK ||
        mcpwm_capture_timer_get_resolution(timer, &rate) != ESP_OK || rate != REF_CLOCK_HZ ||
        mcpwm_new_capture_channel(timer, &latch, &current) != ESP_OK) return false;
    // The builder pins the SDK/private header. Bind its live driver objects too.
    if (!timer->group || timer->group->group_id != 1 || !timer->group->hal.dev ||
        current->cap_timer != timer || current->gpio_num != -1 || current->cap_chan_id < 0 ||
        current->cap_chan_id >= SOC_MCPWM_CAPTURE_CHANNELS_PER_TIMER) return false;
    registers = timer->group->hal.dev;
    channel = current->cap_chan_id;
    event_mask = MCPWM_LL_EVENT_CAPTURE(channel);
    if (mcpwm_capture_timer_enable(timer) != ESP_OK) return false;
    enabled = true;
    if (mcpwm_capture_timer_start(timer) != ESP_OK) return false;
    running = true;
    if (mcpwm_capture_channel_enable(current) != ESP_OK) return false;
    channel_enabled = true;
    uint64_t first, second;
    return ref_counter_port_now(&first) && ref_counter_port_now(&second) && second > first;
}

static bool IRAM_ATTR __attribute__((noinline)) latch_current(uint32_t *raw)
{
    if (!channel_enabled || !registers->cap_timer_cfg.cap_timer_en ||
        (registers->int_ena.val & event_mask)) return false;
    registers->int_clr.val = event_mask;
    for (unsigned poll = 0; poll < 128; ++poll) {
        if (!(registers->int_raw.val & event_mask)) {
            mcpwm_ll_trigger_soft_capture(registers, channel);
            for (unsigned wait = 0; wait < 128; ++wait) {
                if (registers->int_raw.val & event_mask) {
                    *raw = mcpwm_ll_capture_get_value(registers, channel);
                    return true;
                }
            }
            return false;
        }
    }
    return false;
}

bool IRAM_ATTR ref_counter_port_now(uint64_t *tick)
{
    uint32_t raw;
    portENTER_CRITICAL_SAFE(&counter_lock);
    const bool ok = !extension.failed && latch_current(&raw) &&
                    ref_capture32_advance(&extension, raw, (uint64_t)esp_timer_get_time(), tick);
    if (!ok) extension.failed = true;
    portEXIT_CRITICAL_SAFE(&counter_lock);
    return ok;
}

bool ref_counter_port_stop(void)
{
    if (channel_enabled && mcpwm_capture_channel_disable(current) != ESP_OK) return false;
    channel_enabled = false;
    if (running && mcpwm_capture_timer_stop(timer) != ESP_OK) return false;
    running = false;
    if (enabled && mcpwm_capture_timer_disable(timer) != ESP_OK) return false;
    enabled = false;
    if (current && mcpwm_del_capture_channel(current) != ESP_OK) return false;
    current = NULL;
    if (timer && mcpwm_del_capture_timer(timer) != ESP_OK) return false;
    timer = NULL;
    return true;
}
#else
#include "driver/gptimer.h"

static gptimer_handle_t timer;
static bool enabled, running;

bool ref_counter_port_begin(void)
{
    const gptimer_config_t config = {.clk_src = GPTIMER_CLK_SRC_XTAL,
        .direction = GPTIMER_COUNT_UP, .resolution_hz = REF_CLOCK_HZ};
    if (gptimer_new_timer(&config, &timer) != ESP_OK || gptimer_enable(timer) != ESP_OK) return false;
    enabled = true;
    if (gptimer_start(timer) != ESP_OK) return false;
    running = true;
    return true;
}

bool IRAM_ATTR ref_counter_port_now(uint64_t *tick)
{
    return running && gptimer_get_raw_count(timer, tick) == ESP_OK;
}

bool ref_counter_port_stop(void)
{
    if (running && gptimer_stop(timer) != ESP_OK) return false;
    running = false;
    if (enabled && gptimer_disable(timer) != ESP_OK) return false;
    enabled = false;
    if (timer && gptimer_del_timer(timer) != ESP_OK) return false;
    timer = NULL;
    return true;
}
#endif

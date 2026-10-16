#pragma once
#include "driver/gptimer.h"
#include "../button_input.hpp"
struct DisplayState;
namespace sticks3 {
struct View;
struct TimingSnapshot;
void present_timing(const TimingSnapshot& timing);
void present_view(const View& view,uint64_t tick);
void present(const DisplayState& state,uint8_t quarter,uint64_t tick);
bool buttons_start(gptimer_handle_t clock);
ButtonInput::Action next_action();
// Optional input-only MCPWM capture diagnostics. Disabled build does not touch GPIO6.
bool pps_start(gptimer_handle_t clock);
void pps_poll(uint64_t tick);
void pps_augment(View& view,uint64_t tick);
void status(uint64_t tick);
void display_status();
// Board reset/download only; no radio role or source permission is changed.
bool power_command(bool download);
}

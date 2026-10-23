// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Compile the actual exported counter readers against independent fake clocks.
#include <cassert>
#include <cstdint>
#define IRAM_ATTR
constexpr int ESP_OK=0;
void* clock_timer=reinterpret_cast<void*>(1);
bool fine_selected=true,fine_ok=true,timer_ok=true;
unsigned fine_reads=0,timer_reads=0;
bool n30_fine_backend_selected(){return fine_selected;}
bool n30_fine_backend_current(uint64_t* out){++fine_reads;if(!out||!fine_ok)return false;*out=9000000;return true;}
int gptimer_get_raw_count(void* timer,uint64_t* out){assert(timer==clock_timer);++timer_reads;if(!timer_ok)return -1;*out=7000000;return ESP_OK;}
#include "actual_lora_clock_reference.inc"
int main(){
 uint64_t tick=0;
 assert(n30_lora_reference_read(&tick)&&tick==7000000&&timer_reads==1&&!fine_reads);
 assert(clock_reference_read(&tick)&&tick==9000000&&fine_reads==1&&timer_reads==1);
 fine_ok=false;assert(!clock_reference_read(&tick)&&timer_reads==1);
 assert(n30_lora_reference_read(&tick)&&tick==7000000&&fine_reads==2);
 timer_ok=false;assert(!n30_lora_reference_read(&tick)&&fine_reads==2);
 assert(!n30_lora_reference_read(nullptr));clock_timer=nullptr;assert(!n30_lora_reference_read(&tick));
 clock_timer=reinterpret_cast<void*>(1);timer_ok=true;fine_selected=false;
 assert(clock_reference_read(&tick)&&tick==7000000&&fine_reads==2);
}

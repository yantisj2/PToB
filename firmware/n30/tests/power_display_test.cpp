// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include <cassert>
#include <cstdio>
using portMUX_TYPE=int;
void portENTER_CRITICAL(int*){}void portEXIT_CRITICAL(int*){}
int mux=0,ui_task=1,frame_timer=2;bool minimal_wait=true,power_action=false,power_stopping=false,power_stopped=false;
struct {bool blank=false;} stats;
unsigned commands=0,timer_stops=0,notifies=0,suspends=0;bool command_ok=true;
bool command(unsigned value){assert(value==0xae);++commands;return command_ok;}
void esp_timer_stop(int timer){assert(timer==2);++timer_stops;}
void xTaskNotifyGive(int task){assert(task==1);++notifies;}
struct Suspended{};
void vTaskSuspend(void*){++suspends;throw Suspended{};}
#include "actual_display_power_api.inc"
void worker_stop(){const bool stopping=power_stopping;for(;;){
#include "actual_display_power_worker.inc"
 break;
}}
int main(){
 assert(!n30_display_take_poweroff()&&!n30_display_power_ready());worker_stop();assert(!commands);
 power_action=true;assert(n30_display_take_poweroff()&&!n30_display_take_poweroff());
 n30_display_request_poweroff();assert(power_stopping&&notifies==1&&!n30_display_power_ready());
 try{worker_stop();assert(false);}catch(Suspended&){}
 assert(commands==1&&timer_stops==1&&suspends==1&&stats.blank&&!minimal_wait&&n30_display_power_ready());
 // An OLED command failure still releases this worker's ownership; the board
 // off path removes its rail and never waits on another display transaction.
 command_ok=false;try{worker_stop();assert(false);}catch(Suspended&){}assert(suspends==2);
 std::puts("PASS actual N30 display stop: queued main request, OLED off, timer stopped and quiescent worker acknowledgement");
}

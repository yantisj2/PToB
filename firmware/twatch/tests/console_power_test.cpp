// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// The actual watch output worker with deterministic USB/cable/task boundaries.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <functional>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
uint64_t fake_now=1000;
std::string wire;bool reader=true;unsigned waits=0,stop_after=0,writes=0,notifications=0;
unsigned last_wait=0;std::function<void()> boundary;struct Stop{};
int usb_serial_jtag_write_bytes(const void* p,size_t n,unsigned wait){assert(wait==0);++writes;if(!reader)return 0;wire.append(static_cast<const char*>(p),n);return int(n);}
int usb_serial_jtag_wait_tx_done(unsigned n){assert(!n);return reader?0:-1;}
void vTaskDelay(unsigned){assert(false&&"Console must block on notifications");}
void xTaskNotifyGive(TaskHandle_t){++notifications;}
unsigned ulTaskNotifyTake(int,unsigned wait){last_wait=wait;++waits;if(boundary)boundary();fake_now+=wait==portMAX_DELAY?1000:uint64_t(wait)*1000;if(waits>=stop_after)throw Stop{};return 0;}
#include "../main/bounded_log.cpp"
void run(unsigned n){waits=0;stop_after=n;try{v12::output(nullptr);}catch(const Stop&){}assert(waits==n);}
void reset(){using namespace v12;wire.clear();reader=true;fake_now=1000;writes=notifications=0;boundary={};stats={};frames={};lines={};current_frame={};current_line={};usb_online=false;usb_generation=0;usb_since=0;tx_mode=false;tx_filtered=0;capture_generation=live_next_us=0;log_task=reinterpret_cast<void*>(1);}
static v12::ConsoleFrame frame;
void make(){v12::frame_begin(frame,1,2,fake_now,"02:00:00:00:00:01",3,"source","build");for(unsigned i=0;i<10;i++)assert(frame.format("ROW_%u value=%080u\n",i,0u));}
int main(){using namespace v12;
 reset();run(1);assert(last_wait==portMAX_DELAY&&!writes&&!notifications);
 const auto invalid=reinterpret_cast<const char*>(uintptr_t(1));assert(logf(invalid)==0&&log_live(invalid)==0);assert(!capture_begin(1,1000));make();assert(!frame_submit(frame));
 log_usb_power(true);assert(notifications==1);make();assert(frame_submit(frame));run(30);
 assert(wire.find("W12_V12_BEGIN")!=std::string::npos&&wire.find("W12_V12_END")!=std::string::npos&&last_wait==portMAX_DELAY);
 assert(capture_begin(2,1000));assert(logf("record\n")>0);assert(lines.count==1);const auto log_seq=stats.log_sequence;
 // A cable transition drops queued evidence with counters and disarms capture.
 fake_now+=1;log_usb_power(false);assert(!lines.count&&!frames.count&&!stats.capture_active);assert(logf(invalid)==0&&stats.log_sequence==log_seq);
 make();const auto old=frame;fake_now+=1;log_usb_power(true);assert(!frame_submit(old));
 // Blocked host expires at its original deadline, then returns to an empty wait.
 reset();log_usb_power(true);make();assert(frame_submit(frame));reader=false;run(1600);assert(stats.frame_drops==1&&wire.empty()&&last_wait==portMAX_DELAY);
 // Disconnect during a partial frame; the old response must not resume later.
 reset();log_usb_power(true);make();assert(frame_submit(frame));
 boundary=[](){if(waits==1){reader=false;log_usb_power(false);}if(waits==2){fake_now+=100;log_usb_power(true);reader=true;}};
 run(8);assert(stats.frame_drops==1&&wire.find("W12_V12_END")==std::string::npos&&last_wait==portMAX_DELAY);
 boundary={};wire.clear();make();assert(frame_submit(frame));run(30);assert(wire.find("W12_V12_END")!=std::string::npos);
 std::puts("PASS actual console: battery dormancy, empty notification wait, cable generations, partial-frame abandonment, original expiry, hotplug resume");
 return 0;
}

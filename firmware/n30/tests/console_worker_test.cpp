// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Exercise the actual N30 producer admission and bounded UART output worker.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#define V12_R2_ENABLED 1
uint64_t fake_now=1000;unsigned turns=0,stop_after=0;bool reader=true;
std::string wire;struct Stop{};
int64_t read_clock(){return int64_t(fake_now);}
#define FAKE_ESP_TIMER_READ read_clock
int uart_tx_chars(int port,const char* p,uint32_t n){assert(port==0);if(!reader)return 0;wire.append(p,n);return int(n);}
int uart_wait_tx_done(int port,unsigned timeout){assert(port==0&&timeout==0);return reader?0:-1;}
void vTaskDelay(unsigned n){fake_now+=uint64_t(n)*1000;if(++turns>=stop_after)throw Stop{};}
#include "../main/bounded_log.cpp"
void run(unsigned n){turns=0;stop_after=n;try{v12::output(nullptr);}catch(const Stop&){}assert(turns==n);}
void clear(){fake_now=1000;wire.clear();reader=true;v12::stats={};v12::frames={};v12::lines={};v12::current_frame={};v12::current_line={};}
void make(v12::ConsoleFrame& f,unsigned rows){
 v12::frame_begin(f,1,2,fake_now,"02:00:00:00:30:01",3,"source","build");
 for(unsigned i=0;i<rows;i++)f.format(i==9?"N30_UI profile=RC GPS=ABSENT page=0\n":"ROW_%u value=0\n",i);
}
int main(){
 static v12::ConsoleFrame f;
 clear();make(f,10);assert(v12::frame_submit(f));run(100);
 auto b=wire.find("W12_V12_BEGIN"),e=wire.find("W12_V12_END");
 assert(b!=std::string::npos&&e>b&&wire.find("N30_UI",b)<e);
 assert(wire.find("rows=10",e)!=std::string::npos&&wire.find("end_crc32=",e)!=std::string::npos);
 assert(v12::log_status().frame_drops==0);
 for(unsigned rows:{8u,9u,11u}){clear();make(f,rows);assert(!v12::frame_submit(f));run(100);assert(wire.empty()&&v12::log_status().frame_drops==1);}
 clear();make(f,10);fake_now+=1500000;assert(!v12::frame_submit(f));assert(wire.empty());
 clear();make(f,10);assert(v12::frame_submit(f));reader=false;run(1600);assert(wire.empty()&&v12::log_status().frame_drops==1);
 clear();make(f,10);std::string huge(v12::ConsoleFrameBytes,'X');assert(!f.format("%s",huge.c_str()));assert(!v12::frame_submit(f));
 std::puts("PASS actual N30 frame submit/output: ten rows, nine-row refusal, original deadlines, no-reader expiry, overflow");
}

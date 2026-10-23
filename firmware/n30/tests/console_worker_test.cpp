// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
// Exercise the actual N30 producer admission and bounded UART output worker.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <fstream>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
// Override only this translation unit after loading the shared fake header.
// Shared W12 fake behavior and source remain unchanged.
bool fake_isr=false;
#define xPortInIsrContext() fake_isr
#define V12_R2_ENABLED 1
uint64_t fake_now=1000;unsigned turns=0,stop_after=0;bool reader=true;
std::string wire;struct Stop{};
int64_t read_clock(){return int64_t(fake_now);}
#define FAKE_ESP_TIMER_READ read_clock
int uart_tx_chars(int port,const char* p,uint32_t n){assert(port==0);if(!reader)return 0;wire.append(p,n);return int(n);}
int uart_wait_tx_done(int port,unsigned timeout){assert(port==0&&timeout==0);return reader?0:-1;}
void vTaskDelay(unsigned n){fake_now+=uint64_t(n)*1000;if(++turns>=stop_after)throw Stop{};}
#include "../main/bounded_log.cpp"
#undef xPortInIsrContext
#include "surface_trace_actual.inc"
int invoke_logv(const char* format,...){va_list args;va_start(args,format);const int n=v12::logv(format,args);va_end(args);return n;}
void run(unsigned n){turns=0;stop_after=n;try{v12::output(nullptr);}catch(const Stop&){}assert(turns==n);}
void clear(){fake_isr=false;fake_now=1000;wire.clear();reader=true;v12::stats={};v12::frames={};v12::lines={};v12::current_frame={};v12::current_line={};v12::tx_mode=false;v12::tx_filtered=0;v12::capture_generation=0;v12::live_next_us=0;}
void make(v12::ConsoleFrame& f,unsigned rows){
 v12::frame_begin(f,1,2,fake_now,"02:00:00:00:30:01",3,"source","build");
 for(unsigned i=0;i<rows;i++)f.format(i==9?"N30_UI profile=RC GPS=ABSENT page=0\n":"ROW_%u value=0\n",i);
}
int main(int argc,char** argv){
 static v12::ConsoleFrame f;
 // Every public entry must reject ISR calls without reading a format/snapshot
 // or touching capture/queue state. An invalid pointer catches accidental
 // formatting; native disassembly separately verifies the small stack gate.
 clear();assert(v12::n30_capture_tx_begin(8,1000));fake_isr=true;
 const auto invalid=reinterpret_cast<const char*>(uintptr_t(1));
 assert(invoke_logv(invalid)==0);assert(v12::logf(invalid)==0);
 assert(__wrap_printf(invalid)==0);assert(__wrap_puts(invalid)==0);
 assert(wc12_v9_observer_printf(invalid)==0);assert(v12::log_live(invalid)==0);
 assert(v12::stats.isr_refused==6&&v12::stats.log_sequence==0&&v12::tx_filtered==0);
 assert(v12::stats.enqueue_work_us==0&&v12::stats.suppressed_no_capture==0&&wire.empty());
 fake_isr=false;assert(v12::logf("W12_G1_TX_EST x=1 bound=UNKNOWN\n")>0);run(20);
 assert(!wire.empty()&&v12::stats.isr_refused==6);
 clear();make(f,10);assert(v12::frame_submit(f));run(100);
 auto b=wire.find("W12_V12_BEGIN"),e=wire.find("W12_V12_END");
 assert(b!=std::string::npos&&e>b&&wire.find("N30_UI",b)<e);
 assert(wire.find("rows=10",e)!=std::string::npos&&wire.find("end_crc32=",e)!=std::string::npos);
 assert(v12::log_status().frame_drops==0);
 for(unsigned rows:{8u,9u,11u}){clear();make(f,rows);assert(!v12::frame_submit(f));run(100);assert(wire.empty()&&v12::log_status().frame_drops==1);}
 clear();make(f,10);fake_now+=1500000;assert(!v12::frame_submit(f));assert(wire.empty());
 clear();make(f,10);assert(v12::frame_submit(f));reader=false;run(1600);assert(wire.empty()&&v12::log_status().frame_drops==1);
 clear();make(f,10);std::string huge(v12::ConsoleFrameBytes,'X');assert(!f.format("%s",huge.c_str()));assert(!v12::frame_submit(f));
 // Default capture output remains the original byte envelope.
 clear();assert(v12::capture_begin(9,1000));assert(v12::logf("ordinary record\n")>0);run(20);
 assert(wire=="\nW12_V12_LOG capture_id=0000000000000009 log_seq=1 enqueue_us=1000 record=ordinary record\n");
 assert(!v12::n30_capture_tx_status().selected);
 // TX-only full-payload filter; intentionally excluded records consume the
 // original log sequence and their own counter, not dropped/expired counters.
 clear();assert(v12::n30_capture_tx_begin(10,1000));assert(v12::logf("ordinary record\n")==0);
 assert(v12::n30_capture_tx_status().filtered==1&&v12::log_status().dropped==0);
 assert(v12::logf("W12_G1_TX_EST x=1 bound=UNKNOWN\n")>0);run(20);
 const auto txline=wire;if(argc==2){std::ofstream out(argv[1],std::ios::binary);out<<txline;}assert(txline.find("version=2 mode=TX capture_id=000000000000000a log_seq=2 enqueue_us=1000 record_bytes=32 record_crc32=")!=std::string::npos);
 assert(txline.ends_with(" record=W12_G1_TX_EST x=1 bound=UNKNOWN\n"));
 v12::capture_stop();assert(v12::n30_capture_tx_status().selected&&v12::n30_capture_tx_status().filtered==1);
 assert(!v12::n30_capture_tx_begin(11,999)&&v12::n30_capture_tx_status().filtered==1);
 // Actual producer completion helpers simulate a formatter paused across
 // stop/restart: old generation cannot relabel or increment the new lease.
 const auto old_generation=v12::capture_generation;v12::Line paused;paused.length=1;paused.bytes[0]='X';paused.deadline=100000;
 assert(v12::n30_capture_tx_begin(11,1000));assert(v12::n30_capture_tx_status().filtered==0);
 assert(v12::filtered_log(old_generation,10)==0&&v12::n30_capture_tx_status().filtered==0);
 assert(v12::finish_log(paused,1000,true,old_generation,10,false,false)==0);
 assert(v12::log_status().dropped==2);
 // Original expiry cannot renew a paused same-generation producer.
 fake_now=paused.deadline;assert(v12::finish_log(paused,1000,true,v12::capture_generation,11,false,false)==0);
 assert(v12::log_status().expired==1);
 // Retained TX counter appears only in END, under the complete END CRC.
 clear();assert(v12::n30_capture_tx_begin(12,1000));v12::tx_filtered=UINT32_MAX;make(f,10);assert(v12::frame_submit(f));run(100);
 assert(wire.find(" xf=4294967295 end_crc32=")!=std::string::npos);
 assert(v12::log_status().frame_drops==0);
 // Actual maximum END serialization includes all32/64-bit counters, CRC,
 // final newline and NUL inside800B; its old ALL form remains785B.
 clear();fake_now=uint64_t(INT64_MAX)-2000000;assert(v12::n30_capture_tx_begin(UINT64_MAX,1000));v12::tx_filtered=UINT32_MAX;
 v12::stats.dropped=v12::stats.truncated=v12::stats.isr_refused=v12::stats.write_errors=v12::stats.expired=v12::stats.frame_drops=UINT32_MAX;
 v12::stats.base_dropped=v12::stats.base_truncated=v12::stats.base_isr=v12::stats.base_errors=v12::stats.base_expired=v12::stats.base_frames=UINT32_MAX;
 v12::stats.capture_deadline_us=UINT64_MAX;make(f,10);assert(v12::frame_submit(f));run(100);
 const auto end_begin=wire.find("W12_V12_END ");assert(end_begin!=std::string::npos);
 assert(wire.size()-end_begin<=799&&v12::log_status().frame_drops==UINT32_MAX);
 if(argc==2){std::ofstream out(std::string(argv[1])+"-max-frame.txt",std::ios::binary);out<<wire;}
 std::fprintf(stderr,"Actual positive-int64-clock fixed10-row END: %zu bytes (budget bound799 inclLF, NULwithin800)\n",wire.size()-end_begin);
 // Partial output is still abandoned at its original250ms deadline, and a
 // later priority frame starts with the original delimiter.
 clear();assert(v12::n30_capture_tx_begin(13,1000));std::string long_value(700,'X');assert(v12::logf("W12_G1_TX_PATH x=%s bound=UNKNOWN\n",long_value.c_str())>0);run(1);
 assert(!wire.empty());reader=false;run(260);assert(v12::log_status().expired==1);
 reader=true;make(f,10);assert(v12::frame_submit(f));run(100);assert(wire.find("\nW12_V12_BEGIN")!=std::string::npos);
 // Normalize a missing final newline and refuse multiline target records.
 clear();assert(v12::n30_capture_tx_begin(14,1000));assert(v12::logf("W12_G1_TX_EST x=1 bound=UNKNOWN")>0);run(20);assert(wire.ends_with("bound=UNKNOWN\n"));
 assert(v12::logf("W12_G1_TX_EST bad\nforged\n")==0&&v12::log_status().truncated==1);
 assert(v12::logf("W12_G1_TX_EST bad\tfield bound=UNKNOWN\n")==0&&v12::log_status().truncated==2);
 assert(v12::logf("W12_G1_TX_EST bad\rfield bound=UNKNOWN\n")==0&&v12::log_status().truncated==3);
 v12::capture_stop();assert(v12::capture_begin(15,1000));assert(!v12::n30_capture_tx_status().selected&&v12::n30_capture_tx_status().filtered==0);
 // Compile the real surface-record owner and poller, using the real bounded
 // logger/UART worker. It must remain silent by default, retain dropped
 // records explicitly, fit maximum widths, and obey TX-only filtering.
 clear();surfaces={};SurfaceRecord surface{UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT32_MAX,1,0,1,3,1,1,1,6,4,255,0};
 assert(surfaces.push(surface));n30_display_trace_poll();run(10);assert(wire.empty()&&!surfaces.count);
 clear();surfaces={};assert(v12::capture_begin(16,1000));
 for(unsigned i=0;i<4;i++)assert(surfaces.push(surface));assert(!surfaces.push(surface)&&surfaces.drops==1);
 for(unsigned i=0;i<4;i++){n30_display_trace_poll();run(20);}
 assert(!surfaces.count&&v12::log_status().truncated==0&&v12::log_status().expired==0);
 assert(wire.find("N30_UI_SURFACE seq=4294967295 dropped=1 at_us=18446744073709551615")!=std::string::npos);
 assert(wire.find("written=ff ok=0\n")!=std::string::npos);
 clear();surfaces={};assert(v12::n30_capture_tx_begin(17,1000));assert(surfaces.push(surface));n30_display_trace_poll();run(20);
 assert(wire.empty()&&v12::n30_capture_tx_status().filtered==1);
 std::puts("PASS actual N30 frame submit/output and capture-only surface trace: maximum widths, bounded drops, OFF silence, TX filtering");
}

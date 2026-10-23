// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#define V12_R2_ENABLED 1
#define PTOB_N30 1
#define PTOB_N30_FINE_PINS 1
#define PTOB_N30_FINE_RW 1
#define PTOB_N30_FINE_BACKEND 1
#include "../main/fine_capture.cpp"
namespace v12 {
bool ConsoleFrame::format(const char* fmt,...){va_list args;va_start(args,fmt);const int n=std::vsnprintf(bytes+length,sizeof(bytes)-length,fmt,args);va_end(args);if(n<=0||unsigned(n)>=sizeof(bytes)-length){failed=true;return false;}for(int i=0;i<n;++i)rows+=bytes[length+i]=='\n';length+=uint16_t(n);return true;}
}
void advance(uint64_t us){fake_now+=us;fake_raw+=uint32_t(us*80);}
void edge(unsigned ch,uint64_t raw,uint32_t polarity){fake_radio_capture[ch]=uint32_t(raw);const mcpwm_capture_event_data_t e{uint32_t(raw),polarity};fake_channels[ch].cb(&fake_channels[ch],&e,fake_channels[ch].context);}
int main(){
 assert(n30_fine_backend_selected()&&!production_owned&&n30_fine_released());
 static_assert(sizeof(packet_edges)==1);
 assert(!n30_fine_window_start(1,PTOB_N30_FINE_FACTORY,fake_now,1000));
 assert(!window_owned&&n30_fine_released());
 assert(!n30_fine_start(1,PTOB_N30_FINE_FACTORY,fake_now,1000,1));
 assert(n30_fine_backend_start(2,PTOB_N30_FINE_FACTORY,fake_now));assert(active&&production_owned&&history.until()==UINT64_MAX);
 assert(!n30_fine_window_start(3,PTOB_N30_FINE_FACTORY,fake_now,1000));
 assert(!n30_fine_window_begin(1,1,2,incarnation)&&!n30_fine_window_refuse(1)&&!n30_fine_window_rearm());
 const auto empty_window=n30_fine_window_status();
 assert(!window_owned&&active&&production_owned&&!empty_window.lease&&!empty_window.id&&!empty_window.count&&!empty_window.sealed);
 assert(empty_window.active&&empty_window.current_incarnation==incarnation&&empty_window.hz==80000000);
 const auto origin=uint64_t(first_raw32);uint64_t current20=0;assert(n30_fine_backend_current(&current20)&&current20>=1000000);
 v12::periodic_rx::Stamp missing{};assert(n30_fine_backend_receive(missing)==0&&active&&!error);
 const auto center=origin+800000;advance(12000);edge(0,center-10000,0);edge(0,center+125440,1);
 v12::periodic_rx::Stamp stamp{};stamp.tick20=1199920;stamp.radio_extra_us=4;stamp.available20=current20;stamp.epoch=1;stamp.sequence=1;stamp.actual=stamp.association_observed=stamp.radio_reference_applied=stamp.radio_reference_valid=true;stamp.map_enclosure_ns=0;stamp.phy=0;stamp.pdu_length=191;
 assert(n30_fine_backend_receive(stamp)==1);advance(2000);assert(n30_fine_backend_receive(stamp)==2&&stamp.tick20==1200000&&stamp.available20>stamp.tick20&&!stamp.edge_calibrated&&stamp.map_enclosure_ns==4025);
 assert(receiver.status().claimed==1&&n30_fine_backend_current(&current20));
 assert(backend_stats.stamp20==stamp.tick20&&backend_stats.available20==stamp.available20&&backend_stats.fall80==center+125440);
 static v12::ConsoleFrame frame;frame={};n30_fine_frame(frame,0,fake_now);assert(!frame.failed&&std::strstr(frame.bytes,"clock_update=G1_RX")&&std::strstr(frame.bytes,"backend=1")&&std::strstr(frame.bytes,"phase80=125440"));
 {FILE* o=std::fopen("fine-backend-frame.txt","w");assert(o);std::fwrite(frame.bytes,1,frame.length,o);std::fclose(o);}
 // Production is not a10-minute observer lease or500,000-callback timer.
 for(unsigned i=0;i<61;++i){advance(10000000);assert(n30_fine_backend_current(&current20));n30_fine_poll(0);}
 assert(active&&history.until()==UINT64_MAX&&production_owned);total_callbacks=500000;edge(1,last_raw80-100,0);assert(active&&!error&&total_callbacks==500001);
 // A hardware pending late capture is rejected before it can escape in a
 // new CURRENT/finalize call, even if its callback has not yet run.
 // Use a fresh recent claimed interval after the long health exercise.
 const auto d=last_raw80+800000;advance(12000);edge(0,d-10000,0);edge(0,d+125440,1);advance(2000);
 stamp.tick20=n30::fine::Counter::Base20+(d-origin)/4;stamp.epoch=2;stamp.sequence=1;assert(n30_fine_backend_receive(stamp)==2);
 fake_radio_capture[0]=uint32_t(d+120000);fake_device.int_raw.val|=1u<<27;
 assert(!n30_fine_backend_current(&current20)&&!active&&contained&&receiver.status().fault&&!GPIO.oe[4]&&!GPIO.oe[5]);
 n30_fine_stop();assert(n30_fine_released());const auto disabled=fake_output_disables[4]+fake_output_disables[5];assert(!n30_fine_backend_current(&current20));
 assert(n30_fine_backend_receive(missing)==0&&fake_output_disables[4]+fake_output_disables[5]==disabled&&n30_fine_released());
 std::puts("PASS actual persistent owner, finite-command exclusion, rawfall phase, nearest commonCURRENT, >600s/wrap/500k health, pending-late failclosed and maintenance release");
}

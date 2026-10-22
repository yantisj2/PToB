// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#define V12_R2_ENABLED 1
#define PTOB_N30 1
#include "../main/fine_capture.cpp"
namespace v12 {
bool ConsoleFrame::format(const char* fmt,...){
 va_list args;va_start(args,fmt);const int n=std::vsnprintf(bytes+length,sizeof(bytes)-length,fmt,args);va_end(args);
 if(n<=0||unsigned(n)>=sizeof(bytes)-length){failed=true;return false;}
 for(int i=0;i<n;++i)rows+=bytes[length+i]=='\n';length+=uint16_t(n);return true;
}
}
#include "update_gate.hpp"
#undef V12_R2_ENABLED
namespace v12 {
enum class Lifecycle {Off,Fault};enum class DirectStage {Off,Fault};
struct Rx {struct {bool scan=false,pending=false,synced=false;Lifecycle state=Lifecycle::Off;}client;bool radio_requested=false,host_work_busy=false,command_pending=false,off_acknowledged=true;};
struct Tx {bool owner_busy=false,off_acknowledged=true,periodic=false,requested=false,discovery=false,fault=false;struct {bool pending=false;}lease;};
struct Direct {DirectStage stage=DirectStage::Off;};
struct Lora {bool enabled=false,requested=false,worker_busy=false,off_acknowledged=true,failed=false;};
Rx transport_status(){return {};}Tx diagnostic_tx_status(){return {};}Direct direct_status(){return {};}Lora lora_status(){return {};}
}
v12::UpdateGate update_gate;uint64_t update_completed_us=0;
uint64_t micros(){return fake_now;}
#include "actual_fine_update_gate.hpp"
void clear_error(){assert(n30_fine_released());error=0;fake_complete=fake_clear_ok=fake_disable_ok=true;fake_stale=false;fake_cycle_step=32;fake_device.int_raw.val=0;}
int main(){
 assert(n30_fine_released()&&fake_allocated==0);
 assert(!n30_fine_start(1,0,fake_now,1000,1)&&!n30_fine_start(0,PTOB_N30_FINE_FACTORY,fake_now,1000,1));
 assert(fake_allocated==0&&!n30_fine_start(1,PTOB_N30_FINE_FACTORY,fake_now,999,1));
 assert(n30_fine_start(1,PTOB_N30_FINE_FACTORY,fake_now,1000,1));
 assert(fake_group==1&&fake_allocated==3&&fake_callbacks==0&&!fake_channels[2].cb&&!routed&&!n30_fine_released());
 assert(!n30_fine_start(2,PTOB_N30_FINE_FACTORY,fake_now,1000,1));
 for(unsigned i=0;i<40;++i){mcpwm_capture_event_data_t e{fake_raw+40,i%2+1};on_edge(&fake_channels[i%2],&e,reinterpret_cast<void*>(uintptr_t(i%2)));}
 assert(!fake_channels[0].fsm&&!fake_channels[1].fsm&&!fake_device.cap_chn_cfg[0].capn_en&&!fake_device.cap_chn_cfg[1].capn_en);
 assert(history.seen()==40&&history.rx()==20&&history.tx()==20&&history.overwritten()==8&&invalid==0);
 static v12::ConsoleFrame f;f={};n30_fine_frame(f,0,fake_now);assert(!f.failed&&f.rows==6&&f.length<4000);
 if(auto* output=std::fopen("fine-frame.txt","wb")){std::fwrite(f.bytes,1,f.length,output);std::fclose(output);}
 assert(std::strstr(f.bytes,"N30_FINE_EDGE0 present=1 n=9")&&std::strstr(f.bytes,"packet_association=NONE")&&std::strstr(f.bytes,"clock_update=NONE"));
 const auto retained=history.sequence();f={};n30_fine_frame(f,0,fake_now);assert(history.sequence()==retained);
 // Older command admission/main-loop time must not revoke a lease after an ISR.
 const auto old=fake_now-10;assert(active);n30_fine_poll(old);assert(active);
 n30_fine_stop();assert(n30_fine_released()&&!n30_fine_cleanup_fault());
 // Unknown native callback storm is contained without a main-task service.
 assert(n30_fine_start(98,PTOB_N30_FINE_FACTORY,fake_now,1000,1));
 for(unsigned i=0;i<65;++i){mcpwm_capture_event_data_t edge{fake_raw+40,1};on_edge(channels[0],&edge,nullptr);}
 assert(contained&&!active&&error==-18&&!(fake_device.int_ena.val&(3u<<27))&&!fake_device.cap_chn_cfg[0].capn_en&&!fake_device.cap_chn_cfg[1].capn_en);
 n30_fine_stop();clear_error();
 // Failure to disable cannot falsely acknowledge update drain.
 assert(n30_fine_start(2,PTOB_N30_FINE_FACTORY,fake_now,1000,1));fake_disable_ok=false;n30_fine_stop();
 assert(!n30_fine_released()&&n30_fine_cleanup_fault());
 assert(update_gate.begin(fake_now));poll_update_drain(fake_now);assert(update_gate.stage==v12::UpdateStage::Fault&&update_gate.deadline==fake_now+2000000);
 fake_disable_ok=true;n30_fine_stop();clear_error();
 update_gate={};assert(update_gate.begin(fake_now));poll_update_drain(fake_now);assert(update_gate.stage==v12::UpdateStage::Ready);
 fake_start_delay=2000000;assert(!n30_fine_start(99,PTOB_N30_FINE_FACTORY,fake_now,1000,1));assert(n30_fine_released()&&!routed);fake_start_delay=0;clear_error();
 // Fresh raw-completion cannot be fabricated by stale INT_RAW or CAP value.
 fake_device.int_raw.val=1u<<29;fake_clear_ok=false;assert(!n30_fine_start(3,PTOB_N30_FINE_FACTORY,fake_now,1000,1));assert(error==-8&&timeouts==1);clear_error();
 fake_complete=false;assert(!n30_fine_start(4,PTOB_N30_FINE_FACTORY,fake_now,1000,1));assert(timeouts==1);clear_error();
 fake_stale=true;assert(!n30_fine_start(5,PTOB_N30_FINE_FACTORY,fake_now,1000,1));clear_error();
 fake_cycle_step=5000;assert(!n30_fine_start(6,PTOB_N30_FINE_FACTORY,fake_now,1000,1));assert(timeouts==1);clear_error();
 assert(n30_fine_start(7,PTOB_N30_FINE_FACTORY,fake_now,1000,1));fake_device.cap_timer_cfg.cap_timer_en=false;n30_fine_poll(fake_now);assert(n30_fine_released()&&error==-13);clear_error();
 assert(n30_fine_start(8,PTOB_N30_FINE_FACTORY,fake_now,1000,1));fake_now=history.until();n30_fine_poll(fake_now);assert(n30_fine_released());
 // Real adapter formatter at maximum field widths and four retained records.
 snapshot={};history={};assert(history.start(UINT64_MAX,fake_now,600000,1));
 for(unsigned i=0;i<4;++i)history.append({UINT64_MAX,UINT64_MAX,UINT64_MAX,0,UINT32_MAX,UINT32_MAX,1,2,true});
 incarnation=hz=current_reads=timeouts=invalid=poll_max=cycles_max=service_max_us=UINT32_MAX;last_raw80=UINT64_MAX;error=-2147483647;
 f={};n30_fine_frame(f,0,fake_now);assert(!f.failed&&f.rows==6&&f.length<4000);
 std::printf("PASS actual fine adapter: no default resources/pins; IDs0/1/2/group1; masked/noedgeCURRENT; RAWclear/completion/stale/timeout/cyclebound; edges/cursor/finiteexpiry; failedcleanupoffack; max6rows=%u\n",f.length);
}

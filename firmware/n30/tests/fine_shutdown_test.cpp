#include "../../w12/main/power_off.hpp"
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
#include "update_gate.hpp"
namespace v12 {
bool ConsoleFrame::format(const char*,...){return true;}
enum class Lifecycle {Off,Fault};enum class DirectStage {Off,Fault};
struct Rx {struct {bool scan=false,pending=false,synced=false;Lifecycle state=Lifecycle::Off;}client;bool radio_requested=false,host_work_busy=false,command_pending=false,off_acknowledged=true;};
struct Tx {bool owner_busy=false,off_acknowledged=true,periodic=false,requested=false,discovery=false,fault=false;struct {bool pending=false;}lease;};
struct Direct {DirectStage stage=DirectStage::Off;};
struct Lora {bool enabled=false,requested=false,worker_busy=false,off_acknowledged=true,failed=false;};
struct Mesh {bool requested=true,scan=false,sync_pending=false,connecting=false,synced=true,advertising=false,owner_busy=false,off_ack=false,fault=false;unsigned connections=0;struct {bool installed=true;}rx_capture;struct {bool pending=false,running=true,stopping=false;}client;};
Rx rx;Tx tx;Direct acl;Lora lora;Mesh mesh;
Rx transport_status(){return rx;}Tx diagnostic_tx_status(){return tx;}Direct direct_status(){return acl;}Lora lora_status(){return lora;}Mesh r2_status(){return mesh;}
void fault_watchdog_sync_progress(bool,uint64_t){}
void r2_stop(){mesh.requested=false;mesh.client.stopping=true;}
void transport_stop(){}void direct_stop(){}void diagnostic_tx_stop(){}void lora_request(bool,uint32_t){}
}
w12::PowerOff power_off;bool display_ready=true;bool n30_display_power_ready(){return display_ready;}
v12::UpdateGate update_gate;uint64_t update_completed_us=0;bool fine_stop_pending=false;
uint64_t micros(){return fake_now;}
void n30_timing_stop(){}
void n30_window_stop(){n30_fine_window_stop();}
#include "actual_fine_stop_all.hpp"
#include "actual_fine_update_gate.hpp"
void host_closed(){
 // The real RX owner polls its reference before uninstalling its hooks. A
 // prematurely stopped clock makes this assertion reproduce the old fault.
 uint64_t tick=0;assert(n30_fine_backend_current(&tick));
 v12::mesh.synced=false;v12::mesh.client.pending=v12::mesh.client.running=v12::mesh.client.stopping=false;v12::mesh.rx_capture.installed=false;v12::mesh.off_ack=true;
}
int main(int argc,char** argv){
 assert(argc==2);const bool off=!std::strcmp(argv[1],"off"),timeout=!std::strcmp(argv[1],"timeout"),cleanup=!std::strcmp(argv[1],"cleanup"),fault=!std::strcmp(argv[1],"fault");
 const bool power=!std::strcmp(argv[1],"power")||!std::strcmp(argv[1],"power-timeout");
 const bool power_timeout=!std::strcmp(argv[1],"power-timeout");
 assert(n30_fine_backend_start(2,PTOB_N30_FINE_FACTORY,fake_now));
 if(power){assert(power_off.request(update_gate,fake_now));display_ready=false;}else if(!off)assert(update_gate.begin(fake_now));const auto original_deadline=update_gate.deadline;
 stop_all();assert(fine_stop_pending&&active&&!n30_fine_released());
 poll_update_drain(fake_now);assert(active&&!n30_fine_released()&&update_gate.stage==(off?v12::UpdateStage::Idle:v12::UpdateStage::Draining));
 if(timeout){fake_now+=2000000;fake_raw+=160000000;poll_update_drain(fake_now);assert(update_gate.stage==v12::UpdateStage::Fault&&active);}
 host_closed();v12::lora.worker_busy=true;poll_update_drain(fake_now);assert(active&&fine_stop_pending);
 v12::lora.worker_busy=false;if(cleanup)fake_disable_ok=false;if(fault)v12::mesh.fault=true;
 poll_update_drain(fake_now);
 if(power){assert(n30_fine_released()&&update_gate.stage==v12::UpdateStage::Draining);if(power_timeout)fake_now=original_deadline;else display_ready=true;poll_update_drain(fake_now);}
 if(cleanup){assert(update_gate.stage==v12::UpdateStage::Fault&&!n30_fine_released());fake_disable_ok=true;poll_update_drain(fake_now);}
 assert(n30_fine_released()&&!fine_stop_pending&&update_gate.deadline==original_deadline);
 assert(update_gate.stage==(off?v12::UpdateStage::Idle:((timeout||cleanup||fault||power_timeout)?v12::UpdateStage::Fault:v12::UpdateStage::Ready)));
 std::puts("PASS actual stop_all/drain: same production clock survives RX and LoRa close; original2s timeout, fault and failedcleanup never become READY");
}

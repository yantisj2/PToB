// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include <cassert>
#include <cstdio>
#include <cstring>
#define V12_R2_ENABLED 1
#include "w12_fine_clock.cpp"
#include "update_gate.hpp"
namespace v12 {
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

v12::UpdateGate update_gate;uint64_t update_completed_us=0;
uint64_t micros(){return fake_now;}
#include "actual_fine_stop_all.hpp"
#include "actual_fine_update_gate.hpp"
int main(int argc,char** argv){
 assert(argc==2);assert(w12_fine_start(2,PTOB_W12_FINE_FACTORY,fake_now));
 assert(update_gate.begin(fake_now));const auto deadline=update_gate.deadline;
 stop_all();poll_update_drain(fake_now);assert(active&&update_gate.stage==v12::UpdateStage::Draining);
 uint64_t tick=0;assert(w12_fine_current(&tick));
 if(!std::strcmp(argv[1],"timeout")){
   fake_now+=2000000;fake_raw+=160000000;poll_update_drain(fake_now);
   assert(update_gate.stage==v12::UpdateStage::Fault&&active&&update_gate.deadline==deadline);
   w12_fine_stop();return 0;
 }
 v12::mesh.synced=false;v12::mesh.client.pending=v12::mesh.client.running=v12::mesh.client.stopping=false;
 v12::mesh.rx_capture.installed=false;v12::mesh.off_ack=true;
 v12::lora.worker_busy=true;poll_update_drain(fake_now);assert(active);
 v12::lora.worker_busy=false;
 const bool cleanup=!std::strcmp(argv[1],"cleanup");
 if(cleanup)fake_disable_ok=false;
 poll_update_drain(fake_now);
 if(cleanup){assert(update_gate.stage==v12::UpdateStage::Fault&&!w12_fine_released());fake_disable_ok=true;w12_fine_stop();}
 else assert(update_gate.stage==v12::UpdateStage::Ready);
 assert(w12_fine_released()&&update_gate.deadline==deadline);
 puts("PASS actual W12 maintenance retains counter until consumers close; original2s and cleanup failure gates");
}

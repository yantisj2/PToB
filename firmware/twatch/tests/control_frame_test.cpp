// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#define main inherited_console_main
#include "console_power_test.cpp"
#undef main
#include "control_frame.hpp"
#include "shadow_gnss.hpp"
#include "ble_power.hpp"
#include <climits>
#include "control_common_max.inc"
struct Mesh {uint32_t mode=UINT32_MAX;bool requested=true,following=true,serving=false,owner_busy=false,off_ack=false,fault=false;uint64_t group_id=UINT64_MAX,client_identity=UINT64_MAX;uint32_t key_epoch=UINT32_MAX;};
static Mesh mesh;
static bool fine_off=false;
static watch::ControlUi ui{UINT32_MAX,UINT32_MAX,3,true,false,false};
static watch::BlePower power;
static watchshadow::ControlProof gnss{true,true,false,true,true,true,false,false};
static void control(uint64_t nonce,const CommonControl& cs={}){
 using namespace v12;frame_begin(frame,nonce,nonce+1,fake_now,"02:00:00:00:00:01",3,"source","build");
 watch::control_mesh(frame,mesh,"r2-g1","eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");
 common_control(frame,nonce,fake_now,false,cs);
 watch::control_ui(frame,ui,power,gnss,fine_off,false);assert(!frame.failed&&frame.rows==10);
}
int main(int argc,char** argv){
 assert(argc==2);inherited_console_main();using namespace v12;
 power.generation=power.off_generation=UINT32_MAX;power.error=power.cleanup_error=INT32_MIN;power.started_us=power.deadline_us=power.off_proven_us=UINT64_MAX;
 static ConsoleFrame sizing;watch::control_mesh(sizing,mesh,"r2-g1","eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee");watch::control_ui(sizing,ui,power,gnss,false,false);
 FILE* v=std::fopen(argv[1],"w");assert(v);std::fwrite(sizing.bytes,1,sizing.length,v);std::fclose(v);assert(sizing.length<1700&&!sizing.failed);
 // A verbose overflow cannot corrupt or consume the next compact control job.
 reset();log_usb_power(true);make();frame.format("%09000u",0u);assert(frame.failed&&!frame_submit(frame));const auto rejected=stats.frame_drops;assert(rejected==1);
 mesh.mode=3;control(2);assert(frame_submit(frame));run(50);assert(wire.find("control_version=1")!=std::string::npos&&wire.find("end_crc32=")!=std::string::npos&&watch_completed_frame()==2);
 FILE* f=std::fopen((std::string(argv[1])+".wire").c_str(),"w");assert(f);std::fwrite(wire.data(),1,wire.size(),f);std::fclose(f);
 // Original expiry and reconnect behavior apply unchanged to compact control.
 reset();log_usb_power(true);control(3);assert(frame_submit(frame));reader=false;run(1600);assert(stats.frame_drops==1&&wire.empty()&&watch_completed_frame()!=3);
 reader=true;fake_now+=1;log_usb_power(false);fake_now+=1;log_usb_power(true);control(4);assert(frame_submit(frame));run(50);assert(watch_completed_frame()==4);
 watch::Bootrom boot;assert(boot.request(4,fake_now,true));assert(!boot.poll(fake_now,true,3,true));assert(!boot.poll(fake_now,true,4,false));assert(boot.poll(fake_now,true,watch_completed_frame(),true));
 // Emit the real guarded install handshake, preserving one immutable drain.
 reset();log_usb_power(true);mesh.mode=3;ui.errors=0;ui.ready=true;ui.frames=1;power={};gnss={true,true,false,true,true,true,false,false};fine_off=false;
 auto emit=[&](uint64_t n,const char* suffix,const CommonControl& cs){wire.clear();control(n,cs);assert(frame_submit(frame));run(30);FILE* q=std::fopen((std::string(argv[1])+suffix).c_str(),"w");assert(q);std::fwrite(wire.data(),1,wire.size(),q);std::fclose(q);};
 emit(5,".bootstrap",{});const auto admitted=fake_now;CommonControl preparing;preparing.stage=1;preparing.deadline=admitted+2000000;preparing.action="PREPARE_UPDATE";preparing.reason="ORIGINAL_2S_DRAIN";emit(6,".prepare",preparing);
 power.stage=watch::BlePower::Off;power.requested=power.host_running=power.worker_running=false;power.controller=0;power.generation=power.off_generation=2;power.started_us=admitted;power.deadline_us=admitted+2000000;power.off_proven=true;power.off_proven_us=admitted+1000;power.cleanup_error=0;gnss={false,false,false,false,false,false,true,false};fine_off=true;ui.display_off=true;
 CommonControl ready=preparing;ready.stage=2;ready.drained=true;ready.completed=admitted+1500;ready.action="CONTROL_STATUS";ready.reason="READ_ONLY";emit(7,".ready",ready);
 ready.action="BOOTROM";ready.reason="ACK_THEN_ROM";emit(8,".bootrom",ready);
 watch::Bootrom exact_ack;assert(exact_ack.request(8,fake_now,true));assert(!exact_ack.poll(fake_now,true,7,true));assert(exact_ack.poll(fake_now,true,watch_completed_frame(),true));
 std::printf("PASS actual compact control: max mesh/UI %u bytes, verbose overflow recovery, CRC output, backpressure/original expiry, reconnect and exact ACK drain\n",unsigned(sizing.length));
}

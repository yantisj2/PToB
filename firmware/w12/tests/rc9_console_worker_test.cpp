// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual native diagnostic builder and actual writer, not a replacement codec.
#define main shared_writer_checks
#include "bounded_log_test.cpp"
#undef main
#include "native_r2.hpp"
#include "w12_lora.hpp"
#include "sync_watchdog.hpp"
uint64_t micros(){return fake_now;}
#include "display_second_deadline.hpp"
namespace v12 {
R2Status r2_status(){R2Status s;s.group_id=17;s.key_epoch=9;s.client_identity=0x020000000005;return s;}
Rc9Status r2_rc9_status(){return {};}
}
void w12_lora_snapshot(w12_lora_view& l){l={};l.link_rms_ns=123;l.active=true;l.accepted=10;}
static const char factory_text[]="02:00:00:00:00:05";
constexpr uint64_t session=0x1234;
#define V12_R2_PROFILE "r2-g1"
#define V12_R2_ENROLLMENT_SHA "0000000000000000000000000000000000000000000000000000000000000000"
#define V12_SOURCE_REVISION "0000000000000000000000000000000000000000"
#define V12_BUILD_ID "0000000000000000000000000000000000000000000000000000000000000000"
static v12::ConsoleTicket ticket;static v12::ConsoleFrame response;
static bool gps_ready=true,ui_valid=true;static uint8_t page=0;static uint32_t ui_utc=1700000000,ui_paint_us=2000;
static uint64_t ui_sampled_us=1000,ui_utc_us=1700000000000000,ui_drawn_us=3000;
static w12::DisplaySecondDeadline ui_deadline;
static struct {uint64_t nonce=0xabc,admitted_us=1000;bool accepted=true;const char* action="RC9_DIAG";const char* reason="READ_ONLY_CRC_SNAPSHOT";} last_command;
#include "actual_rc9_frame.inc"
int main(int argc,char** argv){
 shared_writer_checks(1,argv);reset();ui_deadline.painted(ui_sampled_us,true,ui_utc_us);
 rc9_status_frame(0xabc,fake_now);assert(v12::frames.count==1&&v12::stats.frame_drops==0&&response.rows==10);
 fake_reader=true;run(100);assert(fake_wire.find("rows=10")!=std::string::npos&&fake_wire.find("end_crc32=")!=std::string::npos);
 if(argc==2){std::ofstream f(argv[1]);f<<fake_wire;}
 reset();assert(!v12::frame_submit(response)); // Ten rows cannot enter normal status.
 for(auto rows:{9,11}){reset();response.rows=rows;assert(!v12::frame_submit_rc9(response));}
 reset();response.rows=10;fake_now=response.deadline_us;assert(!v12::frame_submit_rc9(response));
 reset();rc9_status_frame(0xabc,fake_now);run(1600);assert(fake_wire.empty()&&v12::stats.frame_drops==1);
 puts("PASS actual RC9 diagnostic builder/writer fixed10 rows, normal9 refusal, original expiry and whole CRC");
}

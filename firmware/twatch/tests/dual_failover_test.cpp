// Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual receiver adapter/controller: calibration applicability is not admission.
#include "../main/dual_radio.cpp"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
namespace v12 {
R2Status test_status;
R2Status r2_status(){return test_status;}
bool ConsoleFrame::format(const char* fmt,...){va_list a;va_start(a,fmt);int n=std::vsnprintf(bytes+length,sizeof bytes-length,fmt,a);va_end(a);if(n<0||unsigned(n)>=sizeof bytes-length){failed=true;return false;}for(int i=0;i<n;++i)rows+=bytes[length+i]=='\n';length+=uint16_t(n);return true;}
}
static n30_lora_view radio;
static unsigned reads=0;
static bool read20(uint64_t* t){*t=shadow_fake::now*20+(++reads);return true;}
extern "C" bool n30_lora_reference_read(uint64_t* t){*t=shadow_fake::now*20+1000;return true;}
bool watch_clock_bridge(bool (*)(uint64_t*),WatchClockBridge& b){b={shadow_fake::now*20,shadow_fake::now*20+999,shadow_fake::now*20+1001,1,true};return true;}
void watch_lora_dual_snapshot(n30_lora_view& out,watch::dual::LoRaBridge& b){out=radio;out.sampled_us=shadow_fake::now;b={shadow_fake::now*20+999,shadow_fake::now*20+1001,shadow_fake::now*16+16000000,shadow_fake::now,radio.capture_epoch};}
constexpr uint64_t UTC=UINT64_C(1790000000000000000);
static void ble(uint64_t root){
 auto& s=v12::test_status;s={};const auto tick=shadow_fake::now*20;
 s.requested=s.following=s.clock_valid=s.client.running=true;s.client.generation=1;
 s.g1_clock_origin=s.root_identity=root;s.root_boot=root+10;s.source_epoch=s.publication=1;s.rx_capture.epoch=root;
 s.follower_snapshot_us=shadow_fake::now;s.g1_anchor_deadline_us=s.g1_corrected_until_us=s.service_until_us=shadow_fake::now+2000000;
 s.g1_clock_model={tick, int64_t(UTC+shadow_fake::now*1000),0,0,0};s.g1_corrected_original20=tick;
 auto& q=s.g1_relative_quality;q.original20=q.evaluated20=tick;q.samples=32;q.rms_ns=50;q.max_ns=100;q.rate_guard_ppb=10;q.scope_generation=uint32_t(root);
 assert(!ptob_ble::ble_lock_refusal(s,shadow_fake::now));
}
static void lora(uint64_t emitter){
 radio={};radio.requested=radio.active=radio.time_valid=radio.metadata_known=radio.model_valid=true;
 radio.state=2;radio.model_kind=1;radio.capture_epoch=radio.model_epoch=radio.request_generation=1;
 radio.emitter=emitter;radio.emitter_boot=emitter+10;radio.root=watchdual::profile.lora_root;radio.root_boot=22;
 radio.edge_tick=radio.model_tick16=shadow_fake::now*16+16000000;
 radio.model_ns=UTC+shadow_fake::now*1000+35000;radio.until_us=shadow_fake::now+2000000;
}
int main(){
 using watch::dual::Mode;watchdual::init(123,read20);assert(watchdual::profile.enabled);
 shadow_fake::now=1000000;ble(watchdual::profile.ble_root);watchdual::poll(shadow_fake::now);
 auto c=watchdual::clock();assert(c.valid&&c.mode==Mode::Ble);const auto instance=c.instance;
 shadow_fake::now+=1000000;v12::test_status.following=false;watchdual::poll(shadow_fake::now);assert(watchdual::clock().mode==Mode::Hold);
 // Fresh authenticated GM with a different root; never pin availability to
 // the root used by the previous offset measurement.
 for(unsigned i=0;i<=4;++i){shadow_fake::now+=62500;ble(99);watchdual::poll(shadow_fake::now);}
 c=watchdual::clock();assert(c.mode==Mode::Ble&&c.instance==instance&&!c.calibrated&&c.uncertainty_floor_ns==UINT32_MAX);
 const auto pps_us=shadow_fake::now+25000;
 watchdual::edge({pps_us*20,pps_us,1,1,50,true},{shadow_fake::now*20,1,1,true},true);
 const auto& row=watchdual::state->rows[(watchdual::state->rhead+31)%32];
 assert(!row.ble.calibration_match&&row.ble.correction==watchdual::profile.ble_existing_ns+watchdual::ble_extra(false));
 assert(row.ble.utc_ns==UTC+pps_us*1000+watchdual::ble_extra(false));
 assert(watchdual::ble_extra(false)==(watchdual::profile.study_fixed_priors?watchdual::profile.ble_incremental_ns:0));
 assert(watchdual::lora_extra(false)==(watchdual::profile.study_fixed_priors?watchdual::profile.lora_correction_ns:0));
 // Backend expiry and authentication remain mandatory after failover.
 v12::test_status.service_until_us=shadow_fake::now;watchdual::poll(shadow_fake::now);assert(watchdual::clock().mode==Mode::Hold);
 shadow_fake::now+=62500;ble(99);v12::test_status.following=false;lora(watchdual::profile.lora_emitter);watchdual::poll(shadow_fake::now);
 assert(watchdual::clock().mode==Mode::LoRa);
 shadow_fake::now+=62500;lora(77);watchdual::poll(shadow_fake::now);assert(watchdual::clock().mode==Mode::LoRa&&!watchdual::clock().calibrated);
 const auto& snap=watchdual::state->snapshot;
 assert(snap.ble.edge==snap.lora.edge&&snap.lora.edge==snap.clock.tick);
 assert(!snap.lora.calibration_match&&snap.lora.correction==watchdual::lora_extra(false));
 assert(snap.lora.utc_ns&&snap.lora.available<snap.clock.tick);
 v12::ConsoleFrame snapshot;watchdual::snapshot_frame(snapshot);
 assert(!snapshot.failed&&snapshot.rows==1&&snapshot.length<6000);
 assert(std::strstr(snapshot.bytes,"TWATCH_GNSS_FREE_SNAPSHOT_V1"));
 assert(!std::strstr(snapshot.bytes,"phase_ns"));
 std::puts(snapshot.bytes);
 for(unsigned i=0;i<=4;++i){shadow_fake::now+=62500;ble(99);watchdual::poll(shadow_fake::now);}
 assert(watchdual::clock().mode==Mode::Ble);
 v12::test_status.requested=false;radio.requested=false;watchdual::poll(shadow_fake::now);
 c=watchdual::clock();assert(c.mode==Mode::Hold&&!c.authoritative&&c.instance==instance);
 watchdual::state->~State();std::free(watchdual::state);watchdual::state=nullptr;
 std::puts("PASS actual authenticated GM failover, prompt BLE return, independent LoRa fallback and both-loss HOLD");
}

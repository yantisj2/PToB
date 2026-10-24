// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual CoreS3-SE LCD owner. Timing remains on the existing serialized host.
#include "ui.hpp"
#include "sleep_gate.hpp"
#include "corese_display.hpp"
#include "mesh_owner.hpp"
#include "native_r2.hpp"
#include "native_device_binding.hpp"
#include "display.hpp"
#include "n30_display.hpp"
#include "fine_capture.hpp"
#include "fine_backend_api.hpp"
#include "bounded_log.hpp"
#include "ptob_registry.h"
#include "../../ss3/main/td_publication.hpp"
#include <M5Unified.h>
#include "esp_heap_caps.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "esp_clk_tree.h"
#include "esp_rom_crc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
namespace {
using namespace corese;
v12::TdPublication publication;Model* models=nullptr;DisplayMemory* memory=nullptr;uint16_t *frame=nullptr,*last=nullptr,*capture=nullptr;
TaskHandle_t task=nullptr;portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
struct Stats {uint64_t sampled=0,snap_model_us=0;uint32_t frames=0,errors=0,max_us=0,crc=0,touches=0,power_clicks=0,snap=0,rms=UINT32_MAX;int64_t offset=0;unsigned page=0,board=0,source=0,snap_page=0;bool ready=false,pulse=false,valid=false,precise=false,offset_valid=false,snap_offscreen=false;};
corese::SleepGate sleep_gate;
Stats stats;int requested_page=-1,preview_page=-1;bool snap_requested=false,snap_busy=false;
unsigned shot_offset=0;bool shot_response=false,snapshot_response=false,mesh_response=false;
uint32_t xtal_hz=0,cpu_hz=0;uint64_t now_us(){return uint64_t(esp_timer_get_time());}
void name(char* out,size_t size,uint64_t id){const char* known=ptob_registry_name(id);if(known)std::snprintf(out,size,"%s",known);else if(id)std::snprintf(out,size,"%04X",unsigned(id&65535));else std::snprintf(out,size,"UNKNOWN");}
unsigned page(const View& v){return v.screen==Screen::Launcher?0:v.screen==Screen::Clock?(v.face==Face::Instrument?1:7):2+unsigned(v.diagnostic);}
void select(View& v,unsigned p){v.pressed=Action::None;if(p==0)v.screen=Screen::Launcher;else if(p==1||p==7){v.screen=Screen::Clock;v.face=p==7?Face::UtcRegister:Face::Instrument;}else{v.screen=Screen::Diagnostics;v.diagnostic=mesh::Page(p-2);}}
void worker(void*){
 View v;Touch touch;Model m;
 for(;;){
  const auto begin=now_us();const int slot=publication.pin();if(slot>=0){m=models[slot];publication.release(slot);}
  int requested,preview;bool snap;portENTER_CRITICAL(&mux);requested=requested_page;requested_page=-1;preview=preview_page;preview_page=-1;snap=snap_requested||preview>=0;snap_requested=false;if(snap)snap_busy=true;portEXIT_CRITICAL(&mux);
  if(requested>=0)select(v,unsigned(requested));
  M5.update();
  if(M5.BtnPWR.wasClicked()){portENTER_CRITICAL(&mux);++stats.power_clicks;portEXIT_CRITICAL(&mux);}
  if(M5.BtnPWR.wasDoubleClicked())sleep_gate.request();
  if(sleep_gate.take_shutdown()){M5.Display.setBrightness(0);M5.Power.powerOff();for(;;)vTaskDelay(portMAX_DELAY);}
  const auto detail=M5.Touch.getDetail();const bool down=M5.Touch.getCount()>0;
  const auto action=touch.update(v,down,detail.x,detail.y);if(action!=Action::None){apply(v,action);portENTER_CRITICAL(&mux);++stats.touches;portEXIT_CRITICAL(&mux);}
  render(frame,m,v,begin);
  bool ok=true;
  if(std::memcmp(frame,last,FrameBytes)){
   M5.Display.pushImage(0,0,Width,Height,frame);std::memcpy(last,frame,FrameBytes);
  }
  if(snap){View pv=v;if(preview>=0){select(pv,unsigned(preview));render(capture,m,pv,begin);}else std::memcpy(capture,last,FrameBytes);const auto crc=esp_rom_crc32_le(0,reinterpret_cast<const uint8_t*>(capture),FrameBytes);portENTER_CRITICAL(&mux);++stats.snap;stats.crc=crc;stats.snap_page=page(pv);stats.snap_model_us=m.sampled_us;stats.snap_offscreen=preview>=0;snap_busy=false;portEXIT_CRITICAL(&mux);}
  uint64_t utc=0;const bool valid=time(m,begin,utc),precise=precision(m,begin),offset_valid=corese::offset(m,begin);
  const auto elapsed=uint32_t(now_us()-begin);portENTER_CRITICAL(&mux);stats.ready=ok;stats.frames++;stats.page=page(v);stats.sampled=m.sampled_us;stats.pulse=source_pulse(m,begin);stats.max_us=std::max(stats.max_us,elapsed);stats.valid=valid;stats.precise=precise;stats.offset_valid=offset_valid;stats.source=unsigned(m.source);stats.rms=m.rms_ns;stats.offset=m.offset_ns;portEXIT_CRITICAL(&mux);
  ulTaskNotifyTake(pdTRUE,pdMS_TO_TICKS(20));
 }
}
}
bool display_init(){
 using namespace corese;
 if(!esp_psram_is_initialized()||esp_psram_get_size()<8*1024*1024)return false;
 auto config=M5.config();config.internal_imu=config.external_imu=config.internal_rtc=config.external_rtc=config.internal_mic=config.internal_spk=false;config.output_power=false;config.pmic_button=true;config.fallback_board=m5::board_t::board_unknown;
 M5.begin(config);M5.BtnPWR.setHoldThresh(500);
 // Both family IDs use the same LCD/touch/MBus route. Preserve the actual
 // detected ID; factory binding remains the hardware ownership guard.
 const auto detected=M5.getBoard();stats.board=unsigned(detected);
 if(detected!=m5::board_t::board_M5StackCoreS3SE&&detected!=m5::board_t::board_M5StackCoreS3)return false;
 M5.Display.setRotation(1);if(M5.Display.width()!=Width||M5.Display.height()!=Height)M5.Display.setRotation(0);
 if(M5.Display.width()!=Width||M5.Display.height()!=Height||!M5.Touch.isEnabled())return false;
 M5.Display.setBrightness(150);M5.Display.setSwapBytes(true);
 frame=static_cast<uint16_t*>(heap_caps_malloc(FrameBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));last=static_cast<uint16_t*>(heap_caps_malloc(FrameBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));capture=static_cast<uint16_t*>(heap_caps_malloc(FrameBytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 models=static_cast<Model*>(heap_caps_calloc(3,sizeof(Model),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 memory=static_cast<DisplayMemory*>(heap_caps_malloc(sizeof(DisplayMemory),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));
 if(!frame||!last||!capture||!models||!memory||!owner::init())return false;
 new(memory)DisplayMemory;
 for(unsigned i=0;i<3;++i)new(models+i)Model;
 std::memset(last,0xff,FrameBytes);
 esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_XTAL,ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,&xtal_hz);esp_clk_tree_src_get_freq_hz(SOC_MOD_CLK_CPU,ESP_CLK_TREE_SRC_FREQ_PRECISION_CACHED,&cpu_hz);
 return xTaskCreatePinnedToCore(worker,"corese_ui",8192,nullptr,1,&task,1)==pdPASS;
}
void n30_display_publish(const v12::R2Status& s,const DisplayState& d,uint64_t now){
 using namespace corese;Model m;
 m.sampled_us=now;m.utc_at_us=s.follower_snapshot_us;m.utc_us=s.follower_utc_us;
 m.until_us=std::min(s.g1_anchor_deadline_us,s.g1_corrected_until_us);m.precision_until_us=m.until_us;
 m.root=s.root_identity;m.boot=s.root_boot;m.source_epoch=s.source_epoch;
 const bool healthy=s.requested&&!s.fault&&!s.rc7_upstream_mode_pending&&!s.rc_parent.pending()&&s.rc_parent.state!=4;
 m.utc_valid=healthy&&s.following&&s.clock_valid&&d.valid&&!s.g1_retained_estimate;
 m.state=s.fault?State::Fault:m.utc_valid?(d.r2_relative_lock?State::Locked:State::Acquire):s.group_member?State::Acquire:State::Search;
 m.source=m.utc_valid?(s.rc7_bound?(s.rc7_depth==2?Source::Relay2:Source::Relay1):Source::Grandmaster):Source::None;
 m.rms_ns=m.utc_valid?d.rms_ns:UINT32_MAX;m.precision_valid=m.utc_valid&&d.r2_relative_lock&&m.rms_ns!=UINT32_MAX;
 name(m.parent,sizeof m.parent,s.joined_peer);name(m.root_name,sizeof m.root_name,m.root);
 const auto rc9=v12::r2_rc9_status();
 if(!m.precision_valid&&healthy&&rc9.valid&&rc9.following&&!rc9.fault&&now>=rc9.sampled_us&&now-rc9.sampled_us<SnapshotMaxUs&&now<rc9.until_us){m.source=Source::Rc9;m.state=State::Acquire;m.utc_at_us=rc9.sampled_us;m.utc_us=rc9.utc_us;m.until_us=rc9.until_us;m.utc_valid=true;m.precision_valid=false;m.rms_ns=UINT32_MAX;m.root=rc9.root;m.boot=rc9.root_boot;m.source_epoch=rc9.source_epoch;name(m.parent,sizeof m.parent,rc9.server);name(m.root_name,sizeof m.root_name,m.root);}
 m.xtal_hz=xtal_hz;m.cpu_hz=cpu_hz;const auto fine=n30_fine_clock_status(now);m.counter_hz=n30_fine_backend_selected()?fine.hz:20000000;m.counter_healthy=!n30_fine_backend_selected()||(fine.active&&fine.healthy&&fine.production);
 m.psram_total=esp_psram_get_size();m.psram_free=heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
 m.rssi_valid=s.n30_signal.fresh(now);m.rssi_dbm=s.n30_signal.dbm;m.rssi_sampled_us=s.n30_signal.at;
 owner::Observation observations;
 if(owner::snapshot(observations,now)){m.mesh.roles=observations.roles;m.mesh.reference=observations.reference;name(m.mesh.reference.root_name,25,m.mesh.reference.scope.root);m.mesh.capabilities=observations.cap;auto& r=m.mesh.roles;name(r.primary_name,25,r.root);name(r.hot_name,25,r.hot);name(r.coordinator_name,25,r.coordinator);
  m.offset_ns=observations.offset;m.offset_sampled_us=observations.offset_at;m.offset_until_us=observations.offset_at+SnapshotMaxUs;m.offset_root=observations.offset_scope.root;m.offset_boot=observations.offset_scope.boot;m.offset_epoch=observations.offset_scope.epoch;m.offset_valid=observations.offset_valid;
 }
 auto& p=m.mesh.path;p.sampled_us=now;p.until_us=m.until_us;p.source_epoch=m.source_epoch;
 if(m.utc_valid&&m.source!=Source::Rc9){
  if(s.rc7_bound){uint8_t raw[89]{};bool valid=true;for(unsigned i=0;i<89;++i){unsigned value=0;if(std::sscanf(s.rc7_path_hex+i*2,"%2x",&value)!=1){valid=false;break;}raw[i]=uint8_t(value);}if(valid){p.count=raw[4]+1;if(p.count>3)p.count=0;for(unsigned i=0;i<p.count;++i){const unsigned at=5+28*i;p.nodes[i].id=r2_group_get(raw+at,8);p.nodes[i].boot=r2_group_get(raw+at+8,8);p.nodes[i].train=uint32_t(r2_group_get(raw+at+16,4));p.nodes[i].epoch=uint32_t(r2_group_get(raw+at+20,4));name(p.nodes[i].name,25,p.nodes[i].id);}}}
  else{p.count=1;p.nodes[0].id=m.root;p.nodes[0].boot=m.boot;p.nodes[0].train=s.publication;p.nodes[0].epoch=s.rx_capture.epoch;name(p.nodes[0].name,25,m.root);}p.known=p.authenticated=p.count>0;
 }
 auto& l=m.mesh.link;l.scope={m.root,m.boot,m.source_epoch};l.sampled_us=now;l.until_us=m.until_us;l.known=m.utc_valid;
 l.matched=s.g1_corrected_accepted;l.rejected=s.g1_corrected_refused;
 if(m.utc_valid&&m.source!=Source::Rc9){
  if(s.follower_received_us&&now>=s.follower_received_us)l.source_age_us=uint32_t(std::min<uint64_t>(now-s.follower_received_us,UINT32_MAX-1));
  if(m.until_us>now)l.source_ttl_us=uint32_t(std::min<uint64_t>(m.until_us-now,UINT32_MAX-1));
 }
 if(m.source==Source::Rc9){l.upstream_rms_known=rc9.metadata_known&&rc9.rms_ns!=UINT32_MAX;l.upstream_rms_ns=rc9.rms_ns;}
 memory->retain(m);const int slot=publication.reserve();if(slot>=0){models[slot]=m;publication.commit(slot);}
}
void n30_display_trace_poll(){}
void n30_display_status(v12::ConsoleFrame& f){Stats s;portENTER_CRITICAL(&mux);s=stats;portEXIT_CRITICAL(&mux);f.format("N30_UI profile=CORESE GPS=ABSENT page=%u ready=%u help=0 saver=0 blank=0 frames=%lu errors=%lu max_frame_us=%lu model_us=%llu rotation=LANDSCAPE display=320x240 hardware=LIVE path=CLIENT upstream=AUTO phase_us=0 m=0 snap=%lu crc=%08lx pulse=%u psram=%lu touches=%lu board=%u sleep=%u power_clicks=%lu clock=%u/%u/%u/%u/%lu/%lld\n",s.page,unsigned(s.ready),(unsigned long)s.frames,(unsigned long)s.errors,(unsigned long)s.max_us,(unsigned long long)s.sampled,(unsigned long)s.snap,(unsigned long)s.crc,unsigned(s.pulse),(unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned long)s.touches,s.board,unsigned(sleep_gate.stage()),(unsigned long)s.power_clicks,unsigned(s.valid),unsigned(s.precise),unsigned(s.offset_valid),s.source,(unsigned long)s.rms,(long long)s.offset);}
bool n30_display_page(unsigned p){if(p>7)return false;portENTER_CRITICAL(&mux);requested_page=int(p);portEXIT_CRITICAL(&mux);if(task)xTaskNotifyGive(task);return true;}
bool n30_display_take_upstream_toggle(uint64_t){return false;}bool n30_display_take_lora_toggle(uint64_t){return false;}bool n30_display_minimal(){return false;}
void display_show(const DisplayState&){}bool display_set_scan(DisplayTiming::Profile){return false;}void display_update_indicators(bool,bool,bool){}void display_sleep(){}void display_wake(){}void display_blank(){}
bool corese_display_command(const char* action){
 if(!std::strcmp(action,"mesh diag")){mesh_response=true;return true;}
 if(!std::strncmp(action,"ui render ",10)&&std::strlen(action)==11&&action[10]>='0'&&action[10]<='7'){portENTER_CRITICAL(&mux);preview_page=action[10]-'0';portEXIT_CRITICAL(&mux);snapshot_response=true;if(task)xTaskNotifyGive(task);return true;}
 if(!std::strcmp(action,"ui capture")){snapshot_response=true;return true;}
 if(!std::strcmp(action,"ui snap")){portENTER_CRITICAL(&mux);snap_requested=true;portEXIT_CRITICAL(&mux);snapshot_response=true;if(task)xTaskNotifyGive(task);return true;}
 unsigned offset=0;char tail=0;if(std::sscanf(action,"ui pixels %u %c",&offset,&tail)==1&&offset<corese::Pixels){shot_offset=offset;shot_response=true;return true;}return false;
}
bool corese_display_response_pending(){return shot_response||snapshot_response||mesh_response;}
void corese_display_response(v12::ConsoleFrame& f){
 if(mesh_response){corese::owner::Observation o;const auto now=now_us();corese::owner::snapshot(o,now);const auto& r=o.roles;const auto selected=v12::r2_status();
  f.format("CORESE_MESH known=%u live=%u now_us=%llu received_us=%llu until_us=%llu primary=%016llx boot=%016llx epoch=%lu created_us=%llu epoch_since_us=%llu hot=%016llx hot_boot=%016llx coordinator=%016llx primary_since_us=%llu hot_since_us=%llu sequence=%lu accepted=%lu bad_mac=%lu bad_scope=%lu expired=%lu replay=%lu changes=%lu scan_gate=%u scan_deadline_us=%llu scan_windows=%lu gd_known=%u gd_live=%u gd_root=%016llx gd_boot=%016llx gd_epoch=%lu gd_sequence=%lu gd_received_us=%llu gd_until_us=%llu gd_sampled_us=%llu gd_fields=%u gd_values=%u gd_sv=%u gd_source=%u gd_version=%u gd_hdop_centi=%u gd_ages_ms=%u/%u/%u gd_ptp=%u/%u/%u/%u/%u gd_accepted=%lu gd_bad_mac=%lu gd_bad_scope=%lu gd_expired=%lu gd_replay=%lu\n",unsigned(r.known),unsigned(corese::mesh::live(r,now)),(unsigned long long)now,(unsigned long long)r.received_us,(unsigned long long)r.until_us,(unsigned long long)r.root,(unsigned long long)r.root_boot,(unsigned long)r.epoch,(unsigned long long)r.created_us,(unsigned long long)r.epoch_since_us,(unsigned long long)r.hot,(unsigned long long)r.hot_boot,(unsigned long long)r.coordinator,(unsigned long long)r.primary_since_us,(unsigned long long)r.hot_since_us,(unsigned long)r.sequence,(unsigned long)r.accepted,(unsigned long)r.rejected_auth,(unsigned long)r.rejected_scope,(unsigned long)r.rejected_age,(unsigned long)r.replayed,(unsigned long)r.changes,unsigned(o.scan_allowed),(unsigned long long)o.scan_deadline,(unsigned long)o.scan_windows,unsigned(o.reference.reported),unsigned(corese::mesh::current(o.reference,{selected.root_identity,selected.root_boot,selected.source_epoch},now)),(unsigned long long)o.reference.scope.root,(unsigned long long)o.reference.scope.boot,(unsigned long)o.reference.scope.epoch,(unsigned long)o.reference.sequence,(unsigned long long)o.reference.received_us,(unsigned long long)o.reference.until_us,(unsigned long long)o.reference.remote_sampled_us,unsigned(o.reference.fields),unsigned(o.reference.values),unsigned(o.reference.sats),unsigned(o.reference.source_kind),unsigned(o.reference.wire_version),unsigned(o.reference.hdop_centi),unsigned(o.reference.rmc_age_ms),unsigned(o.reference.pps_age_ms),unsigned(o.reference.gga_age_ms),unsigned(o.reference.priority1),unsigned(o.reference.clock_class),unsigned(o.reference.accuracy),unsigned(o.reference.variance),unsigned(o.reference.priority2),(unsigned long)o.reference.accepted,(unsigned long)o.reference.rejected_auth,(unsigned long)o.reference.rejected_scope,(unsigned long)o.reference.rejected_age,(unsigned long)o.reference.replayed);
  mesh_response=false;return;
 }
 Stats s;bool busy;portENTER_CRITICAL(&mux);s=stats;busy=snap_busy||snap_requested||preview_page>=0;portEXIT_CRITICAL(&mux);
 if(shot_response&&!busy&&s.snap){static char hex[4097];const unsigned n=std::min<unsigned>(1024,corese::Pixels-shot_offset);static const char digits[]="0123456789abcdef";for(unsigned i=0;i<n;++i){const auto v=capture[shot_offset+i];for(unsigned j=0;j<4;++j)hex[i*4+j]=digits[v>>(12-j*4)&15];}hex[n*4]=0;f.format("CORESE_PIXELS snap=%lu crc=%08lx offset=%u count=%u rgb565=%s\n",(unsigned long)s.snap,(unsigned long)s.crc,shot_offset,n,hex);}
 else f.format("CORESE_SNAPSHOT snap=%lu busy=%u crc=%08lx width=320 height=240 page=%u scope=%s model_us=%llu\n",(unsigned long)s.snap,unsigned(busy),(unsigned long)s.crc,s.snap_page,s.snap_offscreen?"ONDEVICE_LIVE_MODEL_RENDER":"COMPLETED_LCD_FRAME",(unsigned long long)s.snap_model_us);
 shot_response=snapshot_response=false;
}

bool corese_display_take_sleep_request(){return sleep_gate.take_request();}
void corese_display_sleep_drain(bool ready,bool fault){sleep_gate.drained(ready,fault);}

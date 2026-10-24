# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""Generate a target-local overlay of selected public N30/W12 sources."""
import hashlib,json
from pathlib import Path
APP=Path(__file__).resolve().parents[1];REPO=APP.parents[1]
def prepare(out,source_root=None):
 out=Path(out);out.mkdir(parents=True,exist_ok=False);provenance={}
 root=Path(source_root) if source_root else REPO
 def load(path):
  p=root/path;provenance[path]=hashlib.sha256(p.read_bytes()).hexdigest();return p.read_text()
 def replace(s,old,new,count=1):
  if s.count(old)!=count:raise ValueError('Runtime overlay anchor changed: '+old[:90])
  return s.replace(old,new)
 main=load('firmware/n30/main/native_main.cpp')
 fine_boot='  if(n30_fine_backend_selected()&&!n30_fine_backend_start(session,fine_factory,micros())){\n    v12::fault_watchdog_fatal();std::printf("W12_V12_FATAL reason=FINE_COUNTER_DOMAIN\\n");return;}'
 main=replace(main,fine_boot,'')
 main=replace(main,'ui_ready=display_init();gps_ready=gnss_pps::init(clock_timer,pps);','ui_ready=display_init();gps_ready=gnss_pps::init(clock_timer,pps);\n'+fine_boot)
 main=replace(main,'bool update_ui=now-last_ui>=125000;','bool update_ui=now-last_ui>=50000;')
 main=replace(main,'#include "n30_console.hpp"','#include "corese_console.hpp"\n#include "corese_display.hpp"')
 main='extern "C" int __real_printf(const char*,...);\n'+main
 # Finite startup breadcrumbs use the existing IDF console before normal
 # bounded logging starts. They expose no keys or protocol packet bodies.
 main=replace(main,'extern "C" void app_main(){','extern "C" void app_main(){\n  __real_printf("CORESE_INIT stage=ENTER\\n");')
 main=replace(main,'const auto device=v12::native_device_binding();','__real_printf("CORESE_INIT stage=FACTORY\\n");\n  const auto device=v12::native_device_binding();')
 main=replace(main,'n30_timing_init();','__real_printf("CORESE_INIT stage=IDENTITY valid=%u\\n",unsigned(identity));\n  n30_timing_init();')
 main=replace(main,'gptimer_config_t timer{};','__real_printf("CORESE_INIT stage=WATCHDOG\\n");\n  gptimer_config_t timer{};')
 main=replace(main,'if(n30_fine_backend_selected()&&!n30_fine_backend_start(session,fine_factory,micros())){','__real_printf("CORESE_INIT stage=GPTIMER\\n");\n  if(n30_fine_backend_selected()&&!n30_fine_backend_start(session,fine_factory,micros())){\n    __real_printf("CORESE_INIT stage=FINE_FAILED\\n");')
 main=replace(main,'if(!n30_console::init()){','__real_printf("CORESE_INIT stage=FINE\\n");\n  if(!n30_console::init()){')
 main=replace(main,'if(!v12::log_start()){','__real_printf("CORESE_INIT stage=CONSOLE\\n");\n  if(!v12::log_start()){')
 main=replace(main,'ui_ready=display_init();gps_ready=gnss_pps::init(clock_timer,pps);','__real_printf("CORESE_INIT stage=DISPLAY_BEGIN\\n");\n  ui_ready=display_init();__real_printf("CORESE_INIT stage=DISPLAY_END ready=%u\\n",unsigned(ui_ready));gps_ready=gnss_pps::init(clock_timer,pps);')
 main=replace(main,'v12::fault_watchdog_host_expected();','__real_printf("CORESE_INIT stage=RUN\\n");\n  v12::fault_watchdog_host_expected();')
 main=replace(main,'std::printf("N30_TARGET version=0.2.24 profile=RC GPS=ABSENT flash_mb=8 console=UART0_43_44 hardware=UNVALIDATED\\n");','std::printf("CORESE_TARGET version=0.1.11 profile=CLIENT GPS=ABSENT flash_mb=16 console=USB_SERIAL_JTAG relay=FORBIDDEN hardware=UNQUALIFIED\\n");')
 main=replace(main,'gpio_config_t button{};button.pin_bit_mask=1;button.mode=GPIO_MODE_INPUT;button.pull_up_en=GPIO_PULLUP_ENABLE;\n  if(gpio_config(&button)!=ESP_OK){v12::fault_watchdog_fatal();std::printf("W12_V12_FATAL reason=BUTTON\\n");return;}','// CoreS3-SE touch is the sole presentation input; do not configure GPIO0.')
 anchor='else if(!std::strcmp(action,"ui page 0")||!std::strcmp(action,"ui page 1")||!std::strcmp(action,"ui page 2")||!std::strcmp(action,"ui page 3"))'
 main=replace(main,anchor,'else if(!std::strncmp(action,"ui page ",8)&&std::strlen(action)==9&&action[8]>=\'0\'&&action[8]<=\'7\')')
 main=replace(main,'else if(!std::strcmp(action,"lora diag"))','else if(corese_display_command(action)){last_command={nonce,admitted,true,"CORESE_UI","DISPLAY_ONLY"};}\n  else if(!std::strcmp(action,"lora diag"))')
 main=replace(main,'void respond_status(uint64_t nonce,uint64_t begin){if(', 'void respond_status(uint64_t nonce,uint64_t begin){if(corese_display_response_pending()){const auto next=ticket.issue(begin);v12::frame_begin(response,nonce,next,begin,factory_text,session,V12_SOURCE_REVISION,V12_BUILD_ID);corese_display_response(response);v12::frame_submit_corese(response);return;}if(')
 main=replace(main,'    poll_update_drain(now);','    if(corese_display_take_sleep_request()){const bool ok=update_gate.begin(now);if(ok){stop_all();v12::capture_stop();}else corese_display_sleep_drain(update_gate.stage==v12::UpdateStage::Ready,update_gate.stage!=v12::UpdateStage::Ready); }\n    poll_update_drain(now);\n    corese_display_sleep_drain(update_gate.stage==v12::UpdateStage::Ready,update_gate.stage==v12::UpdateStage::Fault);')
 (out/'native_main.cpp').write_text(main)
 log=load('firmware/n30/main/bounded_log.cpp').replace('#include "n30_console.hpp"','#include "corese_console.hpp"')
 log+='\nnamespace v12 {\nbool frame_submit_corese(const ConsoleFrame& f){\n portENTER_CRITICAL(&log_mux);\n const bool accepted=!f.failed&&f.rows==1&&now_us()<f.deadline_us&&frames.push(f);\n if(!accepted)frame_drop();\n portEXIT_CRITICAL(&log_mux);return accepted;\n}\n}\n'
 (out/'bounded_log.cpp').write_text(log)
 native=load('firmware/w12/main/native_g1.cpp')
 native=replace(native,'#include "native_r2.hpp"','#include "native_r2.hpp"\n#include "mesh_owner.hpp"\n#include "status_scan.hpp"')
 native=replace(native,'void start_scan(bool gm_observation=false){','corese::StatusScan corese_status_scan;\nvoid start_scan(bool gm_observation=false){')
 native=replace(native,'if(!timely()||(!gm_observation&&!relay_slot_scanning()&&selection_protected(micros()))','if(!timely()||(!gm_observation&&!relay_slot_scanning()&&!corese_status_scan.allows(micros())&&selection_protected(micros()))')
 native=replace(native,'const bool protected_parent=selection_protected(micros())&&!gm_observation;','const bool protected_parent=selection_protected(micros())&&!gm_observation;\n  const auto status_now=micros();\n  const bool status_eligible=protected_parent&&full_listening()&&peer.handle==None&&!peer.connecting&&!sync_cleanup_queued&&!sync_terminating&&lock_protected(status_now)&&status_now<=UINT64_MAX-200000&&corrected_clock.covers(status_now,status_now+200000);\n  corese_status_scan.poll(status_now,status_eligible);\n  corese::owner::scan_state(corese_status_scan.allows(status_now),corese_status_scan.deadline(),corese_status_scan.windows(),status_now);')
 native=replace(native,'if(protected_parent&&!relay_slot_scanning()){','if(protected_parent&&!relay_slot_scanning()&&!corese_status_scan.allows(micros())){')
 native=replace(native,'if(protected_parent&&!relay_slot_scanning())continue;','if(protected_parent&&!relay_slot_scanning()&&!corese_status_scan.observed(d.at))continue;')
 native=replace(native,'if(independent_scan&&!protected_parent&&!peer.connecting&&!scan&&(!synced||due||gm_observation))start_scan(gm_observation);','if(independent_scan&&(!protected_parent||corese_status_scan.allows(micros()))&&!peer.connecting&&!scan&&(!synced||due||gm_observation||corese_status_scan.allows(micros())))start_scan(gm_observation);')
 native=replace(native,'if(!relay_slot_scanning()&&(protected_parent||(synced&&!due&&!gm_observation)))stop_scan();','if(!corese_status_scan.allows(micros())&&!relay_slot_scanning()&&(protected_parent||(synced&&!due&&!gm_observation)))stop_scan();')
 # Observe GS during the receiver's ordinary discovery windows. Preserve the
 # original protected-parent scan policy; continuous background scanning can
 # contaminate the fine receiver's candidate edge population.
 native=replace(native,'void observe_group(const Discovery& d){','void observe_group(const Discovery& d){\n  corese::owner::advertisement(d.bytes,d.length,d.sid,d.address.val,{anchor.root,anchor.boot,anchor.source_epoch},provision.group,d.at,micros());')
 native=replace(native,'inc(join_accepts);inc(diagnostic.peer(r.sender).accepts);','corese::owner::capabilities(r.sender,r.sender_boot,r.capabilities,row.at,peer.exchange.until);\n    inc(join_accepts);inc(diagnostic.peer(r.sender).accepts);')
 (out/'native_g1.cpp').write_text(native)
 rate=load('firmware/n30/main/relative_rate.cpp')
 rate='#define n30_relative_rate_pair corese_relative_rate_base\n'+rate+'\n#undef n30_relative_rate_pair\n#include "mesh_owner.hpp"\nvoid n30_relative_rate_pair(const v12::n30_probe::Pair& p,uint64_t now){corese_relative_rate_base(p,now);corese::owner::residual(p,now);}\n'
 (out/'relative_rate.cpp').write_text(rate)
 fine=load('firmware/n30/main/fine_capture.cpp')
 fine=replace(fine,'#include "fine_capture.hpp"','#include "fine_capture.hpp"\n#include "esp_attr.h"')
 fine=replace(fine,'bool latch(uint32_t& raw,LatchStats* attempt=nullptr){','IRAM_ATTR bool latch(uint32_t& raw,LatchStats* attempt=nullptr){')
 (out/'fine_capture.cpp').write_text(fine)
 cm=load('firmware/n30/main/CMakeLists.txt')
 cm=cm.replace('set(w12 "${CMAKE_CURRENT_LIST_DIR}/../../w12")','set(w12 "${CMAKE_CURRENT_LIST_DIR}/../../w12")\nset(n30 "${CMAKE_CURRENT_LIST_DIR}/../../n30")')
 cm=cm.replace('"${w12}/main/native_g1.cpp"','"${CORESE_RUNTIME}/native_g1.cpp"')
 for name in ['native_main.cpp','bounded_log.cpp','relative_rate.cpp']:cm=cm.replace('"'+name+'"','"${CORESE_RUNTIME}/'+name+'"')
 cm=cm.replace('"native_lora.cpp" "lora_core.c" "lora_diagnostics.cpp"','"lora_absent.cpp"')
 cm=cm.replace('"${w12}/../t096/protocol/lora_rc8.c" "${w12}/../t096/src/lora_rc8_policy.c"','"${w12}/../t096/protocol/role_announcement.c" "${w12}/../t096/protocol/gnss_diagnostics.c"')
 cm=cm.replace('"gnss_absent.cpp" "display.cpp" "ui_render.cpp" "ui_model.cpp" "timing_diagnostics.cpp" "relative_rate.cpp" "fine_capture.cpp" "window_diagnostics.cpp"','"${n30}/main/gnss_absent.cpp" "display.cpp" "ui.cpp" "mesh_diagnostics.cpp" "mesh_wire.cpp" "mesh_owner.cpp" "${n30}/main/timing_diagnostics.cpp" "${CORESE_RUNTIME}/relative_rate.cpp" "${n30}/main/fine_capture.cpp" "${n30}/main/window_diagnostics.cpp"')
 # previous replacement of relative_rate already expanded that token
 cm=cm.replace('"gnss_absent.cpp" "display.cpp" "ui_render.cpp" "ui_model.cpp" "timing_diagnostics.cpp" "${CORESE_RUNTIME}/relative_rate.cpp" "fine_capture.cpp" "window_diagnostics.cpp"','"${n30}/main/gnss_absent.cpp" "display.cpp" "ui.cpp" "mesh_diagnostics.cpp" "mesh_wire.cpp" "mesh_owner.cpp" "${n30}/main/timing_diagnostics.cpp" "${CORESE_RUNTIME}/relative_rate.cpp" "${n30}/main/fine_capture.cpp" "${n30}/main/window_diagnostics.cpp"')
 cm=cm.replace('INCLUDE_DIRS "."','INCLUDE_DIRS "${CORESE_APP}/main" "${n30}/main"')
 cm=cm.replace('REQUIRES bt mbedtls','REQUIRES M5Unified esp_psram bt mbedtls')
 cm=cm.replace(' "${w12}/../t096/include"','')
 cm=cm.replace('V12_UNIT="N30"','V12_UNIT="CORESE"').replace('W12_UNIT_NAME="PToB N30"','W12_UNIT_NAME="PToB CoreS3-SE"')
 cm=cm.replace('PTOB_N30_LORA_RX_ONLY=1','V12_LORA_FORCED_OFF=1')
 cm=cm.replace('${CMAKE_CURRENT_LIST_DIR}/../../w12','${CORESE_APP}/../w12').replace('${CMAKE_CURRENT_LIST_DIR}/../../n30','${CORESE_APP}/../n30')
 for name in ['lora_absent.cpp','display.cpp','ui.cpp','mesh_diagnostics.cpp','mesh_wire.cpp','mesh_owner.cpp']:cm=cm.replace('"'+name+'"','"${CORESE_APP}/main/'+name+'"')
 cm+='\ntarget_compile_definitions(${COMPONENT_LIB} PRIVATE PTOB_CORESE=1)\n'
 cm=cm.replace('"${n30}/main/fine_capture.cpp"','"${CORESE_RUNTIME}/fine_capture.cpp"')
 (out/'live.cmake').write_text(cm)
 (out/'overlay-provenance.json').write_text(json.dumps({'schema':1,'origin':'Selected current public repository sources; target-local overlays only','sources':provenance,'upstream_edited':False},indent=2)+'\n')
 return provenance

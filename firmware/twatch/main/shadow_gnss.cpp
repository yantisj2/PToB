// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "shadow_gnss.hpp"
#include "watch.hpp"
#include "shadow_nmea.hpp"
#include "fine_capture.hpp"
#include "board.hpp"
#include "g1_pps_diagnostic.hpp"
#include "bounded_log.hpp"
#include "driver/uart.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include <new>
namespace watchshadow {
namespace {
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
watch::ShadowView shown;
bool wanted=false,rail=false,rail_transition=false,inhibited=false,hardware=false,uart_owned=false,capture_owned=false;
uint32_t generation=0,observed_generation=0;int power_error=0,cleanup_error=0,start_error=0;
uint64_t boot_id=0,factory_id=0;bool (*read_clock)(uint64_t*)=nullptr;
struct State {
 watch::ShadowStatistics stats;watch::ShadowNmea nmea;v12::G1PpsDiagnostic witness;
 watch::ShadowView view;
 uint64_t root=0,boot=0,last_pps=0;uint32_t source=0,train=0,scope=0,incarnation=0,population=1;
 bool had_fix=false,mesh=false;
};
static_assert(sizeof(State)<12288,"Bounded PSRAM-only GNSS diagnostic workspace");
State* state=nullptr;bool selected_ble=false;uint64_t selected_until=0;
watch::Source selected_source=watch::Source::None;
bool retire(){
 bool ok=true;
 if(capture_owned){if(watch_pps_stop())capture_owned=false;else{if(!cleanup_error)cleanup_error=1;ok=false;}}
 if(uart_owned){if(uart_driver_delete(UART_NUM_1)==ESP_OK)uart_owned=false;else{if(!cleanup_error)cleanup_error=2;ok=false;}}
 portENTER_CRITICAL(&mux);hardware=capture_owned||uart_owned;portEXIT_CRITICAL(&mux);
 return ok&&!hardware;
}
bool start(uint64_t now){
 if(!state){const auto p=heap_caps_malloc(sizeof(State),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!p){start_error=1;return false;}state=new(p) State;}
 else {state->stats.reset();new(&state->nmea) watch::ShadowNmea;new(&state->witness) v12::G1PpsDiagnostic;state->view={};state->root=state->boot=state->last_pps=0;state->source=state->train=state->scope=state->incarnation=0;state->population=1;state->had_fix=false;}
 uart_config_t config{};config.baud_rate=38400;config.data_bits=UART_DATA_8_BITS;config.parity=UART_PARITY_DISABLE;config.stop_bits=UART_STOP_BITS_1;config.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;config.source_clk=UART_SCLK_XTAL;
 // GPIO44 is MCU RX, GPIO43 MCU TX. RX-only: no receiver configuration/aid.
 if(uart_is_driver_installed(UART_NUM_1)||uart_param_config(UART_NUM_1,&config)!=ESP_OK||
    uart_set_pin(UART_NUM_1,UART_PIN_NO_CHANGE,44,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)!=ESP_OK||
    uart_driver_install(UART_NUM_1,2048,0,0,nullptr,0)!=ESP_OK){start_error=2;return false;}
 uart_owned=true;portENTER_CRITICAL(&mux);hardware=true;portEXIT_CRITICAL(&mux);
 // Retain partial-start ownership until its exact stop path proves release.
 capture_owned=true;
 if(!watch_pps_start(boot_id,factory_id,now)){start_error=3;retire();return false;}
 return true;
}
[[gnu::noinline]] void observe(uint64_t now){
 state->mesh=false;
 if(!selected_ble||now>=selected_until){state->stats.pause();return;}
 uint64_t before=0,after=0;if(!read_clock||!read_clock(&before)){state->stats.withdraw();return;}
 const auto status=v12::r2_status();if(!read_clock(&after)){state->stats.withdraw();return;}
 state->witness.observe(status,before,after,now);
 state->mesh=v12::r2_clock_witness(status)&&status.following&&status.g1_anchor_deadline_us>now&&status.g1_corrected_until_us>now&&status.service_until_us>now;
 if(state->root!=status.g1_clock_origin||state->boot!=status.root_boot||state->source!=status.source_epoch||state->train!=status.publication||state->scope!=status.g1_relative_quality.scope_generation){
  state->root=status.g1_clock_origin;state->boot=status.root_boot;state->source=status.source_epoch;state->train=status.publication;state->scope=status.g1_relative_quality.scope_generation;
  state->stats.withdraw();if(state->population<UINT32_MAX)++state->population;else start_error=5;
 }
 if(!state->mesh)state->stats.pause();
}
}
void init(uint64_t boot,uint64_t factory,bool (*reader)(uint64_t*)){boot_id=boot;factory_id=factory;read_clock=reader;}
void visible(bool on){
 portENTER_CRITICAL(&mux);on=on&&!inhibited;
 const bool demand_change=on!=wanted;if(demand_change){wanted=on;++generation;}
 const bool transition=on!=rail;
 rail_transition=transition;
 if(demand_change||transition){shown.valid=false;shown.samples=shown.settling_ms=0;}
 portEXIT_CRITICAL(&mux);
 if(!transition)return;
 const bool ok=watchboard::gnss_power(on);
 portENTER_CRITICAL(&mux);
 if(ok)rail=on;else {rail=true;power_error=1;wanted=false;inhibited=true;++generation;}
 rail_transition=false;
 portEXIT_CRITICAL(&mux);
}
void selected(const watch::Model& model,uint64_t now){
 const bool allowed=watch::shadow_comparison_allowed(model,now);
 // A different selected path starts a new population. A temporary permission
 // lapse on the same path hides the population without deleting good samples.
 const bool changed=model.source!=selected_source;
 if(changed&&state)state->stats.withdraw();
 selected_source=model.source;
 if(changed||allowed!=selected_ble||(selected_ble&&now>=selected_until)){
  if(state)state->stats.pause();
  portENTER_CRITICAL(&mux);shown.valid=false;shown.samples=shown.settling_ms=0;
  if(shown.fix&&shown.pps)shown.stage=watch::ShadowStage::WaitMesh;
  portEXIT_CRITICAL(&mux);
 }
 selected_ble=allowed;selected_until=allowed?std::min(model.until_us,model.sampled_us+250000):0;
}
void poll(uint64_t now){
 portENTER_CRITICAL(&mux);const bool run=wanted&&rail&&!inhibited&&!power_error;const bool powered=rail,pending=rail_transition;const auto gen=generation;portEXIT_CRITICAL(&mux);
 // The UI owns the bounded PMU transaction. Do not consume its new generation
 // before the readback acknowledgment; main can preempt either I2C operation.
 if(pending){portENTER_CRITICAL(&mux);shown={};shown.sampled_us=now;shown.powered=rail;shown.stage=wanted?watch::ShadowStage::Starting:watch::ShadowStage::Off;portEXIT_CRITICAL(&mux);return;}
 if(gen!=observed_generation){
  if(!retire()){portENTER_CRITICAL(&mux);inhibited=true;wanted=false;shown.valid=false;shown.samples=0;shown.stage=watch::ShadowStage::Fault;portEXIT_CRITICAL(&mux);return;}
  observed_generation=gen;if(run&&!cleanup_error)start_error=0;
  if(cleanup_error){portENTER_CRITICAL(&mux);inhibited=true;wanted=false;portEXIT_CRITICAL(&mux);}
  else if(run&&!start(now)){if(!start_error)start_error=4;portENTER_CRITICAL(&mux);inhibited=true;wanted=false;++generation;portEXIT_CRITICAL(&mux);}
 }
 watch::ShadowView view;view.sampled_us=now;view.powered=powered;
 if(!run||start_error||cleanup_error){view.stage=power_error||start_error||cleanup_error?watch::ShadowStage::Fault:watch::ShadowStage::Off;
  portENTER_CRITICAL(&mux);shown=view;portEXIT_CRITICAL(&mux);return;}
 if(!state||!hardware){view.stage=watch::ShadowStage::Starting;portENTER_CRITICAL(&mux);shown=view;portEXIT_CRITICAL(&mux);return;}
 if(!watch_pps_healthy()){start_error=6;retire();portENTER_CRITICAL(&mux);inhibited=true;wanted=false;++generation;shown={};shown.sampled_us=now;shown.stage=watch::ShadowStage::Fault;portEXIT_CRITICAL(&mux);return;}
 observe(now);
 // Bounded 512-byte UART budget per main iteration; queue overflow withdraws
 // reference coherence instead of presenting old reception as a current fix.
 uint8_t bytes[256];size_t buffered=0;
 if(uart_get_buffered_data_len(UART_NUM_1,&buffered)!=ESP_OK){
  if(!start_error){start_error=7;}retire();portENTER_CRITICAL(&mux);inhibited=true;wanted=false;++generation;shown={};shown.sampled_us=now;shown.stage=watch::ShadowStage::Fault;portEXIT_CRITICAL(&mux);return;
 }
 if(buffered>1536){new(&state->nmea) watch::ShadowNmea;state->stats.withdraw();uart_flush_input(UART_NUM_1);}
 for(unsigned i=0;i<2;++i){const int n=uart_read_bytes(UART_NUM_1,bytes,sizeof bytes,0);if(n<=0)break;uint64_t tick=0;if(read_clock)read_clock(&tick);state->nmea.feed(bytes,unsigned(n),now,tick);}
 state->nmea.apply(view,now);
 if(!state->nmea.reference(now)){state->stats.withdraw();}
 WatchPpsCapture edge;
 for(unsigned i=0;i<4&&watch_pps_take(edge,read_clock);++i){
  view.edges=edge.number;
  state->last_pps=edge.seen_us;
  const auto score=state->witness.edge(edge.tick20,edge.number,edge.enclosure_ns,edge.valid,
    {state->nmea.rmc20(),state->nmea.second(),gen?gen:1,state->nmea.reference(now)});
  const auto& map=score.map;
  if(state->incarnation!=edge.incarnation){
   state->incarnation=edge.incarnation;
   if(state->population<UINT32_MAX)++state->population;else {start_error=5;break;}
  }
  const bool same=state->root==map.root&&state->boot==map.boot&&state->source==map.source&&state->train==map.train&&state->scope==map.scope;
  const bool usable=selected_ble&&now<selected_until&&state->mesh&&same&&score.map_valid&&score.row.reference&&score.row.following&&score.row.permission&&view.fix;
  state->stats.edge(edge.seen_us,edge.number,state->population,usable,v12::G1PpsDiagnostic::phase(score.utc_ns));
  state->view.edges=edge.number;
 }
 view.edges=state->view.edges;view.last_pps_us=state->last_pps;
 view.pps=state->last_pps&&now>=state->last_pps&&now-state->last_pps<=1500000;
 if(!view.pps)state->stats.withdraw();
 state->stats.apply(view,now);
 view.valid=view.valid&&selected_ble&&now<selected_until&&state->mesh;
 if(!view.fix)view.stage=state->had_fix?watch::ShadowStage::Lost:watch::ShadowStage::Searching;
 else if(!view.pps)view.stage=watch::ShadowStage::WaitPps;
 else if(!state->stats.tracking())view.stage=watch::ShadowStage::WaitMesh;
 else view.stage=state->stats.settled()?watch::ShadowStage::Measuring:watch::ShadowStage::Settling;
 state->had_fix|=view.fix;
 portENTER_CRITICAL(&mux);
 // A UI withdrawal can race UART work. Never republish a live old generation.
 if(gen==generation&&wanted&&!inhibited)shown=view;
 else {shown={};shown.sampled_us=now;shown.powered=rail;}
 portEXIT_CRITICAL(&mux);
}
void stop(){portENTER_CRITICAL(&mux);inhibited=true;if(wanted){wanted=false;++generation;}portEXIT_CRITICAL(&mux);retire();}
ControlProof control(){portENTER_CRITICAL(&mux);ControlProof p{wanted,rail,rail_transition,hardware,uart_owned,capture_owned,!wanted&&!rail&&!rail_transition&&!hardware&&!cleanup_error,cleanup_error!=0};portEXIT_CRITICAL(&mux);return p;}
bool released(){return control().off;}
watch::ShadowView snapshot(){portENTER_CRITICAL(&mux);const auto s=shown;portEXIT_CRITICAL(&mux);return s;}
void status(v12::ConsoleFrame& frame){
 const auto s=snapshot();
 const auto p=control();
 frame.format(" shadow_version=1 shadow_powered=%u shadow_owner=%u shadow_stage=%u shadow_fix=%u shadow_pps=%u shadow_valid=%u\n",unsigned(p.powered),unsigned(p.owner),unsigned(s.stage),unsigned(s.fix),unsigned(s.pps),unsigned(s.valid));
}
void detail(v12::ConsoleFrame& frame){
 const auto s=snapshot();portENTER_CRITICAL(&mux);const bool request=wanted,powered=rail,owner=hardware;const auto gen=generation;const int power=power_error,cleanup=cleanup_error;portEXIT_CRITICAL(&mux);
 frame.format("TWATCH_SHADOW version=1 shadow_requested=%u shadow_powered=%u shadow_owner=%u shadow_generation=%u shadow_stage=%u shadow_fix=%u shadow_used=%u shadow_view=%u shadow_pps=%u shadow_edges=%u shadow_settling_ms=%u shadow_samples=%u shadow_mean_tenth_ns=%lld shadow_p99_abs_ns=%u shadow_rms_ns=%u shadow_approximate=%u shadow_skipped=%u shadow_missing=%u shadow_valid=%u shadow_power_error=%d shadow_start_error=%d shadow_cleanup_error=%d shadow_capture=MCPWM1_CH1_GPIO13 shadow_mapping=ENCLOSED20 shadow_GNSS_aid=NONE shadow_clock_training=NONE shadow_association=EMPIRICAL_UNCALIBRATED shadow_UTC_bound=UNKNOWN shadow_precision=UNQUALIFIED",
  unsigned(request),unsigned(powered),unsigned(owner),unsigned(gen),unsigned(s.stage),unsigned(s.fix),unsigned(s.used),unsigned(s.in_view),unsigned(s.pps),unsigned(s.edges),unsigned(s.settling_ms),unsigned(s.samples),(long long)s.mean_tenth_ns,unsigned(s.p99_ns),unsigned(s.rms_ns),unsigned(s.approximate),unsigned(s.skipped),unsigned(s.missing),unsigned(s.valid),power,start_error,cleanup);
 frame.format(" shadow_uart_bytes=%u shadow_nmea_sentences=%u shadow_nmea_errors=%u shadow_nmea_recent=%u shadow_reference=%u\n",
  state?unsigned(state->nmea.bytes()):0,state?unsigned(state->nmea.sentences()):0,state?unsigned(state->nmea.errors()):0,unsigned(state&&request&&powered&&state->nmea.packets(s.sampled_us)),unsigned(state&&request&&powered&&state->nmea.reference(s.sampled_us)));

}
}

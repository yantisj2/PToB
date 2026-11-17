// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Watch-only RX17 owner: finite shadow or explicitly reviewed relative G1 input.
#include "fine_capture.hpp"
#include "fine_counter.hpp"
#include "fine_receiver.hpp"
#include "fine_backend_api.hpp"
#include "periodic_rx_model.hpp"
#include "fine_capture_config.h"
#ifndef PTOB_N30_FINE_RW
#define PTOB_N30_FINE_RW 0
#endif
#ifndef PTOB_N30_FINE_BACKEND
#define PTOB_N30_FINE_BACKEND 0
#endif
#ifndef PTOB_N30_FINE_TRANSLATION80
#define PTOB_N30_FINE_TRANSLATION80 UINT64_C(0)
#endif
#include "bounded_log.hpp"
#include "driver/mcpwm_cap.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "esp_timer.h"
#include "esp_cpu.h"
#include "freertos/FreeRTOS.h"
#include "mcpwm_private.h"
#include "soc/gpio_sig_map.h"
#include "soc/mcpwm_periph.h"
#include "hal/gpio_ll.h"
#include <cinttypes>
namespace {
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
mcpwm_cap_timer_handle_t timer=nullptr;mcpwm_cap_channel_handle_t channels[3]{};
n30::fine::Counter counter;n30::fine::History history;
n30::fine::Receiver receiver;bool production_owned=false;
struct BackendStats {uint64_t fall80=0,stamp20=0,available20=0;uint32_t calls=0,pending=0,missing=0,ambiguous=0,unsupported=0;};
BackendStats backend_stats{};
// Production owns this peripheral for the whole boot and refuses every finite
// WINDOW start. Do not reserve the separate observer's 64 edges/128 fences in
// that mutually exclusive image: the BLE controller shares the internal heap.
#if PTOB_N30_FINE_BACKEND
class InactivePacketWindow {
 public:
 void reset(const n30::fine::Bridge&){}
 bool begin(uint32_t,uint64_t,uint64_t,const n30::fine::History&,uint32_t){return false;}
 bool skip(uint32_t){return false;}
 void arm(uint64_t){}
 void observe(const n30::fine::Edge&){}
 void seal(uint64_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t){}
 void withdraw(){}
 n30::fine::WindowStatus status()const{return {};}
 unsigned read(uint32_t,n30::fine::Edge*,unsigned)const{return 0;}
 unsigned late(uint32_t,n30::fine::Window::Late*,unsigned)const{return 0;}
};
InactivePacketWindow packet_edges;
static_assert(sizeof(packet_edges)==1,"Production must not reserve finite WINDOW storage");
#else
n30::fine::Window packet_edges;
#endif
bool window_owned=false;
n30::fine::Edge batch[4];
struct Snapshot {uint64_t id,until,latest;uint32_t every,seen,rx,tx,sampled,overwritten,oldest,incarnation,hz,current,timeout,invalid,poll,cycles,wall,service,first32,latest32,wraps,callbacks,budget_refused,expired_refused,inactive_callbacks,rx_out,tx_out,rx_in,tx_in,oe,channel_mask,irq_mask;uint64_t first_us,last_us;int error;bool active,contained;};
Snapshot snapshot{};
struct Probe {bool synthetic=false;uint32_t spacing_ms=0,goal=0,attempted=0,issued[2]{},rise[2]{},fall[2]{},initial_low=0,verified_levels=0,level_poll_max=0;uint64_t next_us=0,last_issue_us=0,stopped_us=0;};
Probe probe{},probe_snapshot{};uint32_t input_levels_snapshot=0;
struct LatchStats {uint32_t reads=0,timeouts=0,poll_max=0,cycles_max=0,wall_max_us=0;};
LatchStats last_start{},start_snapshot{};bool last_start_synthetic=false,start_synthetic_snapshot=false;

uint32_t incarnation=0,hz=0,current_reads=0,timeouts=0,invalid=0,poll_max=0,cycles_max=0,wall_max_us=0,service_max_us=0;
uint32_t latest_raw32=0,first_raw32=0,wraps=0;
uint64_t first_coarse=0,last_service=0,last_raw80=0;int error=0;
bool allocated=false,active=false,timer_enabled=false,timer_started=false,routed=false;
bool channel_enabled[3]{};uint64_t burst_at=0;uint32_t burst_count=0,total_callbacks=0,budget_refused=0,expired_refused=0,inactive_callbacks=0;bool contained=false;
bool pps_enabled=false,pps_clock_owned=false;
struct PpsRaw {uint32_t raw=0,number=0;uint64_t seen=0;};
PpsRaw pps_queue[4]{};unsigned pps_head=0,pps_count=0;uint32_t pps_sequence=0,pps_burst=0;uint64_t pps_burst_at=0;
constexpr unsigned RadioChannels=1;
constexpr uint32_t CurrentMask=UINT32_C(1)<<29,RadioMask=UINT32_C(1)<<27;
static_assert(!PTOB_N30_FINE_BACKEND||(PTOB_N30_FINE_TRANSLATION80>0&&PTOB_N30_FINE_TRANSLATION80<=248000&&PTOB_N30_FINE_TRANSLATION80%4==0),"Watch fine input requires explicit reviewed watch phase convention");
static_assert(!PTOB_N30_FINE_PINS||(PTOB_N30_FINE_RX_GPIO==17&&PTOB_N30_FINE_TX_GPIO==-1&&PTOB_N30_FINE_RW),"Watch shadow owns only reviewed microphone CLK GPIO17");
static_assert(SYNCFOUND_FLAG_IDX==230&&PKT_TX_ON_IDX==235&&RW_RX_ON_IDX==238&&RW_TX_ON_IDX==237,"Pinned native signal IDs changed");
static_assert(PTOB_N30_FINE_RW==0||(PTOB_N30_FINE_RW==1&&PTOB_N30_FINE_PINS),"RW profile requires reviewed fixed pads");
static_assert(!PTOB_N30_FINE_BACKEND||(PTOB_N30_FINE_RW&&PTOB_N30_FINE_PINS),"Fine backend requires the reviewed fixed RW profile");
constexpr int NativeRxSignal=PTOB_N30_FINE_RW?RW_RX_ON_IDX:SYNCFOUND_FLAG_IDX;
constexpr const char* NativeMode=PTOB_N30_FINE_BACKEND?"TWATCH_NATIVE_RW_RX17_G1":PTOB_N30_FINE_PINS?"TWATCH_NATIVE_RW_RX17_SHADOW":"CURRENT_ONLY";
constexpr const char* NativeRxName="RW_RX_ON_238_GPIO17";
constexpr const char* NativeTxName="NONE_CONSTANT0";
uint64_t coarse(){const auto t=esp_timer_get_time();return t>0?uint64_t(t):0;}
void inc(uint32_t& x){if(x!=UINT32_MAX)++x;}
// Pinned private ABI/register adapter; not a stable public IDF API.
// Caller holds our sole cross-core lock; CH2 has no ISR/callback. Clear only2.
bool latch(uint32_t& raw,LatchStats* attempt=nullptr){
 auto& reads=attempt?attempt->reads:current_reads;auto& timeout_count=attempt?attempt->timeouts:timeouts;
 auto& poll_peak=attempt?attempt->poll_max:poll_max;auto& cycle_peak=attempt?attempt->cycles_max:cycles_max;
 auto& wall_peak=attempt?attempt->wall_max_us:wall_max_us;
 if(!timer_enabled||!timer_started||!channel_enabled[2]||timer->fsm!=MCPWM_CAP_TIMER_FSM_ENABLE||
    channels[2]->fsm!=MCPWM_CAP_CHAN_FSM_ENABLE){error=-12;return false;}
 auto* dev=timer->group->hal.dev;
 if((!PTOB_N30_FINE_PINS&&(dev->cap_chn_cfg[0].capn_en||(!pps_enabled&&dev->cap_chn_cfg[1].capn_en)||(dev->int_ena.val&(UINT32_C(1)<<27))))||!dev->cap_timer_cfg.cap_timer_en||(dev->int_ena.val&CurrentMask)){error=-13;return false;}
 // CPU-cycle span includes interrupt/cache latency and is diagnostic only.
 // Keep finite register polls and a separate monotonic wall deadline, within
 // Counter's100us nonprogress health window, before releasing a CURRENT.
 constexpr uint64_t WallLimitUs=100;
 const auto wall_begin=coarse();const auto begin=esp_cpu_get_cycle_count();
 dev->int_clr.val=CurrentMask;
 unsigned clear=0;for(;clear<128;++clear)if(!(dev->int_raw.val&CurrentMask))break;
 if(clear>=128){const auto span=uint32_t(esp_cpu_get_cycle_count()-begin);if(span>cycle_peak)cycle_peak=span;inc(timeout_count);error=-14;return false;}
 mcpwm_ll_trigger_soft_capture(dev,2);
 unsigned n=0;for(;n<128;++n)if(dev->int_raw.val&CurrentMask)break;
 const auto elapsed=uint32_t(esp_cpu_get_cycle_count()-begin);
 if(elapsed>cycle_peak)cycle_peak=elapsed;
 if(n>=128){inc(timeout_count);error=-1;return false;}
 const auto captured=mcpwm_ll_capture_get_value(dev,2);dev->int_clr.val=CurrentMask;
 const auto wall_end=coarse();
 if(wall_end>=wall_begin&&wall_end-wall_begin>wall_peak)wall_peak=uint32_t(wall_end-wall_begin>UINT32_MAX?UINT32_MAX:wall_end-wall_begin);
 if(!wall_begin||wall_end<wall_begin||wall_end-wall_begin>WallLimitUs){inc(timeout_count);error=-28;return false;}
 raw=captured;
 if(n+clear+2>poll_peak)poll_peak=n+clear+2;
 inc(reads);return true;
}
bool current(uint64_t now,n30::fine::Time& out){
 uint32_t raw=0;if(!latch(raw))return false;
 if(now<last_service){error=-2;return false;}
 const auto gap=now-last_service;
 if(gap>service_max_us)service_max_us=uint32_t(gap>UINT32_MAX?UINT32_MAX:gap);
 last_service=now;
 if(!counter.current(incarnation,raw,now,out)){error=-3;return false;}
 if(raw<latest_raw32)inc(wraps);
 latest_raw32=raw;
 last_raw80=out.raw80;return true;
}
#if PTOB_N30_FINE_PINS
void pad_off(int pin){
 // Force software OE control before clearing OE, including partial ROM routes.
 gpio_ll_set_output_enable_ctrl(&GPIO,pin,false,false);gpio_ll_output_disable(&GPIO,pin);
}
#endif
void radio_capture_off(){
 auto* dev=timer->group->hal.dev;
 mcpwm_ll_intr_enable(dev,RadioMask,false);
 mcpwm_ll_capture_enable_channel(dev,0,false);mcpwm_ll_capture_enable_channel(dev,1,false);
}
#if PTOB_N30_FINE_PINS
bool radio_prepare(){
 // Public channel enable toggles CPU IRQ/capn_en; it does NOT restore INT_ENA.
 // Both hardware channels stay disabled while only their stale RAW bits clear.
 radio_capture_off();auto* dev=timer->group->hal.dev;dev->int_clr.val=RadioMask;
 unsigned n=0;for(;n<128;++n)if(!(dev->int_raw.val&RadioMask))break;
 if(n==128){error=-19;return false;}
 mcpwm_ll_intr_enable(dev,RadioMask,true);
 if((dev->int_ena.val&RadioMask)!=RadioMask){error=-20;return false;}
 return true;
}
#endif
void contain(){
 // This sole group1 owner contains unknown native signals before returningISR.
 radio_capture_off();
#if PTOB_N30_FINE_PINS
 if(routed)pad_off(PTOB_N30_FINE_RX_GPIO);
#endif
 contained=true;active=false;history.stop();packet_edges.withdraw();if(probe.synthetic&&!probe.stopped_us)probe.stopped_us=coarse();
}
bool on_edge(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t* value,void* context){
 portENTER_CRITICAL_ISR(&mux);
 const auto now=coarse();inc(total_callbacks);
 if(active&&history.active(now)){
  if(now<burst_at){error=-2;contain();portEXIT_CRITICAL_ISR(&mux);return false;}
  if(now-burst_at>=10000){burst_at=now;burst_count=0;}
  ++burst_count;
  if(burst_count>64||(!production_owned&&total_callbacks>500000)){inc(budget_refused);error=-18;contain();portEXIT_CRITICAL_ISR(&mux);return false;}
  n30::fine::Time anchor{},edge{};
  const bool valid=current(now,anchor)&&counter.event(incarnation,value->cap_value,edge);
  if(!valid)inc(invalid);
  n30::fine::Edge observed{edge.raw80,edge.tick20,now,0,value->cap_value,incarnation,uint8_t(uintptr_t(context)),uint8_t(value->cap_edge),valid};
  history.append(observed);observed.number=history.sequence();
  if(production_owned){receiver.observe(observed);if(receiver.status().fault){error=-25;}}
  if(window_owned)packet_edges.observe(observed);
  if(error)contain();
 }else if(active){inc(expired_refused);contain();}
 else {inc(inactive_callbacks);if(routed||channel_enabled[0]||channel_enabled[1])contain();}
 portEXIT_CRITICAL_ISR(&mux);return false;
}
bool on_pps(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t* e,void*){
 portENTER_CRITICAL_ISR(&mux);
 const auto at=coarse();
 if(pps_enabled){
  if(at<pps_burst_at||pps_sequence==UINT32_MAX||((at-pps_burst_at<100000)&&++pps_burst>8)){
   pps_enabled=false;mcpwm_ll_intr_enable(timer->group->hal.dev,1u<<28,false);mcpwm_ll_capture_enable_channel(timer->group->hal.dev,1,false);
  }else if(at-pps_burst_at>=100000){pps_burst_at=at;pps_burst=1;}
 }
 if(pps_enabled){
  const PpsRaw row{e->cap_value,++pps_sequence,coarse()};
  if(pps_count<4){pps_queue[(pps_head+pps_count)%4]=row;++pps_count;}
  // A full queue leaves a sequence gap; it can never silently extend settling.
 }
 portEXIT_CRITICAL_ISR(&mux);return false;
}
bool allocate(){
 if(allocated)return true;
 // Reserve group1 exclusively; native_lora.cpp retains group0 unchanged.
 mcpwm_capture_timer_config_t cfg{};cfg.group_id=1;cfg.clk_src=MCPWM_CAPTURE_CLK_SRC_APB;
 if(mcpwm_new_capture_timer(&cfg,&timer)!=ESP_OK)return false;
 if(mcpwm_capture_timer_get_resolution(timer,&hz)!=ESP_OK||hz!=80000000||timer->group->group_id!=1)return false;
 for(unsigned i=0;i<3;++i){
  mcpwm_capture_channel_config_t ch{};ch.gpio_num=-1;ch.prescale=1;ch.flags.pos_edge=(i==1)||(i<RadioChannels&&PTOB_N30_FINE_PINS);ch.flags.neg_edge=i<RadioChannels&&PTOB_N30_FINE_PINS;
  if(mcpwm_new_capture_channel(timer,&ch,&channels[i])!=ESP_OK||channels[i]->cap_chan_id!=int(i)||channels[i]->cap_timer!=timer)return false;
  esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT,mcpwm_periph_signals.groups[1].captures[i].cap_sig,false);
  if(i<RadioChannels&&PTOB_N30_FINE_PINS){const mcpwm_capture_event_callbacks_t cb={.on_cap=on_edge};
   if(mcpwm_capture_channel_register_event_callbacks(channels[i],&cb,reinterpret_cast<void*>(uintptr_t(i)))!=ESP_OK)return false;}
 }
 const mcpwm_capture_event_callbacks_t pcb={.on_cap=on_pps};
 if(mcpwm_capture_channel_register_event_callbacks(channels[1],&pcb,nullptr)!=ESP_OK)return false;
 mcpwm_ll_intr_enable(timer->group->hal.dev,1u<<28,false);
 // No registration forCH2: its raw completion bit is polled, IRQ stays masked.
 if(timer->group->hal.dev->int_ena.val&CurrentMask)return false;
#if PTOB_N30_FINE_PINS
 radio_capture_off();
#endif
 allocated=true;return true;
}
bool route(bool enable){
#if PTOB_N30_FINE_PINS
 constexpr int pins[RadioChannels]={PTOB_N30_FINE_RX_GPIO};
 const int signals[RadioChannels]={NativeRxSignal};
 if(!enable&&!routed)return true;
 if(!enable)for(const auto pin:pins)pad_off(pin);
 bool ok=true;
 for(unsigned i=0;i<RadioChannels;++i){
  if(enable){
   // Refuse an already driven/owned pin before any route or direction change.
   gpio_io_config_t initial{};
   if(gpio_get_io_config(gpio_num_t(pins[i]),&initial)!=ESP_OK||initial.oe||initial.oe_inv||initial.sig_out!=SIG_GPIO_OUT_IDX||
      (GPIO.func_out_sel_cfg[pins[i]].val&~(1u<<10))!=SIG_GPIO_OUT_IDX){error=-27;return false;}
   // Claim only after the final ownership check, before the first mutation.
   routed=true;
   if(gpio_set_direction(gpio_num_t(pins[i]),GPIO_MODE_INPUT_OUTPUT)!=ESP_OK||
      gpio_pullup_dis(gpio_num_t(pins[i]))!=ESP_OK||gpio_pulldown_dis(gpio_num_t(pins[i]))!=ESP_OK)return false;
   esp_rom_gpio_connect_in_signal(pins[i],mcpwm_periph_signals.groups[1].captures[i].cap_sig,false);
   // gpio_set_direction/output_enable reset matrix selection: native route LAST.
   esp_rom_gpio_connect_out_signal(pins[i],signals[i],false,false);
   gpio_ll_set_output_enable_ctrl(&GPIO,pins[i],false,false);
   gpio_io_config_t io{};if(gpio_get_io_config(gpio_num_t(pins[i]),&io)!=ESP_OK)return false;
   const uint32_t input=GPIO.func_in_sel_cfg[mcpwm_periph_signals.groups[1].captures[i].cap_sig].val;
   const uint32_t output=GPIO.func_out_sel_cfg[pins[i]].val;
   if(!io.ie||!io.oe||io.fun_sel!=PIN_FUNC_GPIO||io.sig_out!=uint32_t(signals[i])||io.oe_ctrl_by_periph||io.oe_inv||
      !(input&(1u<<7))||(input&63)!=uint32_t(pins[i])||(input&(1u<<6))||(output&(1u<<9)))return false;

  }else{
   // Release only the one RX pad actually claimed by this owner.
   if(gpio_set_direction(gpio_num_t(pins[i]),GPIO_MODE_INPUT)!=ESP_OK)ok=false;
   esp_rom_gpio_connect_out_signal(pins[i],SIG_GPIO_OUT_IDX,false,false);
   esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT,mcpwm_periph_signals.groups[1].captures[i].cap_sig,false);
   if(gpio_reset_pin(gpio_num_t(pins[i]))!=ESP_OK)ok=false;
   pad_off(pins[i]);
   gpio_io_config_t io{};
   const uint32_t input=GPIO.func_in_sel_cfg[mcpwm_periph_signals.groups[1].captures[i].cap_sig].val;
   if(gpio_get_io_config(gpio_num_t(pins[i]),&io)!=ESP_OK||io.oe||io.oe_ctrl_by_periph||io.oe_inv||io.sig_out!=SIG_GPIO_OUT_IDX||
      !(input&(1u<<7))||(input&63)!=GPIO_MATRIX_CONST_ZERO_INPUT||(input&(1u<<6)))ok=false;
  }
 }
 if(!ok)return false;
#else
 (void)enable;
#endif
 routed=enable&&bool(PTOB_N30_FINE_PINS);return true;
}
// Synthetic GPIO probes are not supported by the watch microphone route.
[[gnu::noinline]] bool probe_tick(uint64_t now){(void)now;return true;}
}
[[gnu::noinline]] static bool fine_start(uint64_t id,uint64_t factory,uint64_t now,uint32_t ms,uint32_t every,bool synthetic,uint32_t spacing,uint32_t edges,bool production=false,bool shadow_only=false){
 if(synthetic)return false;
 if((PTOB_N30_FINE_BACKEND&&!production)||(production&&(!PTOB_N30_FINE_BACKEND||!PTOB_N30_FINE_PINS||!PTOB_N30_FINE_RW||synthetic||every!=1))||(synthetic&&!PTOB_N30_FINE_PINS)||active||window_owned||error||factory!=PTOB_N30_FINE_FACTORY||!id||!now||ms<1000||ms>600000||now>UINT64_MAX-uint64_t(ms)*1000||!every||every>64||incarnation==UINT32_MAX)return false;
#if PTOB_N30_FINE_PINS
 // Refuse ownership before allocation or any pad mutation. A failed claim
 // must not reset, disable, or reroute another subsystem's GPIO17 output.
 gpio_io_config_t initial{};
 // The documented reset input has OEN_SEL=0; with SIG_GPIO_OUT selected,
 // actual GPIO OE still determines whether it is driven. Accept that exact
 // reset state as well as the software-OE input left by our own release.
 if(gpio_get_io_config(gpio_num_t(PTOB_N30_FINE_RX_GPIO),&initial)!=ESP_OK||initial.oe||initial.oe_inv||initial.sig_out!=SIG_GPIO_OUT_IDX||
    (GPIO.func_out_sel_cfg[PTOB_N30_FINE_RX_GPIO].val&~(1u<<10))!=SIG_GPIO_OUT_IDX){error=-27;return false;}
#endif
 last_start={};last_start_synthetic=synthetic;
 if(!allocate()){error=-4;return false;}
 if(mcpwm_capture_timer_enable(timer)!=ESP_OK){error=-5;return false;}timer_enabled=true;
 if(mcpwm_capture_timer_start(timer)!=ESP_OK){error=-6;n30_fine_stop();return false;}timer_started=true;
 if(mcpwm_capture_channel_enable(channels[2])!=ESP_OK){error=-7;n30_fine_stop();return false;}channel_enabled[2]=true;
 portENTER_CRITICAL(&mux);
 const auto admitted_current=coarse();LatchStats attempt{};n30::fine::Counter candidate;
 uint32_t first=0,second=0;const auto next_incarnation=incarnation+1;
 bool ok=latch(first,&attempt)&&latch(second,&attempt)&&uint32_t(second-first)>0&&uint32_t(second-first)<80000;
 last_start=attempt;
 const auto fresh=coarse();
 if(ok)ok=fresh>=now&&fresh<now+uint64_t(ms)*1000&&candidate.begin(next_incarnation,second,admitted_current,hz);
 // History.start checks all bounds before mutation. No prior domain metadata
 // changes until it succeeds; latch failure leaves retained edges coherent.
 if(ok)ok=history.start(id,now,ms,every,production||shadow_only);
 if(ok){
  incarnation=next_incarnation;counter=candidate;production_owned=production;if(production){receiver.begin(second,second,incarnation);backend_stats={};}probe={};probe.synthetic=synthetic;probe.spacing_ms=spacing;probe.goal=edges;probe.next_us=coarse()+uint64_t(spacing)*1000;
  current_reads=attempt.reads;timeouts=attempt.timeouts;poll_max=attempt.poll_max;cycles_max=attempt.cycles_max;wall_max_us=attempt.wall_max_us;invalid=service_max_us=0;last_service=admitted_current;
  last_raw80=second;latest_raw32=first_raw32=second;wraps=0;first_coarse=last_service;burst_at=last_service;burst_count=total_callbacks=budget_refused=expired_refused=inactive_callbacks=0;contained=false;
  ok=history.active(coarse());
 }
 if(!ok&&!error)error=-8;
 active=false;
 portEXIT_CRITICAL(&mux);
 if(!ok){n30_fine_stop();return false;}
 if(!shadow_only&&!route(true)){if(!error)error=-15;n30_fine_stop();return false;}
 // Arm finite ISR containment before enabling the first unknown native input.
 portENTER_CRITICAL(&mux);
 bool live=history.active(coarse());
#if PTOB_N30_FINE_PINS
 if(live&&!shadow_only)live=radio_prepare();
#endif
 active=live;portEXIT_CRITICAL(&mux);
 if(!live){if(!error)error=-17;n30_fine_stop();return false;}
#if PTOB_N30_FINE_PINS
 for(unsigned i=0;!shadow_only&&i<RadioChannels;++i){
  if(mcpwm_capture_channel_enable(channels[i])!=ESP_OK){error=-7;n30_fine_stop();return false;}channel_enabled[i]=true;
  // A callback may have contained the owner during enable; never re-arm it.
  portENTER_CRITICAL(&mux);live=active&&!contained&&!error&&history.active(coarse());portEXIT_CRITICAL(&mux);
  if(!live){n30_fine_stop();return false;}
 }
#endif
 portENTER_CRITICAL(&mux);live=active&&!contained&&!error&&history.active(coarse());portEXIT_CRITICAL(&mux);
 if(!live){if(!error)error=-17;n30_fine_stop();return false;}return true;
}
bool n30_fine_start(uint64_t id,uint64_t factory,uint64_t now,uint32_t ms,uint32_t every){return fine_start(id,factory,now,ms,every,false,0,0);}
bool n30_fine_probe(uint64_t,uint64_t,uint64_t,uint32_t,uint32_t){return false;}
void n30_fine_stop(){
 portENTER_CRITICAL(&mux);pps_enabled=false;active=false;history.stop();packet_edges.withdraw();if(probe.synthetic&&!probe.stopped_us)probe.stopped_us=coarse();
#if PTOB_N30_FINE_PINS
 if(timer)radio_capture_off();
 if(routed)pad_off(PTOB_N30_FINE_RX_GPIO);
#endif
 portEXIT_CRITICAL(&mux);
 if(routed&&!route(false))error=-16;
 for(unsigned i=0;i<3;++i)if(channel_enabled[i]){if(mcpwm_capture_channel_disable(channels[i])!=ESP_OK)error=-9;else channel_enabled[i]=false;}
 if(timer_started){if(mcpwm_capture_timer_stop(timer)!=ESP_OK)error=-10;else timer_started=false;}
 if(timer_enabled){if(mcpwm_capture_timer_disable(timer)!=ESP_OK)error=-11;else timer_enabled=false;}
}
bool n30_fine_released(){
 portENTER_CRITICAL(&mux);
 const bool released=!active&&!routed&&!timer_enabled&&!timer_started&&!channel_enabled[0]&&!channel_enabled[1]&&!channel_enabled[2];
 portEXIT_CRITICAL(&mux);return released;
}
bool n30_fine_cleanup_fault(){return !n30_fine_released()&&error!=0;}
void n30_fine_poll(uint64_t){
 portENTER_CRITICAL(&mux);
 const auto now=coarse();
 bool stop=false;if(active){n30::fine::Time t{};stop=!history.active(now)||!current(coarse(),t);if(!stop)stop=!probe_tick(coarse());
  if(!stop&&window_owned){auto* d=timer->group->hal.dev;const auto w=packet_edges.status();
   if(w.collecting&&t.raw80>w.upper80){
    // Freeze capture channels between packet windows; CURRENT remains live.
    // Any callback already dispatched on the other core can still invalidate
    // this sealed interval through the retained late-event fences.
    radio_capture_off();uint32_t mask=0;for(unsigned i=0;i<3;++i)mask|=uint32_t(d->cap_chn_cfg[i].capn_en)<<i;
    packet_edges.seal(t.raw80,history.sequence(),(d->int_raw.val>>27)&1,(d->int_ena.val>>27)&7,mask,(error||invalid||contained)?uint32_t(n30::fine::OwnerFault):0u);
   }}}
 else stop=timer_started||timer_enabled||routed||channel_enabled[0]||channel_enabled[1]||channel_enabled[2];
 portEXIT_CRITICAL(&mux);if(stop)n30_fine_stop();
}
N30FineClockStatus n30_fine_clock_status(uint64_t now){
 portENTER_CRITICAL(&mux);
 const bool running=active&&now<history.until();
 const N30FineClockStatus status{history.until(),hz,running,running&&!error&&!contained&&hz==80000000,production_owned};
 portEXIT_CRITICAL(&mux);return status;
}
bool n30_fine_backend_selected(){return bool(PTOB_N30_FINE_BACKEND);}
bool n30_fine_backend_start(uint64_t boot,uint64_t factory,uint64_t now){
 return fine_start(boot,factory,now,600000,1,false,0,0,true);
}
bool n30_fine_backend_current(uint64_t* tick20){
 if(!tick20)return false;
 *tick20=0;
 portENTER_CRITICAL(&mux);n30::fine::Time t{};
 bool ok=production_owned&&active&&!error&&!contained&&history.active(coarse());
 uint32_t pending=0,captured[2]{};
 if(ok){
  // Snapshot pending radio values BEFORE CURRENT. The peripheral continues
  // capturing while the CPU lock is held: reading CAP after CURRENT can see
  // a legitimate newer edge, whose unsigned age would look invalid. A fresh
  // CH2 latch after these reads anchors every saved value without accepting
  // future values or weakening Counter::event's stale/epoch checks.
  auto* d=timer->group->hal.dev;pending=(d->int_raw.val>>27)&1;
  for(unsigned i=0;i<RadioChannels;++i)if(pending&(1u<<i))captured[i]=mcpwm_ll_capture_get_value(d,i);
  ok=current(coarse(),t)&&receiver.coordinate(t.raw80,0,*tick20);
  for(unsigned i=0;i<RadioChannels&&ok;++i)if(pending&(1u<<i)){n30::fine::Time edge{};
   ok=counter.event(incarnation,captured[i],edge)&&receiver.pending(edge.raw80);
  }
 }
 if(!ok&&production_owned&&active){if(!error)error=-26;contain();}
 portEXIT_CRITICAL(&mux);return ok;
}
int n30_fine_backend_receive(v12::periodic_rx::Stamp& stamp){
 if(!PTOB_N30_FINE_BACKEND)return 0;
 portENTER_CRITICAL(&mux);n30::fine::Time t{};n30::fine::Receiver::Match found{};
 inc(backend_stats.calls);
 bool live=production_owned&&active&&!error&&!contained&&history.active(coarse())&&current(coarse(),t);
 auto result=live?n30::fine::Receiver::Result::Unsupported:n30::fine::Receiver::Result::Fault;
 // Only the observed watch packet endpoint class enters this fine fit. Search
 // uses the descriptor's raw coordinate; phase uses solely the selected fall.
 // Unsupported packets remain authenticated/coarse outside this adapter.
 if(live&&stamp.actual&&stamp.association_observed&&stamp.radio_reference_valid&&stamp.radio_reference_applied&&stamp.radio_extra_us==4){
  result=receiver.claim(stamp.raw_tick20(),stamp.map_enclosure_ns,stamp.phy,stamp.pdu_length,stamp.ext_header,
    stamp.epoch,stamp.sequence,t.raw80,(timer->group->hal.dev->int_raw.val>>27)&1,found);
 }
 int outcome=0;
 if(result==n30::fine::Receiver::Result::Pending){inc(backend_stats.pending);outcome=1;}
 else if(result==n30::fine::Receiver::Result::Ready){
  uint64_t tick=0,available=0;uint32_t guard=UINT32_MAX;
  if(receiver.coordinate(found.fall.raw80,PTOB_N30_FINE_TRANSLATION80,tick)&&receiver.coordinate(t.raw80,0,available)&&tick<=available&&
     n30::fine::Receiver::lifetime_guard(stamp.tick20,tick,stamp.map_enclosure_ns,guard)){
   stamp.tick20=tick;stamp.available20=available;stamp.radio_extra_us=0;stamp.refinement_applied=false;stamp.refinement_ticks=0;
   // Preserve signed expiry/coarse permission with the old guard plus the
   // actual coordinate displacement and rounding. This is not an RF bound.
   stamp.map_enclosure_ns=guard;stamp.edge_calibrated=false;backend_stats.fall80=found.fall.raw80;backend_stats.stamp20=tick;backend_stats.available20=available;outcome=2;
  }
 }
 if(result==n30::fine::Receiver::Result::Missing)inc(backend_stats.missing);
 if(result==n30::fine::Receiver::Result::Ambiguous)inc(backend_stats.ambiguous);
 if(result==n30::fine::Receiver::Result::Unsupported)inc(backend_stats.unsupported);
 if(result==n30::fine::Receiver::Result::Fault&&production_owned&&active){if(!error)error=-25;contain();}
 portEXIT_CRITICAL(&mux);return outcome;
}
bool n30_fine_window_start(uint64_t id,uint64_t factory,uint64_t now,uint32_t duration){
 if(!PTOB_N30_FINE_PINS||!PTOB_N30_FINE_RW)return false;
 if(!n30_fine_start(id,factory,now,duration,1))return false;
 portENTER_CRITICAL(&mux);
 n30::fine::Time t{};const bool ok=current(coarse(),t);
 if(ok){packet_edges.reset({t.raw80,0,t.raw80,first_raw32,history.id(),history.until(),incarnation,hz});window_owned=true;}
 if(!ok)contain();
 portEXIT_CRITICAL(&mux);if(!ok)n30_fine_stop();return ok;
}
void n30_fine_window_stop(){portENTER_CRITICAL(&mux);packet_edges.withdraw();window_owned=false;portEXIT_CRITICAL(&mux);n30_fine_stop();}
bool n30_fine_window_bridge(bool (*read20)(uint64_t*),n30::fine::Bridge& b){
 b={};portENTER_CRITICAL(&mux);n30::fine::Time a{},z{};uint64_t g=0;
 const bool ok=window_owned&&active&&!probe.synthetic&&history.active(coarse())&&current(coarse(),a)&&read20&&read20(&g)&&current(coarse(),z)&&a.incarnation==z.incarnation;
 if(ok)b={a.raw80,g,z.raw80,first_raw32,history.id(),history.until(),incarnation,hz};
 if(error){contain();}portEXIT_CRITICAL(&mux);return ok;
}
bool n30_fine_window_begin(uint32_t id,uint64_t lower,uint64_t upper,uint32_t inc){
 portENTER_CRITICAL(&mux);const bool ok=window_owned&&active&&!error&&history.active(coarse())&&packet_edges.begin(id,lower,upper,history,inc);portEXIT_CRITICAL(&mux);return ok;
}
bool n30_fine_window_refuse(uint32_t id){portENTER_CRITICAL(&mux);const bool ok=window_owned&&packet_edges.skip(id);if(ok)radio_capture_off();portEXIT_CRITICAL(&mux);return ok;}
bool n30_fine_window_rearm(){
 portENTER_CRITICAL(&mux);bool ok=window_owned&&active&&!error&&history.active(coarse())&&packet_edges.status().sealed;
#if PTOB_N30_FINE_PINS
 if(ok)ok=radio_prepare();
 if(ok){auto* d=timer->group->hal.dev;mcpwm_ll_capture_enable_channel(d,0,true);mcpwm_ll_capture_enable_channel(d,1,false);
  ok=d->cap_chn_cfg[0].capn_en&&!d->cap_chn_cfg[1].capn_en;}
#else
 ok=false;
#endif
 n30::fine::Time t{};if(ok)ok=current(coarse(),t);if(ok)packet_edges.arm(t.raw80);else if(window_owned){error=-24;contain();}
 portEXIT_CRITICAL(&mux);return ok;
}
static void window_snapshot_locked(n30::fine::WindowStatus& s){s=packet_edges.status();s.current80=last_raw80;s.current_incarnation=incarnation;s.hz=hz;s.timeouts=timeouts;s.invalid=invalid;s.inactive=inactive_callbacks;s.error=error;s.active=active;s.contained=contained;}
n30::fine::WindowStatus n30_fine_window_status(){n30::fine::WindowStatus s{};portENTER_CRITICAL(&mux);window_snapshot_locked(s);portEXIT_CRITICAL(&mux);return s;}
unsigned n30_fine_window_read(uint32_t after,n30::fine::Edge* out,unsigned capacity){portENTER_CRITICAL(&mux);const auto n=packet_edges.read(after,out,capacity);portEXIT_CRITICAL(&mux);return n;}
unsigned n30_fine_window_late(uint32_t after,n30::fine::Window::Late* out,unsigned capacity,n30::fine::WindowStatus* snapshot){portENTER_CRITICAL(&mux);const auto n=packet_edges.late(after,out,capacity);if(snapshot){window_snapshot_locked(*snapshot);}portEXIT_CRITICAL(&mux);return n;}
void n30_fine_frame(v12::ConsoleFrame& f,uint32_t after,uint64_t now){
 n30_fine_poll(now);
 portENTER_CRITICAL(&mux);
 const unsigned count=history.read(after,batch,4);
 const auto receiver_status=receiver.status();const bool backend=production_owned;
 const auto bs=backend_stats;
 uint32_t rx_out=0,tx_out=0,rx_in=0,tx_in=0,oe=0,channel_mask=0,irq_mask=0;
#if PTOB_N30_FINE_PINS
 rx_out=GPIO.func_out_sel_cfg[PTOB_N30_FINE_RX_GPIO].val;

 rx_in=GPIO.func_in_sel_cfg[mcpwm_periph_signals.groups[1].captures[0].cap_sig].val;
 tx_in=GPIO.func_in_sel_cfg[mcpwm_periph_signals.groups[1].captures[1].cap_sig].val;
 oe=(GPIO.enable>>PTOB_N30_FINE_RX_GPIO)&1u;
#endif
 if(timer){const auto* dev=timer->group->hal.dev;for(unsigned i=0;i<3;++i)channel_mask|=uint32_t(dev->cap_chn_cfg[i].capn_en)<<i;irq_mask=(dev->int_ena.val>>27)&7;}
 probe_snapshot=probe;start_snapshot=last_start;start_synthetic_snapshot=last_start_synthetic;input_levels_snapshot=0;
#if PTOB_N30_FINE_PINS
 input_levels_snapshot=uint32_t(gpio_get_level(gpio_num_t(PTOB_N30_FINE_RX_GPIO)));
#endif
 snapshot={history.id(),history.until(),last_raw80,history.every(),history.seen(),history.rx(),history.tx(),history.sequence(),history.overwritten(),history.oldest(),incarnation,hz,current_reads,timeouts,invalid,poll_max,cycles_max,wall_max_us,service_max_us,first_raw32,latest_raw32,wraps,total_callbacks,budget_refused,expired_refused,inactive_callbacks,rx_out,tx_out,rx_in,tx_in,oe,channel_mask,irq_mask,first_coarse,last_service,error,active,contained};
 portEXIT_CRITICAL(&mux);
 const auto& q=snapshot;const auto& p=probe_snapshot;
 const char* mode=p.synthetic?"SYNTHETIC_GPIO":NativeMode;
 const auto quiet_at=p.stopped_us?p.stopped_us:q.last_us;
 const auto quiet=p.last_issue_us&&quiet_at>=p.last_issue_us?quiet_at-p.last_issue_us:0;
 f.format("N30_FINE_STATUS schema=1 id=%016" PRIx64 " active=%u until_us=%" PRIu64 " every=%" PRIu32 " seen=%" PRIu32 " RX=%" PRIu32 " TX=%" PRIu32 " sampled=%" PRIu32 " overwritten=%" PRIu32 " oldest=%" PRIu32 " after=%" PRIu32 " count=%u incarnation=%" PRIu32 " hz=%" PRIu32 " current=%" PRIu32 " timeout=%" PRIu32 " invalid=%" PRIu32 " poll_max=%" PRIu32 " cycles_max=%" PRIu32 " service_max_us=%" PRIu32 " latest80=%" PRIu64 " first32=%" PRIu32 " latest32=%" PRIu32 " wraps=%" PRIu32 " first_us=%" PRIu64 " last_us=%" PRIu64 " released=%u error=%d pins=%u callbacks=%" PRIu32 " budget_refused=%" PRIu32 " expired_refused=%" PRIu32 " inactive_callbacks=%" PRIu32 " contained=%u rx_out=%" PRIu32 " tx_out=%" PRIu32 " rx_in=%" PRIu32 " tx_in=%" PRIu32 " OE=%" PRIu32 " channels=%" PRIu32 " IRQ=%" PRIu32 " mode=%s probe_spacing_ms=%" PRIu32 " probe_goal=%" PRIu32 " attempted=%" PRIu32 " issued0=%" PRIu32 " issued1=%" PRIu32 " rise0=%" PRIu32 " fall0=%" PRIu32 " rise1=%" PRIu32 " fall1=%" PRIu32 " initial_low=%" PRIu32 " verified_levels=%" PRIu32 " input_levels=%" PRIu32 " level_poll_max=%" PRIu32 " last_issue_us=%" PRIu64 " stopped_us=%" PRIu64 " quiet_us=%" PRIu64 " start_mode=%s start_reads=%" PRIu32 " start_timeouts=%" PRIu32 " start_poll_max=%" PRIu32 " start_cycles_max=%" PRIu32 " latch_max_us=%" PRIu32 " start_latch_max_us=%" PRIu32 "\n",
 q.id,unsigned(q.active),q.until,q.every,q.seen,q.rx,q.tx,q.sampled,q.overwritten,q.oldest,after,count,q.incarnation,q.hz,q.current,q.timeout,q.invalid,q.poll,q.cycles,q.service,q.latest,q.first32,q.latest32,q.wraps,q.first_us,q.last_us,unsigned(n30_fine_released()),q.error,unsigned(PTOB_N30_FINE_PINS),q.callbacks,q.budget_refused,q.expired_refused,q.inactive_callbacks,unsigned(q.contained),q.rx_out,q.tx_out,q.rx_in,q.tx_in,q.oe,q.channel_mask,q.irq_mask,mode,p.spacing_ms,p.goal,p.attempted,p.issued[0],p.issued[1],p.rise[0],p.fall[0],p.rise[1],p.fall[1],p.initial_low,p.verified_levels,input_levels_snapshot,p.level_poll_max,p.last_issue_us,p.stopped_us,quiet,start_synthetic_snapshot?"SYNTHETIC_GPIO":NativeMode,start_snapshot.reads,start_snapshot.timeouts,start_snapshot.poll_max,start_snapshot.cycles_max,q.wall,start_snapshot.wall_max_us);
 for(unsigned i=0;i<4;++i){
  if(i>=count){f.format("N30_FINE_EDGE%u present=0\n",i);continue;}
  const auto& e=batch[i];
  f.format("N30_FINE_EDGE%u present=1 n=%" PRIu32 " raw32=%" PRIu32 " raw80=%" PRIu64 " tick20=%" PRIu64 " seen_us=%" PRIu64 " incarnation=%" PRIu32 " kind=%u polarity=%u extended=%u association=UNESTABLISHED provenance=%s\n",i,e.number,e.raw32,e.raw80,e.tick20,e.seen_us,e.incarnation,unsigned(e.kind),unsigned(e.polarity),unsigned(e.valid),mode);
 }
 f.format("N30_FINE_CONTRACT observer=%s domain=MCPWM1_80MHZ RX=%s TX=%s CURRENT=CH2_SW128 capture_channels=RX0/CURRENT2 auxiliary=GNSS_PPS13_CH1 pps_aux=%u pps_input=%u edge=RISING_AND_FALLING endpoint=UNKNOWN coalesced=UNKNOWN packet_association=%s RF_bound=UNKNOWN UTC_bound=UNKNOWN clock_update=%s production=%s offset=%s provenance=%s expected=%s backend=%u phase80=%" PRIu64 " claimed=%" PRIu32 " callback_history=%" PRIu32 " callback_overwritten=%" PRIu32 " late=%" PRIu32 " receiver_fault=%" PRIu32 " receive_calls=%" PRIu32 " pending_calls=%" PRIu32 " missing_calls=%" PRIu32 " ambiguous_calls=%" PRIu32 " unsupported_calls=%" PRIu32 " fall80=%" PRIu64 " stamp20=%" PRIu64 " available20=%" PRIu64 "\n",backend?"RAW_AND_G1":"RAW_ONLY",NativeRxName,NativeTxName,unsigned(pps_enabled),unsigned(pps_enabled?GPIO.func_in_sel_cfg[mcpwm_periph_signals.groups[1].captures[1].cap_sig].val:188),backend?"EMPIRICAL_CLASS_UNIQUE_FALL":"NONE",backend?"G1_RX":"NONE",backend?"MCPWM1_20":"GPTIMER20",backend?"FINE_SPECIFIC":"LEGACY_ONLY",mode,p.synthetic?"SYNTHETIC_VERIFIED_PAD_TRANSITIONS_ONLY":"NONE",unsigned(backend),backend?PTOB_N30_FINE_TRANSLATION80:0,receiver_status.claimed,receiver_status.callbacks,receiver_status.overwritten,receiver_status.late,receiver_status.fault,bs.calls,bs.pending,bs.missing,bs.ambiguous,bs.unsupported,bs.fall80,bs.stamp20,bs.available20);
}

// The main task alone manages this auxiliary owner. UI visibility is a mailbox.
bool watch_pps_start(uint64_t boot,uint64_t factory,uint64_t now){
 if(pps_enabled)return true;
 if(channel_enabled[1]||window_owned||error||contained)return false;
 if(!active){
  pps_clock_owned=true;
  if(!fine_start(boot,factory,now,600000,1,false,0,0,false,true)){watch_pps_stop();return false;}
 }else if(!production_owned)return false; // Never borrow a finite diagnostic lease.
 gpio_io_config_t prior{};
 if(gpio_get_io_config(GPIO_NUM_13,&prior)!=ESP_OK||prior.oe||prior.oe_inv||prior.sig_out!=SIG_GPIO_OUT_IDX){watch_pps_stop();return false;}
 if(gpio_set_direction(GPIO_NUM_13,GPIO_MODE_INPUT)!=ESP_OK){watch_pps_stop();return false;}
 portENTER_CRITICAL(&mux);pps_head=pps_count=0;pps_sequence=pps_burst=0;pps_burst_at=coarse();pps_enabled=true;
 esp_rom_gpio_connect_in_signal(13,mcpwm_periph_signals.groups[1].captures[1].cap_sig,false);
 timer->group->hal.dev->int_clr.val=1u<<28;
 mcpwm_ll_intr_enable(timer->group->hal.dev,1u<<28,true);
 portEXIT_CRITICAL(&mux);
 if(mcpwm_capture_channel_enable(channels[1])!=ESP_OK){watch_pps_stop();return false;}
 channel_enabled[1]=true;return true;
}
bool watch_pps_stop(){
 portENTER_CRITICAL(&mux);pps_enabled=false;pps_head=pps_count=0;
 if(timer){mcpwm_ll_intr_enable(timer->group->hal.dev,1u<<28,false);mcpwm_ll_capture_enable_channel(timer->group->hal.dev,1,false);
  esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT,mcpwm_periph_signals.groups[1].captures[1].cap_sig,false);}
 portEXIT_CRITICAL(&mux);
 if(channel_enabled[1]){if(mcpwm_capture_channel_disable(channels[1])!=ESP_OK)return false;channel_enabled[1]=false;}
 if(pps_clock_owned){n30_fine_stop();if(!n30_fine_released())return false;pps_clock_owned=false;}
 return true;
}
bool watch_pps_take(WatchPpsCapture& out,bool (*read20)(uint64_t*)){
 out={};uint64_t before=0,after=0;const bool fine=n30_fine_backend_selected();
 const bool bracket=fine||(read20&&read20(&before));
 portENTER_CRITICAL(&mux);
 if(!pps_count){portEXIT_CRITICAL(&mux);return false;}
 const auto row=pps_queue[pps_head];pps_head=(pps_head+1)%4;--pps_count;
 out.number=row.number;out.seen_us=row.seen;out.incarnation=incarnation;
 n30::fine::Time anchor{},edge{};
 bool ok=pps_enabled&&active&&!error&&!contained&&history.active(coarse())&&current(coarse(),anchor)&&counter.event(incarnation,row.raw,edge);
 if(ok&&fine){ok=receiver.coordinate(edge.raw80,0,out.tick20);out.enclosure_ns=50;}
 const auto anchor20=anchor.tick20,edge20=edge.tick20;
 portEXIT_CRITICAL(&mux);
 if(ok&&!fine){
  ok=bracket&&read20&&read20(&after)&&after>=before&&after-before<=4000&&anchor20>=edge20;
  if(ok){const auto middle=before+(after-before)/2,age=anchor20-edge20;ok=middle>age;
   if(ok){out.tick20=middle-age;out.enclosure_ns=uint32_t((after-before)*50+50);}}
 }
 out.valid=ok;return true;
}

bool watch_pps_healthy(){portENTER_CRITICAL(&mux);const bool ok=pps_enabled&&active&&!error&&!contained;portEXIT_CRITICAL(&mux);return ok;}

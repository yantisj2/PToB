// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Finite N30-only raw observer. No clock training, packet claim or RF correction.
#include "fine_capture.hpp"
#include "fine_counter.hpp"
#include "fine_capture_config.h"
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
n30::fine::Edge batch[4];
struct Snapshot {uint64_t id,until,latest;uint32_t every,seen,rx,tx,sampled,overwritten,oldest,incarnation,hz,current,timeout,invalid,poll,cycles,service,first32,latest32,wraps,callbacks,budget_refused,expired_refused,inactive_callbacks,rx_out,tx_out,rx_in,tx_in,oe,channel_mask,irq_mask;uint64_t first_us,last_us;int error;bool active,contained;};
Snapshot snapshot{};
uint32_t incarnation=0,hz=0,current_reads=0,timeouts=0,invalid=0,poll_max=0,cycles_max=0,service_max_us=0;
uint32_t latest_raw32=0,first_raw32=0,wraps=0;
uint64_t first_coarse=0,last_service=0,last_raw80=0;int error=0;
bool allocated=false,active=false,timer_enabled=false,timer_started=false,routed=false;
bool channel_enabled[3]{};uint64_t burst_at=0;uint32_t burst_count=0,total_callbacks=0,budget_refused=0,expired_refused=0,inactive_callbacks=0;bool contained=false;
constexpr uint32_t CurrentMask=UINT32_C(1)<<29,RadioMask=UINT32_C(3)<<27;
static_assert(SYNCFOUND_FLAG_IDX==230&&PKT_TX_ON_IDX==235,"Pinned native signal IDs changed");
uint64_t coarse(){const auto t=esp_timer_get_time();return t>0?uint64_t(t):0;}
void inc(uint32_t& x){if(x!=UINT32_MAX)++x;}
// Pinned private ABI/register adapter; not a stable public IDF API.
// Caller holds our sole cross-core lock; CH2 has no ISR/callback. Clear only2.
bool latch(uint32_t& raw){
 if(!timer_enabled||!timer_started||!channel_enabled[2]||timer->fsm!=MCPWM_CAP_TIMER_FSM_ENABLE||
    channels[2]->fsm!=MCPWM_CAP_CHAN_FSM_ENABLE){error=-12;return false;}
 auto* dev=timer->group->hal.dev;
 if((!PTOB_N30_FINE_PINS&&(dev->cap_chn_cfg[0].capn_en||dev->cap_chn_cfg[1].capn_en||(dev->int_ena.val&(UINT32_C(3)<<27))))||!dev->cap_timer_cfg.cap_timer_en||(dev->int_ena.val&CurrentMask)){error=-13;return false;}
 const auto begin=esp_cpu_get_cycle_count();
 dev->int_clr.val=CurrentMask;
 unsigned clear=0;for(;clear<128;++clear)if(!(dev->int_raw.val&CurrentMask))break;
 if(clear>=128){const auto span=uint32_t(esp_cpu_get_cycle_count()-begin);if(span>cycles_max)cycles_max=span;inc(timeouts);error=-14;return false;}
 mcpwm_ll_trigger_soft_capture(dev,2);
 unsigned n=0;for(;n<128;++n)if(dev->int_raw.val&CurrentMask)break;
 const auto elapsed=uint32_t(esp_cpu_get_cycle_count()-begin);
 if(elapsed>cycles_max)cycles_max=elapsed;
 if(n>=128||elapsed>4096){inc(timeouts);error=-1;return false;}
 raw=mcpwm_ll_capture_get_value(dev,2);dev->int_clr.val=CurrentMask;
 if(n+clear+2>poll_max)poll_max=n+clear+2;
 inc(current_reads);return true;
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
 pad_off(PTOB_N30_FINE_RX_GPIO);pad_off(PTOB_N30_FINE_TX_GPIO);
#endif
 contained=true;active=false;history.stop();
}
bool on_edge(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t* value,void* context){
 portENTER_CRITICAL_ISR(&mux);
 const auto now=coarse();inc(total_callbacks);
 if(active&&history.active(now)){
  if(now<burst_at){error=-2;contain();portEXIT_CRITICAL_ISR(&mux);return false;}
  if(now-burst_at>=10000){burst_at=now;burst_count=0;}
  ++burst_count;
  if(burst_count>64||total_callbacks>500000){inc(budget_refused);error=-18;contain();portEXIT_CRITICAL_ISR(&mux);return false;}
  n30::fine::Time anchor{},edge{};
  const bool valid=current(now,anchor)&&counter.event(incarnation,value->cap_value,edge);
  if(!valid)inc(invalid);
  history.append({edge.raw80,edge.tick20,now,0,value->cap_value,incarnation,uint8_t(uintptr_t(context)),uint8_t(value->cap_edge),valid});
  if(error)contain();
 }else if(active){inc(expired_refused);contain();}
 else {inc(inactive_callbacks);if(routed||channel_enabled[0]||channel_enabled[1])contain();}
 portEXIT_CRITICAL_ISR(&mux);return false;
}
bool allocate(){
 if(allocated)return true;
 // Reserve group1 exclusively; native_lora.cpp retains group0 unchanged.
 mcpwm_capture_timer_config_t cfg{};cfg.group_id=1;cfg.clk_src=MCPWM_CAPTURE_CLK_SRC_APB;
 if(mcpwm_new_capture_timer(&cfg,&timer)!=ESP_OK)return false;
 if(mcpwm_capture_timer_get_resolution(timer,&hz)!=ESP_OK||hz!=80000000||timer->group->group_id!=1)return false;
 for(unsigned i=0;i<3;++i){
  mcpwm_capture_channel_config_t ch{};ch.gpio_num=-1;ch.prescale=1;ch.flags.pos_edge=i<2&&PTOB_N30_FINE_PINS;ch.flags.neg_edge=i<2&&PTOB_N30_FINE_PINS;
  if(mcpwm_new_capture_channel(timer,&ch,&channels[i])!=ESP_OK||channels[i]->cap_chan_id!=int(i)||channels[i]->cap_timer!=timer)return false;
  esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT,mcpwm_periph_signals.groups[1].captures[i].cap_sig,false);
  if(i<2&&PTOB_N30_FINE_PINS){const mcpwm_capture_event_callbacks_t cb={.on_cap=on_edge};
   if(mcpwm_capture_channel_register_event_callbacks(channels[i],&cb,reinterpret_cast<void*>(uintptr_t(i)))!=ESP_OK)return false;}
 }
 // No registration forCH2: its raw completion bit is polled, IRQ stays masked.
 if(timer->group->hal.dev->int_ena.val&CurrentMask)return false;
#if PTOB_N30_FINE_PINS
 radio_capture_off();
#endif
 allocated=true;return true;
}
bool route(bool enable){
#if PTOB_N30_FINE_PINS
 constexpr int pins[2]={PTOB_N30_FINE_RX_GPIO,PTOB_N30_FINE_TX_GPIO};
 constexpr int signals[2]={SYNCFOUND_FLAG_IDX,PKT_TX_ON_IDX};
 // Mark partial ownership before any pad mutation; failed setup must unwind.
 if(enable)routed=true;
 else for(const auto pin:pins)pad_off(pin);
 bool ok=true;
 for(unsigned i=0;i<2;++i){
  if(enable){
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
   // Attempt BOTH pads even if an API/readback fails on the first one.
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
}
bool n30_fine_start(uint64_t id,uint64_t factory,uint64_t now,uint32_t ms,uint32_t every){
 if(active||error||factory!=PTOB_N30_FINE_FACTORY||!id||ms<1000||ms>600000||!every||every>64||incarnation==UINT32_MAX)return false;
 if(!allocate()){error=-4;return false;}
 if(mcpwm_capture_timer_enable(timer)!=ESP_OK){error=-5;return false;}timer_enabled=true;
 if(mcpwm_capture_timer_start(timer)!=ESP_OK){error=-6;n30_fine_stop();return false;}timer_started=true;
 if(mcpwm_capture_channel_enable(channels[2])!=ESP_OK){error=-7;n30_fine_stop();return false;}channel_enabled[2]=true;
 portENTER_CRITICAL(&mux);
 current_reads=timeouts=invalid=poll_max=cycles_max=service_max_us=0;last_service=coarse();
 uint32_t first=0,second=0;
 bool ok=latch(first)&&latch(second)&&uint32_t(second-first)>0&&uint32_t(second-first)<80000;
 if(ok)ok=counter.begin(++incarnation,second,last_service,hz)&&history.start(id,now,ms,every)&&history.active(coarse());
 if(!ok)error=-8;
 active=false;last_raw80=second;latest_raw32=first_raw32=second;wraps=0;first_coarse=last_service;burst_at=last_service;burst_count=total_callbacks=budget_refused=expired_refused=inactive_callbacks=0;contained=false;
 portEXIT_CRITICAL(&mux);
 if(!ok){n30_fine_stop();return false;}
 if(!route(true)){error=-15;n30_fine_stop();return false;}
 // Arm finite ISR containment before enabling the first unknown native input.
 portENTER_CRITICAL(&mux);
 bool live=history.active(coarse());
#if PTOB_N30_FINE_PINS
 if(live)live=radio_prepare();
#endif
 active=live;portEXIT_CRITICAL(&mux);
 if(!live){if(!error)error=-17;n30_fine_stop();return false;}
#if PTOB_N30_FINE_PINS
 for(unsigned i=0;i<2;++i){
  if(mcpwm_capture_channel_enable(channels[i])!=ESP_OK){error=-7;n30_fine_stop();return false;}channel_enabled[i]=true;
  // A callback may have contained the owner during enable; never re-arm it.
  portENTER_CRITICAL(&mux);live=active&&!contained&&!error&&history.active(coarse());portEXIT_CRITICAL(&mux);
  if(!live){n30_fine_stop();return false;}
 }
#endif
 portENTER_CRITICAL(&mux);live=active&&!contained&&!error&&history.active(coarse());portEXIT_CRITICAL(&mux);
 if(!live){if(!error)error=-17;n30_fine_stop();return false;}return true;
}
void n30_fine_stop(){
 portENTER_CRITICAL(&mux);active=false;history.stop();
#if PTOB_N30_FINE_PINS
 if(timer){radio_capture_off();pad_off(PTOB_N30_FINE_RX_GPIO);pad_off(PTOB_N30_FINE_TX_GPIO);}
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
 bool stop=false;if(active){n30::fine::Time t{};stop=!history.active(now)||!current(coarse(),t);}
 else stop=timer_started||timer_enabled||routed||channel_enabled[0]||channel_enabled[1]||channel_enabled[2];
 portEXIT_CRITICAL(&mux);if(stop)n30_fine_stop();
}
void n30_fine_frame(v12::ConsoleFrame& f,uint32_t after,uint64_t now){
 n30_fine_poll(now);
 portENTER_CRITICAL(&mux);
 const unsigned count=history.read(after,batch,4);
 uint32_t rx_out=0,tx_out=0,rx_in=0,tx_in=0,oe=0,channel_mask=0,irq_mask=0;
#if PTOB_N30_FINE_PINS
 rx_out=GPIO.func_out_sel_cfg[PTOB_N30_FINE_RX_GPIO].val;
 tx_out=GPIO.func_out_sel_cfg[PTOB_N30_FINE_TX_GPIO].val;
 rx_in=GPIO.func_in_sel_cfg[mcpwm_periph_signals.groups[1].captures[0].cap_sig].val;
 tx_in=GPIO.func_in_sel_cfg[mcpwm_periph_signals.groups[1].captures[1].cap_sig].val;
 oe=((GPIO.enable>>PTOB_N30_FINE_RX_GPIO)&1u)|(((GPIO.enable>>PTOB_N30_FINE_TX_GPIO)&1u)<<1);
#endif
 if(timer){const auto* dev=timer->group->hal.dev;for(unsigned i=0;i<3;++i)channel_mask|=uint32_t(dev->cap_chn_cfg[i].capn_en)<<i;irq_mask=(dev->int_ena.val>>27)&7;}
 snapshot={history.id(),history.until(),last_raw80,history.every(),history.seen(),history.rx(),history.tx(),history.sequence(),history.overwritten(),history.oldest(),incarnation,hz,current_reads,timeouts,invalid,poll_max,cycles_max,service_max_us,first_raw32,latest_raw32,wraps,total_callbacks,budget_refused,expired_refused,inactive_callbacks,rx_out,tx_out,rx_in,tx_in,oe,channel_mask,irq_mask,first_coarse,last_service,error,active,contained};
 portEXIT_CRITICAL(&mux);
 const auto& q=snapshot;
 f.format("N30_FINE_STATUS schema=1 id=%016" PRIx64 " active=%u until_us=%" PRIu64 " every=%" PRIu32 " seen=%" PRIu32 " RX=%" PRIu32 " TX=%" PRIu32 " sampled=%" PRIu32 " overwritten=%" PRIu32 " oldest=%" PRIu32 " after=%" PRIu32 " count=%u incarnation=%" PRIu32 " hz=%" PRIu32 " current=%" PRIu32 " timeout=%" PRIu32 " invalid=%" PRIu32 " poll_max=%" PRIu32 " cycles_max=%" PRIu32 " service_max_us=%" PRIu32 " latest80=%" PRIu64 " first32=%" PRIu32 " latest32=%" PRIu32 " wraps=%" PRIu32 " first_us=%" PRIu64 " last_us=%" PRIu64 " released=%u error=%d pins=%u callbacks=%" PRIu32 " budget_refused=%" PRIu32 " expired_refused=%" PRIu32 " inactive_callbacks=%" PRIu32 " contained=%u rx_out=%" PRIu32 " tx_out=%" PRIu32 " rx_in=%" PRIu32 " tx_in=%" PRIu32 " OE=%" PRIu32 " channels=%" PRIu32 " IRQ=%" PRIu32 "\n",
 q.id,unsigned(q.active),q.until,q.every,q.seen,q.rx,q.tx,q.sampled,q.overwritten,q.oldest,after,count,q.incarnation,q.hz,q.current,q.timeout,q.invalid,q.poll,q.cycles,q.service,q.latest,q.first32,q.latest32,q.wraps,q.first_us,q.last_us,unsigned(n30_fine_released()),q.error,unsigned(PTOB_N30_FINE_PINS),q.callbacks,q.budget_refused,q.expired_refused,q.inactive_callbacks,unsigned(q.contained),q.rx_out,q.tx_out,q.rx_in,q.tx_in,q.oe,q.channel_mask,q.irq_mask);
 for(unsigned i=0;i<4;++i){
  if(i>=count){f.format("N30_FINE_EDGE%u present=0\n",i);continue;}
  const auto& e=batch[i];
  f.format("N30_FINE_EDGE%u present=1 n=%" PRIu32 " raw32=%" PRIu32 " raw80=%" PRIu64 " tick20=%" PRIu64 " seen_us=%" PRIu64 " incarnation=%" PRIu32 " kind=%u polarity=%u extended=%u association=UNESTABLISHED\n",i,e.number,e.raw32,e.raw80,e.tick20,e.seen_us,e.incarnation,unsigned(e.kind),unsigned(e.polarity),unsigned(e.valid));
 }
 f.format("N30_FINE_CONTRACT observer=RAW_ONLY domain=MCPWM1_80MHZ RX=SYNCFOUND_230 TX=PKT_TX_ON_235 CURRENT=CH2_SW128 capture_channels=0/1/2 edge=RISING_AND_FALLING endpoint=UNKNOWN coalesced=UNKNOWN packet_association=NONE RF_bound=UNKNOWN UTC_bound=UNKNOWN clock_update=NONE production=GPTIMER20 offset=LEGACY_ONLY\n");
}

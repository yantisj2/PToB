// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// W12 adaptation of N30's persistent MCPWM owner. RX only: GPIO16; no TX pad.
#include "w12_fine_clock.hpp"
#include "stack_frame.hpp"
#include "w12_lora_board.hpp"
#include "fine_counter.hpp"
#include "fine_receiver.hpp"
#include "periodic_rx_model.hpp"
#include "w12_fine_config.h"
#include "driver/mcpwm_cap.h"
#include "driver/gpio.h"
#include "esp_rom_gpio.h"
#include "esp_timer.h"
#include "esp_cpu.h"
#include "esp_attr.h"
#include "freertos/FreeRTOS.h"
#include "mcpwm_private.h"
#include "soc/gpio_sig_map.h"
#include "soc/mcpwm_periph.h"
#include "hal/gpio_ll.h"
namespace {
static_assert(PTOB_W12_FINE_RX_GPIO==16 && RW_RX_ON_IDX==238,"Fixed W12 route changed");
constexpr uint32_t CurrentMask=UINT32_C(1)<<29,RadioMask=UINT32_C(3)<<27;
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
mcpwm_cap_timer_handle_t timer=nullptr;
mcpwm_cap_channel_handle_t channels[3]{};
w12::fine::Counter counter;
w12::fine::Counter lora_counter;
w12::fine::Receiver receiver;
bool timer_enabled=false,timer_started=false,enabled[3]{},active=false,pad_owned=false;
int error=0;
bool lora_active=false,lora_borrowed=false;W12LoRaCapture callback=nullptr;
uint32_t sequence=0,burst=0;
uint64_t burst_at=0;
W12FineStatus stats{};
void inc(uint32_t& x){if(x!=UINT32_MAX)++x;}
uint64_t coarse(){const auto n=esp_timer_get_time();return n>0?uint64_t(n):0;}
void pad_off(){
  gpio_ll_set_output_enable_ctrl(&GPIO,16,false,false);
  gpio_ll_output_disable(&GPIO,16);
}
void capture_off(){
  if(!timer)return;
  auto* d=timer->group->hal.dev;
  mcpwm_ll_intr_enable(d,RadioMask,false);
  mcpwm_ll_capture_enable_channel(d,0,false);
  mcpwm_ll_capture_enable_channel(d,1,false);
}
void contain(){active=false;lora_active=false;capture_off();if(pad_owned)pad_off();}
// Bounded pinned-driver CURRENT latch. CH2 has no registered ISR.
W12_STACK_FRAME bool latch(uint32_t& raw){
  if(!timer_enabled||!timer_started||!enabled[2]||
     timer->fsm!=MCPWM_CAP_TIMER_FSM_ENABLE||channels[2]->fsm!=MCPWM_CAP_CHAN_FSM_ENABLE)return false;
  auto* d=timer->group->hal.dev;
  if(!d->cap_timer_cfg.cap_timer_en||(d->int_ena.val&CurrentMask)||(!lora_borrowed&&d->cap_chn_cfg[1].capn_en))return false;
  const auto start=esp_cpu_get_cycle_count();
  d->int_clr.val=CurrentMask;
  unsigned n=0;for(;n<128;++n)if(!(d->int_raw.val&CurrentMask))break;
  if(n==128)return false;
  mcpwm_ll_trigger_soft_capture(d,2);
  for(n=0;n<128;++n)if(d->int_raw.val&CurrentMask)break;
  if(n==128||uint32_t(esp_cpu_get_cycle_count()-start)>24000)return false;
  raw=mcpwm_ll_capture_get_value(d,2);return true;
}
// The caller validates both owners before entering this bounded aperture.
// Keeping it in instruction RAM avoids flash-cache variation between latches.
W12_STACK_FRAME bool IRAM_ATTR gnss_aperture(mcpwm_dev_t* local,mcpwm_dev_t* gnss,uint32_t& lo,uint32_t& hi){
  const auto start=esp_cpu_get_cycle_count();
  local->int_clr.val=CurrentMask;
  unsigned n=0;
  for(;n<128;++n)if(!(local->int_raw.val&CurrentMask))break;
  if(n==128)return false;
  mcpwm_ll_trigger_soft_capture(local,2);
  for(n=0;n<128;++n)if(local->int_raw.val&CurrentMask)break;
  if(n==128)return false;
  lo=mcpwm_ll_capture_get_value(local,2);
  local->int_clr.val=CurrentMask;
  for(n=0;n<128;++n)if(!(local->int_raw.val&CurrentMask))break;
  if(n==128)return false;
  mcpwm_ll_trigger_soft_capture(gnss,1);
  mcpwm_ll_trigger_soft_capture(local,2);
  for(n=0;n<128;++n)if(local->int_raw.val&CurrentMask)break;
  if(n==128||uint32_t(esp_cpu_get_cycle_count()-start)>24000)return false;
  hi=mcpwm_ll_capture_get_value(local,2);
  return true;
}
W12_STACK_FRAME bool current(w12::fine::Time& t){
  uint32_t raw=0;
  return latch(raw)&&counter.current(1,raw,coarse(),t);
}
[[gnu::noinline]] bool IRAM_ATTR bt_aperture(mcpwm_dev_t* local,uint32_t& lo,uint32_t& hi,
    uint32_t* slots,uint32_t* half_us,W12FineBleRead read){
  const auto start=esp_cpu_get_cycle_count();
  local->int_clr.val=CurrentMask;unsigned n=0;
  for(;n<128;++n)if(!(local->int_raw.val&CurrentMask))break;
  if(n==128)return false;
  mcpwm_ll_trigger_soft_capture(local,2);
  for(n=0;n<128;++n)if(local->int_raw.val&CurrentMask)break;
  if(n==128)return false;
  lo=mcpwm_ll_capture_get_value(local,2);
  local->int_clr.val=CurrentMask;
  for(n=0;n<128;++n)if(!(local->int_raw.val&CurrentMask))break;
  if(n==128||!read(slots,half_us))return false;
  mcpwm_ll_trigger_soft_capture(local,2);
  for(n=0;n<128;++n)if(local->int_raw.val&CurrentMask)break;
  if(n==128||uint32_t(esp_cpu_get_cycle_count()-start)>24000)return false;
  hi=mcpwm_ll_capture_get_value(local,2);return true;
}
bool pending_ok(){
  auto* d=timer->group->hal.dev;
  const auto pending=d->int_raw.val&RadioMask;
  // Read pending values before CURRENT: a capture can occur during the latch.
  uint32_t raw=0;if(pending&(1u<<27))raw=mcpwm_ll_capture_get_value(d,0);
  w12::fine::Time now{},edge{};
  return (lora_borrowed||!(pending&(1u<<28)))&&current(now)&&
    (!(pending&(1u<<27))||(counter.event(1,raw,edge)&&receiver.pending(edge.raw80)));
}
bool on_edge(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t* value,void*){
  portENTER_CRITICAL_ISR(&mux);
  const auto now=coarse();
  inc(stats.callbacks);
  if(!active||!value||now<burst_at){error=-10;contain();}
  else {
    if(now-burst_at>=10000){burst_at=now;burst=0;}
    w12::fine::Time anchor{},edge{};
    if(++burst>64||sequence==UINT32_MAX||!current(anchor)||!counter.event(1,value->cap_value,edge)){
      error=-11;contain();
    }else{
      receiver.observe({edge.raw80,edge.tick20,now,++sequence,value->cap_value,1,0,uint8_t(value->cap_edge),true});
      if(receiver.status().fault){error=-12;contain();}
    }
  }
  portEXIT_CRITICAL_ISR(&mux);return false;
}
bool on_lora(mcpwm_cap_channel_handle_t,const mcpwm_capture_event_data_t* value,void*){
  uint64_t tick=0;W12LoRaCapture deliver=nullptr;
  portENTER_CRITICAL_ISR(&mux);
  w12::fine::Time anchor{},edge{};
  // CH1 shares the physical timer with BLE, but never its association history,
  // pending-edge fences or software counter origin. Group0 remains GNSS only.
  uint32_t raw=0;
  if(active&&lora_active&&value&&latch(raw)&&lora_counter.current(1,raw,coarse(),anchor)&&
     lora_counter.event(1,value->cap_value,edge)){tick=edge.tick20;deliver=callback;}
  else if(active&&lora_active)deliver=callback; // Invalid edge is refused by RX.
  portEXIT_CRITICAL_ISR(&mux);
  return deliver?deliver(tick):false;
}
W12_STACK_FRAME bool route(){
  // Claim before the first mutating call so every partial failure retires it.
  pad_owned=true;
  if(gpio_set_direction(gpio_num_t(16),GPIO_MODE_INPUT_OUTPUT)!=ESP_OK||
     gpio_pullup_dis(gpio_num_t(16))!=ESP_OK||gpio_pulldown_dis(gpio_num_t(16))!=ESP_OK)return false;
  esp_rom_gpio_connect_out_signal(16,RW_RX_ON_IDX,false,false);
  gpio_ll_set_output_enable_ctrl(&GPIO,16,false,false);
  const auto sig=mcpwm_periph_signals.groups[1].captures[0].cap_sig;
  esp_rom_gpio_connect_in_signal(16,sig,false);
  gpio_io_config_t io{};
  const auto input=GPIO.func_in_sel_cfg[sig].val,output=GPIO.func_out_sel_cfg[16].val;
  return gpio_get_io_config(gpio_num_t(16),&io)==ESP_OK&&io.ie&&io.oe&&io.fun_sel==PIN_FUNC_GPIO&&
    io.sig_out==RW_RX_ON_IDX&&!io.oe_ctrl_by_periph&&!io.oe_inv&&!(output&(1u<<9))&&
    (input&(1u<<7))&&(input&63)==16&&!(input&(1u<<6));
}
W12_STACK_FRAME bool start(uint64_t now){
  mcpwm_capture_timer_config_t cfg{};cfg.group_id=1;cfg.clk_src=MCPWM_CAPTURE_CLK_SRC_APB;
  uint32_t hz=0;
  if(mcpwm_new_capture_timer(&cfg,&timer)!=ESP_OK||
     mcpwm_capture_timer_get_resolution(timer,&hz)!=ESP_OK||hz!=80000000||timer->group->group_id!=1)return false;
  for(unsigned i=0;i<3;++i){
    mcpwm_capture_channel_config_t ch{};ch.gpio_num=-1;ch.prescale=1;
    ch.flags.pos_edge=i<2;ch.flags.neg_edge=i==0;
    if(mcpwm_new_capture_channel(timer,&ch,&channels[i])!=ESP_OK||
       channels[i]->cap_chan_id!=int(i)||channels[i]->cap_timer!=timer)return false;
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT,mcpwm_periph_signals.groups[1].captures[i].cap_sig,false);
  }
  const mcpwm_capture_event_callbacks_t cb={.on_cap=on_edge};
  if(mcpwm_capture_channel_register_event_callbacks(channels[0],&cb,nullptr)!=ESP_OK)return false;
  const mcpwm_capture_event_callbacks_t lc={.on_cap=on_lora};
  if(mcpwm_capture_channel_register_event_callbacks(channels[1],&lc,nullptr)!=ESP_OK)return false;
  capture_off();
  if(mcpwm_capture_timer_enable(timer)!=ESP_OK)return false;
  timer_enabled=true;
  if(mcpwm_capture_timer_start(timer)!=ESP_OK)return false;
  timer_started=true;
  if(mcpwm_capture_channel_enable(channels[2])!=ESP_OK)return false;
  enabled[2]=true;
  uint32_t first=0,second=0;
  if(!latch(first)||!latch(second)||!uint32_t(second-first)||uint32_t(second-first)>=80000||
     coarse()<now||coarse()-now>=1000000||!counter.begin(1,second,coarse(),hz))return false;
  receiver.begin(second,second,1);sequence=burst=0;burst_at=coarse();
  if(!route())return false;
  auto* d=timer->group->hal.dev;d->int_clr.val=RadioMask;
  unsigned n=0;for(;n<128;++n)if(!(d->int_raw.val&RadioMask))break;
  if(n==128)return false;
  // CH1 remains disabled until the LoRa owner borrows it; group0 is GNSS only.
  mcpwm_ll_intr_enable(d,RadioMask,true);
  d->int_ena.val&=~(UINT32_C(1)<<28);
  if((d->int_ena.val&RadioMask)!=(UINT32_C(1)<<27))return false;
  active=true;
  if(mcpwm_capture_channel_enable(channels[0])!=ESP_OK)return false;
  enabled[0]=true;
  return active&&!error;
}
}
bool w12_fine_start(uint64_t boot,uint64_t factory,uint64_t now){
  if(!boot||!now||factory!=PTOB_W12_FINE_FACTORY||timer||active||error)return false;
  if(start(now))return true;
  error=-1;w12_fine_stop();return false;
}
bool w12_fine_current(uint64_t* tick20){
  if(!tick20)return false;
  *tick20=0;portENTER_CRITICAL(&mux);
  w12::fine::Time t{};
  const bool ok=active&&!error&&pending_ok()&&current(t)&&receiver.coordinate(t.raw80,0,*tick20);
  if(!ok&&active){error=-2;contain();}
  portEXIT_CRITICAL(&mux);return ok;
}
bool w12_fine_bt_bracket(uint64_t* before,uint64_t* after,uint32_t* slots,uint32_t* half_us,W12FineBleRead read){
  if(before)*before=0;
  if(after)*after=0;
  if(slots)*slots=0;
  if(half_us)*half_us=0;
  if(!before||!after||before==after||!slots||!half_us||slots==half_us||!read)return false;
  portENTER_CRITICAL(&mux);
  uint32_t lo=0,hi=0,observed_slots=0,observed_half=0;
  bool ok=active&&!error&&pending_ok()&&
    bt_aperture(timer->group->hal.dev,lo,hi,&observed_slots,&observed_half,read)&&
    observed_slots<=0x0fffffff&&observed_half<=624;
  w12::fine::Time a{},b{};
  const auto now=coarse();
  if(ok)ok=counter.current(1,lo,now,a)&&counter.current(1,hi,now,b)&&
    receiver.coordinate(a.raw80,0,*before)&&receiver.coordinate(b.raw80,0,*after)&&*after>=*before;
  if(ok){*slots=observed_slots;*half_us=observed_half;}else *before=*after=0;
  portEXIT_CRITICAL(&mux);return ok;
}
bool w12_fine_gnss_map_bracket(mcpwm_cap_channel_t* channel,uint64_t* before,uint64_t* after){
  if(before)*before=0;
  if(after)*after=0;
  if(!channel||!before||!after||before==after||!channel->cap_timer||
     !channel->cap_timer->group||channel->cap_chan_id!=1||
     channel->fsm!=MCPWM_CAP_CHAN_FSM_ENABLE||
     channel->cap_timer->fsm!=MCPWM_CAP_TIMER_FSM_ENABLE||
     channel->cap_timer->resolution_hz!=80000000||
     channel->cap_timer->group->group_id!=0)return false;
  auto* gnss=channel->cap_timer->group->hal.dev;
  if(!gnss||!gnss->cap_timer_cfg.cap_timer_en||!gnss->cap_chn_cfg[1].capn_en)return false;
  portENTER_CRITICAL(&mux);
  bool ok=active&&!error&&pending_ok();
  uint32_t lo=0,hi=0;
  if(ok){
    // Keep counter extension, BLE association checks and coordinate conversion
    // outside the aperture. This is the exact pinned public soft-catch primitive
    // on the validated GNSS channel, between bounded driver-owned CURRENT latches.
    ok=gnss_aperture(timer->group->hal.dev,gnss,lo,hi);
  }
  w12::fine::Time a{},b{};
  if(ok)ok=counter.current(1,lo,coarse(),a)&&counter.current(1,hi,coarse(),b)&&
    receiver.coordinate(a.raw80,0,*before)&&receiver.coordinate(b.raw80,0,*after)&&*after>=*before;
  if(!ok)*before=*after=0;
  portEXIT_CRITICAL(&mux);return ok;
}
int w12_fine_receive(v12::periodic_rx::Stamp& stamp){
  portENTER_CRITICAL(&mux);int outcome=0;w12::fine::Time t{};w12::fine::Receiver::Match found{};
  const bool live=active&&!error&&pending_ok()&&current(t);
  if(!live){if(active){error=-3;contain();}}
  else if(stamp.actual&&stamp.association_observed&&stamp.radio_reference_applied){
    // Legacy PDU159 remains descriptor-only on the SAME counter.
    // Current GR3 and RC7 PDU191 both use the empirical fine convention.
    // Its packet class has no reviewed fine phase convention. No guessed bias.
    if(stamp.phy==0&&stamp.pdu_length==159&&stamp.ext_header==0){inc(stats.direct);outcome=2;}
    else {
      const auto result=receiver.claim(stamp.raw_tick20(),stamp.map_enclosure_ns,stamp.phy,stamp.pdu_length,
        stamp.ext_header,stamp.epoch,stamp.sequence,t.raw80,(timer->group->hal.dev->int_raw.val>>27)&1,found);
      if(result==w12::fine::Receiver::Result::Pending){inc(stats.pending);outcome=1;}
      else if(result==w12::fine::Receiver::Result::Ready){
        uint64_t tick=0,available=0;uint32_t guard=UINT32_MAX;
        if(receiver.coordinate(found.fall.raw80,UINT64_C(125440),tick)&&receiver.coordinate(t.raw80,0,available)&&
           tick<=available&&w12::fine::Receiver::lifetime_guard(stamp.tick20,tick,stamp.map_enclosure_ns,guard)){
          stamp.tick20=tick;stamp.available20=available;stamp.radio_extra_us=0;
          stamp.refinement_applied=false;stamp.refinement_ticks=0;stamp.map_enclosure_ns=guard;
          stamp.edge_calibrated=false;inc(stats.claims);outcome=2;
        }
      }else if(result==w12::fine::Receiver::Result::Fault){error=-4;contain();}
      else if(result==w12::fine::Receiver::Result::Missing)inc(stats.missing);
      else if(result==w12::fine::Receiver::Result::Ambiguous)inc(stats.ambiguous);
      else if(result==w12::fine::Receiver::Result::Unsupported)inc(stats.unsupported);
    }
  }
  portEXIT_CRITICAL(&mux);return outcome;
}
void w12_fine_stop(){
  portENTER_CRITICAL(&mux);contain();portEXIT_CRITICAL(&mux);
  bool ok=true;
  if(pad_owned){
    const auto sig=mcpwm_periph_signals.groups[1].captures[0].cap_sig;
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT,sig,false);
    esp_rom_gpio_connect_out_signal(16,SIG_GPIO_OUT_IDX,false,false);
    if(gpio_reset_pin(gpio_num_t(16))!=ESP_OK)ok=false;
    pad_off();gpio_io_config_t io{};
    if(gpio_get_io_config(gpio_num_t(16),&io)!=ESP_OK||io.oe||io.sig_out!=SIG_GPIO_OUT_IDX||
       GPIO.func_in_sel_cfg[sig].val!=(UINT32_C(1)<<7|GPIO_MATRIX_CONST_ZERO_INPUT))ok=false;
    if(ok)pad_owned=false;
  }
  // Retain every failed owner for a later cleanup attempt; no false OFF ack.
  for(unsigned i=0;i<3;++i)if(channels[i]){
    if(enabled[i]||channels[i]->fsm==MCPWM_CAP_CHAN_FSM_ENABLE){
      if(mcpwm_capture_channel_disable(channels[i])!=ESP_OK){ok=false;continue;}
      enabled[i]=false;
    }
    if(mcpwm_del_capture_channel(channels[i])!=ESP_OK)ok=false;else channels[i]=nullptr;
  }
  if(timer){
    if(timer_started){if(mcpwm_capture_timer_stop(timer)!=ESP_OK)ok=false;else timer_started=false;}
    if(timer_enabled){if(mcpwm_capture_timer_disable(timer)!=ESP_OK)ok=false;else timer_enabled=false;}
    if(!channels[0]&&!channels[1]&&!channels[2]&&!timer_started&&!timer_enabled){
      if(mcpwm_del_capture_timer(timer)!=ESP_OK)ok=false;else timer=nullptr;
    }
  }
  if(!ok)error=-5;
}
bool w12_fine_released(){return !timer&&!channels[0]&&!channels[1]&&!channels[2]&&!pad_owned&&!active;}
bool w12_fine_fault(){
  portENTER_CRITICAL(&mux);const bool failed=error!=0;portEXIT_CRITICAL(&mux);return failed;
}
W12FineStatus w12_fine_status(){
  portENTER_CRITICAL(&mux);auto result=stats;result.active=active;result.error=error;
  portEXIT_CRITICAL(&mux);return result;
}

bool w12_fine_lora_start(W12LoRaCapture cb){
 if(!cb)return false;
 portENTER_CRITICAL(&mux);
 if(!active||error||lora_borrowed||enabled[1]){portEXIT_CRITICAL(&mux);return false;}
 uint32_t raw=0;
 if(!latch(raw)||!lora_counter.begin(1,raw,coarse(),80000000)){portEXIT_CRITICAL(&mux);return false;}
 callback=cb;lora_active=lora_borrowed=true;
 esp_rom_gpio_connect_in_signal(w12::lora_board::IrqGpio,mcpwm_periph_signals.groups[1].captures[1].cap_sig,false);
 timer->group->hal.dev->int_clr.val=UINT32_C(1)<<28;
 mcpwm_ll_intr_enable(timer->group->hal.dev,UINT32_C(1)<<28,true);
 portEXIT_CRITICAL(&mux);
 const bool ok=mcpwm_capture_channel_enable(channels[1])==ESP_OK;
 portENTER_CRITICAL(&mux);enabled[1]=ok;if(!ok){lora_active=false;callback=nullptr;}
 portEXIT_CRITICAL(&mux);return ok;
}
bool w12_fine_lora_current(uint64_t* tick20){
 if(!tick20)return false;
 *tick20=0;portENTER_CRITICAL(&mux);
 uint32_t raw=0;w12::fine::Time t{};
 const bool ok=active&&!error&&lora_active&&latch(raw)&&lora_counter.current(1,raw,coarse(),t);
 if(ok)*tick20=t.tick20;
 portEXIT_CRITICAL(&mux);return ok;
}
bool w12_fine_lora_stop(){
 portENTER_CRITICAL(&mux);lora_active=false;callback=nullptr;portEXIT_CRITICAL(&mux);
 bool ok=true;
 if(enabled[1]){ok=mcpwm_capture_channel_disable(channels[1])==ESP_OK;if(ok)enabled[1]=false;}
 portENTER_CRITICAL(&mux);
 if(timer){
  auto* d=timer->group->hal.dev;mcpwm_ll_intr_enable(d,UINT32_C(1)<<28,false);
  esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT,mcpwm_periph_signals.groups[1].captures[1].cap_sig,false);
  d->int_clr.val=UINT32_C(1)<<28;
  unsigned tries=0;for(;tries<128;++tries)if(!(d->int_raw.val&(UINT32_C(1)<<28)))break;
  ok=ok&&tries<128;
 }
 if(ok)lora_borrowed=false;
 portEXIT_CRITICAL(&mux);return ok;
}

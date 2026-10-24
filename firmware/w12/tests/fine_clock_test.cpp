// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include <cassert>
#include <cstdio>
#include <cstring>
#include "w12_fine_clock.cpp"
void advance(uint64_t us){fake_now+=us;fake_raw+=uint32_t(us*80);}
void edge(uint64_t raw,uint32_t polarity){
  fake_radio_capture[0]=uint32_t(raw);
  const mcpwm_capture_event_data_t e{uint32_t(raw),polarity};
  fake_channels[0].cb(&fake_channels[0],&e,nullptr);
}
int main(int argc,char** argv){
  assert(argc==2);const auto mode=argv[1];
  assert(w12_fine_released()&&!w12_fine_fault()&&!fake_gpio_calls);
  assert(!w12_fine_start(1,PTOB_W12_FINE_FACTORY+1,fake_now)&&!fake_gpio_calls);
  if(!std::strcmp(mode,"route")){fake_gpio_failure=8;fake_gpio_failure_pin=16;}
  if(!std::strcmp(mode,"enable"))fake_enable_fail=0;
  if(!std::strcmp(mode,"timeout"))fake_complete=false;
  if(!std::strcmp(mode,"stopped"))fake_stale=true;
  if(!std::strcmp(mode,"stale_raw")){fake_device.int_raw.val=1u<<29;fake_clear_ok=false;}
  if(!std::strcmp(mode,"stale_radio")){fake_device.int_raw.val=1u<<27;fake_radio_clear_ok=false;}
  if(!std::strcmp(mode,"irq"))fake_irq_restore_ok=false;
  if(std::strcmp(mode,"live")&&std::strcmp(mode,"lora")&&std::strcmp(mode,"gnss")&&std::strcmp(mode,"pending_late")&&std::strcmp(mode,"cleanup")&&
     std::strcmp(mode,"wrap")&&std::strcmp(mode,"storm")&&std::strcmp(mode,"gap")){
    assert(!w12_fine_start(2,PTOB_W12_FINE_FACTORY,fake_now));
    assert(w12_fine_fault()&&!active&&!GPIO.oe[16]);
    fake_gpio_failure=0;w12_fine_stop();assert(w12_fine_released());return 0;
  }
  assert(w12_fine_start(2,PTOB_W12_FINE_FACTORY,fake_now));
  assert(active&&fake_group==1&&fake_callbacks==2&&enabled[0]&&!enabled[1]&&enabled[2]);
  assert(GPIO.oe[16]&&!(fake_device.int_ena.val&(1u<<28)));
  assert(!w12_fine_start(2,PTOB_W12_FINE_FACTORY,fake_now));
  uint64_t tick=0;assert(w12_fine_current(&tick)&&tick>=1000000);
  if(!std::strcmp(mode,"gnss")){
    FakeDevice gps{};gps.cap_timer_cfg.cap_timer_en=true;gps.cap_chn_cfg[1].capn_en=true;
    FakeGroup group{};group.group_id=0;group.hal.dev=&gps;
    mcpwm_cap_timer_t gt{&group,MCPWM_CAP_TIMER_FSM_ENABLE};
    mcpwm_cap_channel_t gc{1,&gt,MCPWM_CAP_CHAN_FSM_ENABLE,nullptr,nullptr};
    uint64_t before=123,after=456;
    const auto gpio_calls=fake_gpio_calls;const auto rx=receiver.status();
    assert(!w12_fine_gnss_map_bracket(nullptr,&before,&after)&&!before&&!after);
    assert(!w12_fine_gnss_map_bracket(&gc,nullptr,&after));
    assert(!w12_fine_gnss_map_bracket(&gc,&before,nullptr));
    assert(!w12_fine_gnss_map_bracket(&gc,&before,&before));
    for(unsigned k=0;k<8;++k){
      auto bad=gc;
      if(k==0)bad.cap_timer=nullptr;
      if(k==1)bad.cap_chan_id=0;
      if(k==2)bad.fsm=0;
      if(k==3)gt.fsm=0;
      if(k==4)group.group_id=1;
      if(k==5)gps.cap_timer_cfg.cap_timer_en=false;
      if(k==6)gps.cap_chn_cfg[1].capn_en=false;
      if(k==7)gt.resolution_hz=40000000;
      assert(!w12_fine_gnss_map_bracket(&bad,&before,&after)&&!before&&!after&&!fake_gnss_triggers);
      gt.fsm=MCPWM_CAP_TIMER_FSM_ENABLE;group.group_id=0;
      gt.resolution_hz=80000000;
      gps.cap_timer_cfg.cap_timer_en=gps.cap_chn_cfg[1].capn_en=true;
    }
    // The actual narrow helper crosses hardware wraps using the sole production
    // counter origin. GNSS triggering must not train or claim any BLE capture.
    for(unsigned i=0;i<61;++i){
      advance(10000000);
      assert(w12_fine_gnss_map_bracket(&gc,&before,&after)&&before>=tick&&after>=before);
      uint64_t capture20=0;
      w12::fine::Time capture{};assert(counter.event(1,fake_gnss_raw,capture));
      assert(receiver.coordinate(capture.raw80,0,capture20)&&before<=capture20&&capture20<=after);
      assert(w12_fine_current(&tick)&&tick>=after);
    }
    assert(fake_gnss_triggers==61&&fake_gpio_calls==gpio_calls&&fake_callbacks==2&&
           receiver.status().callbacks==rx.callbacks&&receiver.status().claimed==rx.claimed);
    fake_complete=false;
    assert(!w12_fine_gnss_map_bracket(&gc,&before,&after)&&!before&&!after);
    assert(!w12_fine_current(&tick)&&w12_fine_fault());
    w12_fine_stop();assert(w12_fine_released());return 0;
  }
  if(!std::strcmp(mode,"lora")){
    assert(!w12_fine_lora_current(&tick)&&tick==0);
    advance(1000);assert(w12_fine_current(&tick));
    const auto ble_origin=tick;
    assert(w12_fine_lora_start(+[](uint64_t t){assert(t>=1000000);return false;}));
    assert(lora_active&&enabled[1]&&fake_device.cap_chn_cfg[1].capn_en);
    advance(1000);mcpwm_capture_event_data_t e{fake_raw,0};fake_channels[1].cb(&fake_channels[1],&e,nullptr);
    assert(w12_fine_current(&tick)&&!error&&active);
    uint64_t lora_tick=0;assert(w12_fine_lora_current(&lora_tick)&&lora_tick<tick&&tick-lora_tick>=20000);
    // Six minutes across hardware wraps with independently maintained epochs.
    for(unsigned i=0;i<37;++i){advance(10000000);assert(w12_fine_current(&tick));assert(w12_fine_lora_current(&lora_tick));assert(tick-lora_tick==ble_origin-1000000);}
    fake_device.int_raw.val|=1u<<28;
    assert(w12_fine_lora_stop()&&!lora_active&&!enabled[1]&&enabled[0]&&active);
    assert(w12_fine_current(&tick)&&!error&&active);
    assert(!w12_fine_lora_current(&lora_tick)&&!lora_tick);
    advance(1000);assert(w12_fine_current(&tick));
    assert(w12_fine_lora_start(+[](uint64_t t){assert(t>=1000000);return false;}));
    // A BLE association failure must not be evaluated by the LoRa reader.
    receiver.observe({});assert(receiver.status().fault);
    assert(w12_fine_lora_current(&lora_tick)&&active&&!error);
    e.cap_value=fake_raw;fake_channels[1].cb(&fake_channels[1],&e,nullptr);
    assert(w12_fine_lora_stop());
    assert(!w12_fine_current(&tick)&&w12_fine_fault());
    w12_fine_stop();assert(w12_fine_released());return 0;
  }
  if(!std::strcmp(mode,"gap")){
    advance(w12::fine::Counter::HalfWrapUs);
    assert(!w12_fine_current(&tick)&&!tick&&!active&&w12_fine_fault());
  }else if(!std::strcmp(mode,"storm")){
    for(unsigned i=0;i<65;++i)edge(fake_raw,0);
    assert(!active&&w12_fine_fault()&&!GPIO.oe[16]);
  }else {
    const auto origin=uint64_t(fake_raw);const auto center=origin+800000;
    advance(12000);edge(center-10000,0);edge(center+125440,1);
    v12::periodic_rx::Stamp stamp{};
    stamp.tick20=tick+200000-80;stamp.radio_extra_us=4;stamp.available20=tick;
    stamp.epoch=1;stamp.sequence=1;stamp.actual=stamp.association_observed=true;
    stamp.radio_reference_applied=stamp.radio_reference_valid=true;stamp.map_enclosure_ns=0;
    stamp.phy=0;stamp.pdu_length=191;
    assert(w12_fine_receive(stamp)==1);advance(2000);assert(w12_fine_receive(stamp)==2);
    assert(stamp.tick20==tick+200000&&!stamp.edge_calibrated&&stamp.map_enclosure_ns>0);
    // The same immutable capture cannot be claimed a second time.
    assert(!w12_fine_receive(stamp));
    auto direct=stamp;direct.pdu_length=159;
    const auto prior=direct.tick20;assert(w12_fine_receive(direct)==2&&direct.tick20==prior);
    direct.pdu_length=160;assert(!w12_fine_receive(direct));
    if(!std::strcmp(mode,"pending_late")){
      fake_radio_capture[0]=uint32_t(center+120000);fake_device.int_raw.val|=1u<<27;
      assert(!w12_fine_current(&tick)&&!active&&w12_fine_fault()&&!GPIO.oe[16]);
    }
    if(!std::strcmp(mode,"wrap")){
      for(unsigned i=0;i<61;++i){advance(10000000);assert(w12_fine_current(&tick));}
      assert(active&&!w12_fine_fault());
    }
  }
  if(!std::strcmp(mode,"cleanup")){
    fake_disable_ok=false;w12_fine_stop();assert(!w12_fine_released()&&w12_fine_fault()&&!GPIO.oe[16]);
    fake_disable_ok=true;
  }
  w12_fine_stop();assert(w12_fine_released()&&!GPIO.oe[16]);
  assert(!w12_fine_current(&tick)&&!tick);
  std::puts("PASS W12 actual RX-only fine owner; no physical RF/UTC claim");
}

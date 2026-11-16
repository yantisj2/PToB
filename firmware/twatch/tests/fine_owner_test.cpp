// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <initializer_list>
#define V12_R2_ENABLED 1
#define PTOB_N30 1
#include "../main/fine_capture.cpp"
#define CHECK(condition) do {if(!(condition)){std::fprintf(stderr,"FAIL line%d: %s\n",__LINE__,#condition);std::exit(1);}} while(0)
namespace v12 {
bool ConsoleFrame::format(const char* fmt,...){va_list args;va_start(args,fmt);const int n=std::vsnprintf(bytes+length,sizeof(bytes)-length,fmt,args);va_end(args);if(n<=0||unsigned(n)>=sizeof(bytes)-length){failed=true;return false;}for(int i=0;i<n;++i)rows+=bytes[length+i]=='\n';length+=uint16_t(n);return true;}
}
namespace {
void clear_error(){
 fake_gpio_failure=0;fake_gpio_failure_pin=-1;fake_enable_fail=-1;fake_enable_inject=nullptr;
 fake_disable_ok=fake_radio_clear_ok=fake_irq_restore_ok=true;n30_fine_stop();CHECK(n30_fine_released());
 error=0;fake_complete=fake_clear_ok=true;fake_stale=false;fake_cycle_step=32;fake_device.int_raw.val=0;
}
[[maybe_unused]] void edge(unsigned polarity=1){
 mcpwm_capture_event_data_t event{fake_raw,polarity};CHECK(channels[0]->cb);
 channels[0]->cb(channels[0],&event,channels[0]->context);
}
[[maybe_unused]] void storm(unsigned channel){if(channel==0)for(unsigned i=0;i<65;++i)edge();}
void save_frame(const char* name){static v12::ConsoleFrame f;f={};n30_fine_frame(f,0,fake_now);CHECK(!f.failed);FILE* file=std::fopen(name,"w");CHECK(file);std::fwrite(f.bytes,1,f.length,file);std::fclose(file);}
}
int main(){
 CHECK(!n30_fine_backend_selected());
 CHECK(n30_fine_released()&&fake_allocated==0&&fake_gpio_calls==0);
 CHECK(!n30_fine_start(1,0,fake_now,1000,1)&&fake_gpio_calls==0);
 CHECK(!n30_fine_probe(1,PTOB_N30_FINE_FACTORY,fake_now,100,4));
 CHECK(!n30_fine_backend_start(1,PTOB_N30_FINE_FACTORY,fake_now));
 // Keep group0 (LoRa) input routes as witnesses for every lifecycle/failure.
 for(unsigned i=166;i<=168;++i)GPIO.func_in_sel_cfg[i].val=0xabcd0000+i;
#if PTOB_N30_FINE_PINS
 // Actual ESP32-S3 reset readback: selector256, OE0, OEN_SEL0.
 GPIO.func_out_sel_cfg[17].val=SIG_GPIO_OUT_IDX;
#endif
 save_frame("fine-initial-frame.txt");
#if PTOB_N30_FINE_PINS
 // An existing output owner is refused before allocation or pad mutation.
 GPIO.oe[17]=true;GPIO.enable|=1u<<17;
 const auto owned_route=GPIO.func_out_sel_cfg[17].val;
 CHECK(!n30_fine_start(1,PTOB_N30_FINE_FACTORY,fake_now,1000,1));
 CHECK(error==-27&&fake_allocated==0&&GPIO.oe[17]&&GPIO.func_out_sel_cfg[17].val==owned_route&&fake_output_disables[17]==0);
 GPIO.oe[17]=false;GPIO.enable&=~(1u<<17);error=0;
 const auto foreign_disables_before=fake_output_disables[17];
 for(const uint32_t foreign:{uint32_t(RW_TX_ON_IDX),uint32_t(SIG_GPIO_OUT_IDX|(1u<<9)),uint32_t(SIG_GPIO_OUT_IDX|(1u<<11))}){
  GPIO.func_out_sel_cfg[17].val=foreign;
  CHECK(!n30_fine_start(34,PTOB_N30_FINE_FACTORY,fake_now,1000,1)&&error==-27);
  n30_fine_stop();CHECK(!GPIO.oe[17]&&GPIO.func_out_sel_cfg[17].val==foreign&&fake_output_disables[17]==foreign_disables_before);
  error=0;
 }
 GPIO.func_out_sel_cfg[17].val=SIG_GPIO_OUT_IDX;
#endif
 CHECK(n30_fine_start(2,PTOB_N30_FINE_FACTORY,fake_now,1000,1));
 CHECK(active&&fake_group==1&&fake_channels[2].fsm&&!(fake_device.int_ena.val&(1u<<29)));
#if PTOB_N30_FINE_PINS
 CHECK(routed&&fake_callbacks==1&&fake_channels[0].fsm&&!fake_channels[1].fsm);
 CHECK(GPIO.oe[17]&&GPIO.ie[17]&&GPIO.func_out_sel_cfg[17].val==unsigned(RW_RX_ON_IDX+1024));
 CHECK(GPIO.func_in_sel_cfg[175].val==unsigned(128+17)&&GPIO.func_in_sel_cfg[176].val==unsigned(128+GPIO_MATRIX_CONST_ZERO_INPUT));
 CHECK((fake_device.int_ena.val&(3u<<27))==(1u<<27));
 edge();CHECK(history.rx()==1&&history.tx()==0);
#else
 CHECK(!routed&&fake_callbacks==0&&!fake_channels[0].fsm&&!fake_channels[1].fsm&&fake_gpio_calls==0);
#endif
 static v12::ConsoleFrame frame;frame={};n30_fine_frame(frame,0,fake_now);
 CHECK(!frame.failed&&std::strstr(frame.bytes,"TX=NONE_CONSTANT0")&&std::strstr(frame.bytes,"capture_channels=0/2 tx_channel=1_CONSTANT0")&&std::strstr(frame.bytes,"backend=0 phase80=0"));
 {FILE* file=std::fopen("fine-frame.txt","w");CHECK(file);std::fwrite(frame.bytes,1,frame.length,file);std::fclose(file);}
 fake_now=history.until();n30_fine_poll(fake_now);CHECK(n30_fine_released());
#if PTOB_N30_FINE_PINS
 CHECK(!GPIO.oe[17]&&!fake_device.cap_chn_cfg[1].capn_en&&!(fake_device.int_ena.val&(3u<<27)));
 clear_error();CHECK(n30_fine_start(3,PTOB_N30_FINE_FACTORY,fake_now,1000,1));
 for(unsigned i=0;i<65;++i)edge();CHECK(contained&&!active&&error==-18&&budget_refused==1&&!GPIO.oe[17]);
 clear_error();fake_enable_inject=storm;CHECK(!n30_fine_start(4,PTOB_N30_FINE_FACTORY,fake_now,1000,1)&&error==-18&&n30_fine_released());
 clear_error();fake_enable_fail=0;CHECK(!n30_fine_start(5,PTOB_N30_FINE_FACTORY,fake_now,1000,1)&&error==-7&&n30_fine_released()&&!GPIO.oe[17]);
 for(int operation=1;operation<=8;++operation){
  clear_error();fake_gpio_failure_pin=17;fake_gpio_failure=operation;
  if(operation==4){fake_gpio_failure=0;CHECK(n30_fine_start(10+operation,PTOB_N30_FINE_FACTORY,fake_now,1000,1));fake_gpio_failure=operation;n30_fine_stop();CHECK(!n30_fine_released()&&n30_fine_cleanup_fault());}
  else CHECK(!n30_fine_start(10+operation,PTOB_N30_FINE_FACTORY,fake_now,1000,1));
  // Read-only ownership refusals need no cleanup; all partial mutations do.
  if(operation!=5&&operation!=6)CHECK(!GPIO.oe[17]);
 }
 clear_error();fake_radio_clear_ok=false;fake_device.int_raw.val=1u<<27;
 CHECK(!n30_fine_start(30,PTOB_N30_FINE_FACTORY,fake_now,1000,1)&&error==-19&&n30_fine_released()&&!GPIO.oe[17]);
 clear_error();fake_irq_restore_ok=false;CHECK(!n30_fine_start(31,PTOB_N30_FINE_FACTORY,fake_now,1000,1)&&error==-20&&n30_fine_released());
 clear_error();CHECK(n30_fine_window_start(32,PTOB_N30_FINE_FACTORY,fake_now,1000));
 CHECK(window_owned&&fake_device.cap_chn_cfg[0].capn_en&&!fake_device.cap_chn_cfg[1].capn_en);
 auto advance=[](uint64_t us){fake_now+=us;fake_raw+=uint32_t(us*80);};
 // First window backfills from the original start, then seals and retires RX.
 advance(100);edge();const auto first_lower=last_raw80+800;
 advance(20);edge(0);advance(20);edge(1);const auto first_upper=last_raw80+800;
 CHECK(n30_fine_window_begin(1,first_lower,first_upper,incarnation));
 advance(50);n30_fine_poll(fake_now);
 CHECK(n30_fine_window_status().sealed&&n30_fine_window_status().fault==0);
 CHECK(n30_fine_window_rearm()&&fake_device.cap_chn_cfg[0].capn_en&&!fake_device.cap_chn_cfg[1].capn_en);
 // Force32-record history rollover. A real RX predecessor is required;
 // the disabled TX channel must not manufacture a NoPrehistory fault.
 for(unsigned i=0;i<40;++i){advance(200);edge(i&1u);}
 const auto second_lower=last_raw80+800;
 advance(20);edge(0);advance(20);edge(1);const auto second_upper=last_raw80+800;
 CHECK(n30_fine_window_begin(2,second_lower,second_upper,incarnation));
 advance(50);n30_fine_poll(fake_now);const auto second=n30_fine_window_status();
 CHECK(second.sealed&&second.fault==0&&!second.from_origin&&second.predecessor[0]&&!second.predecessor[1]&&second.count==2);
 n30_fine_window_stop();CHECK(n30_fine_released()&&!GPIO.oe[17]);
 save_frame("fine-released-frame.txt");
 GPIO.oe[17]=true;GPIO.enable|=1u<<17;
 const auto disabled_before=fake_output_disables[17];const auto foreign_route=GPIO.func_out_sel_cfg[17].val;
 CHECK(!n30_fine_start(33,PTOB_N30_FINE_FACTORY,fake_now,1000,1)&&error==-27);
 n30_fine_stop();CHECK(GPIO.oe[17]&&GPIO.func_out_sel_cfg[17].val==foreign_route&&fake_output_disables[17]==disabled_before);
 GPIO.oe[17]=false;GPIO.enable&=~(1u<<17);error=0;
#endif
 // RX prehistory, sequence holes, wrong-channel data and late callbacks keep
 // their original fences even though no TX predecessor is expected.
 {
  using namespace n30::fine;
  History h;CHECK(h.start(1,1000000,10000,1));
  for(unsigned i=0;i<40;++i)h.append({20000+i*10,0,0,0,20000+i*10,1,0,uint8_t(i&1),true});
  Window w;w.reset({1000,0,1000,1000,1,11000000,1,80000000});
  CHECK(w.begin(1,20000,21000,h,1)&&bool(w.status().fault&NoPrehistory));
  w.observe({20500,0,0,41,20500,1,1,0,true});CHECK(w.status().fault&InvalidEdge);
  w.observe({20501,0,0,43,20501,1,0,0,true});CHECK(w.status().fault&NoPrehistory);
  w.seal(22000,43,0,0,4,0);w.observe({20502,0,0,44,20502,1,0,1,true});
  CHECK(w.status().late_count==1&&w.status().late_sequence[0]==44);
 }
 clear_error();fake_start_delay=2000000;const auto calls=fake_gpio_calls;
 CHECK(!n30_fine_start(40,PTOB_N30_FINE_FACTORY,fake_now,1000,1)&&n30_fine_released());
#if !PTOB_N30_FINE_PINS
 CHECK(fake_gpio_calls==calls);
#else
 CHECK(!GPIO.oe[17]);(void)calls;
#endif
 fake_start_delay=0;clear_error();
 for(unsigned i=166;i<=168;++i)CHECK(GPIO.func_in_sel_cfg[i].val==0xabcd0000+i);
 std::puts("PASS actual watch RX17-only owner:default off,identity/ownership,single RX IRQ,constant-zero TX,finite expiry,partial/startup/IRQ faults,stop release,group0 isolation; production backend disabled");
}

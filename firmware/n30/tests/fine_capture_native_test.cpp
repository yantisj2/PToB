// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#define V12_R2_ENABLED 1
#define PTOB_N30 1
#define PTOB_N30_FINE_PINS 1
#include "../main/fine_capture.cpp"
namespace v12 {
bool ConsoleFrame::format(const char* fmt,...){va_list args;va_start(args,fmt);const int n=std::vsnprintf(bytes+length,sizeof(bytes)-length,fmt,args);va_end(args);if(n<=0||unsigned(n)>=sizeof(bytes)-length){failed=true;return false;}for(int i=0;i<n;++i)rows+=bytes[length+i]=='\n';length+=uint16_t(n);return true;}
}
void clear_error(){fake_gpio_failure=0;fake_gpio_failure_pin=-1;fake_enable_fail=-1;fake_enable_inject=nullptr;fake_disable_ok=fake_radio_clear_ok=fake_irq_restore_ok=true;n30_fine_stop();assert(n30_fine_released());error=0;fake_complete=fake_clear_ok=true;fake_stale=false;fake_cycle_step=32;fake_device.int_raw.val=0;}
void delivered_callback(unsigned channel){mcpwm_capture_event_data_t e{fake_raw,1};assert(channels[channel]->cb);channels[channel]->cb(channels[channel],&e,channels[channel]->context);}
void edge(unsigned channel){if(fake_device.cap_chn_cfg[channel].capn_en&&(fake_device.int_ena.val&(1u<<(27+channel))))delivered_callback(channel);}
void storm_during_enable(unsigned channel){if(channel==0)for(unsigned i=0;i<65;++i)edge(0);}
int main(){
 assert(n30_fine_released()&&fake_allocated==0&&fake_gpio_calls==0);for(unsigned i=166;i<=168;++i)GPIO.func_in_sel_cfg[i].val=0xabcd0000+i;
 assert(!n30_fine_start(1,0,fake_now,1000,1)&&fake_gpio_calls==0);
 for(unsigned periph=0;periph<2;++periph)for(unsigned inv=0;inv<2;++inv){gpio_ll_set_output_enable_ctrl(&GPIO,4,bool(periph),bool(inv));gpio_io_config_t io{};assert(gpio_get_io_config(4,&io)==0&&io.oe_ctrl_by_periph==bool(periph)&&io.oe_inv==bool(inv));}
 assert(n30_fine_start(1,PTOB_N30_FINE_FACTORY,fake_now,1000,1));
 assert(active&&routed&&fake_group==1&&fake_callbacks==2&&fake_channels[0].fsm&&fake_channels[1].fsm&&fake_channels[2].fsm);
 assert(GPIO.oe[4]&&GPIO.oe[5]&&GPIO.ie[4]&&GPIO.ie[5]);assert(GPIO.func_out_sel_cfg[4].val==unsigned(NativeRxSignal+1024)&&GPIO.func_out_sel_cfg[5].val==unsigned(NativeTxSignal+1024));assert(GPIO.func_in_sel_cfg[175].val==132&&GPIO.func_in_sel_cfg[176].val==133);
 static v12::ConsoleFrame frame;frame={};n30_fine_frame(frame,0,fake_now);assert(!frame.failed&&snapshot.rx_out==unsigned(NativeRxSignal+1024)&&snapshot.tx_out==unsigned(NativeTxSignal+1024)&&snapshot.rx_in==132&&snapshot.tx_in==133&&snapshot.oe==3&&snapshot.channel_mask==7&&snapshot.irq_mask==3);
 {FILE* out=std::fopen(PTOB_N30_FINE_RW?"fine-rw-radio-frame.txt":"fine-native-radio-frame.txt","w");assert(out);std::fwrite(frame.bytes,1,frame.length,out);std::fclose(out);}
 assert((fake_device.int_ena.val&(3u<<27))==(3u<<27)&&!(fake_device.int_ena.val&(1u<<29)));
 for(unsigned i=0;i<65;++i)edge(i%2);
 assert(contained&&!active&&error==-18&&total_callbacks==65&&history.seen()==64&&budget_refused==1);
 assert(!GPIO.oe[4]&&!GPIO.oe[5]&&!(fake_device.int_ena.val&(3u<<27))&&!fake_device.cap_chn_cfg[0].capn_en&&!fake_device.cap_chn_cfg[1].capn_en&&fake_device.cap_chn_cfg[2].capn_en);
 delivered_callback(0);assert(inactive_callbacks==1&&!GPIO.oe[4]&&!GPIO.oe[5]);clear_error();
 assert(n30_fine_start(2,PTOB_N30_FINE_FACTORY,fake_now,1000,1));fake_now=history.until();edge(0);assert(contained&&!active&&expired_refused==1&&!GPIO.oe[4]&&!GPIO.oe[5]);clear_error();
 // Real public enable does not set peripheral INT_ENA: restore on EVERY lease.
 assert(n30_fine_start(40,PTOB_N30_FINE_FACTORY,fake_now,1000,1));fake_now=history.until();edge(0);assert(contained&&!active&&error==0);n30_fine_poll(fake_now);assert(n30_fine_released());
 fake_device.int_raw.val=(3u<<27)|(1u<<10);
 assert(n30_fine_start(41,PTOB_N30_FINE_FACTORY,fake_now,1000,1));assert((fake_device.int_ena.val&(3u<<27))==(3u<<27)&&!(fake_device.int_raw.val&(3u<<27))&&(fake_device.int_raw.val&(1u<<10)));assert(history.seen()==0);edge(0);assert(history.seen()==1);n30_fine_stop();assert(n30_fine_released()&&!(fake_device.int_ena.val&(3u<<27)));delivered_callback(0);assert(inactive_callbacks==1);
 assert(n30_fine_start(42,PTOB_N30_FINE_FACTORY,fake_now,1000,1));assert((fake_device.int_ena.val&(3u<<27))==(3u<<27));n30_fine_stop();clear_error();
 fake_radio_clear_ok=false;fake_device.int_raw.val=3u<<27;assert(!n30_fine_start(43,PTOB_N30_FINE_FACTORY,fake_now,1000,1));assert(error==-19&&n30_fine_released()&&!GPIO.oe[4]&&!GPIO.oe[5]);clear_error();
 fake_irq_restore_ok=false;assert(!n30_fine_start(44,PTOB_N30_FINE_FACTORY,fake_now,1000,1));assert(error==-20&&n30_fine_released()&&!GPIO.oe[4]&&!GPIO.oe[5]);clear_error();
 fake_enable_inject=storm_during_enable;assert(!n30_fine_start(3,PTOB_N30_FINE_FACTORY,fake_now,1000,1));assert(error==-18&&contained&&n30_fine_released()&&!fake_channels[1].fsm);clear_error();
 for(int pin=4;pin<=5;++pin)for(int operation=1;operation<=8;++operation){
  fake_gpio_failure_pin=pin;fake_gpio_failure=operation;
  // reset-pin fails during release, not setup; exercise it separately below.
  if(operation==4){fake_gpio_failure=0;assert(n30_fine_start(10+pin,PTOB_N30_FINE_FACTORY,fake_now,1000,1));fake_gpio_failure=operation;n30_fine_stop();assert(!n30_fine_released()&&n30_fine_cleanup_fault());}
  else {assert(!n30_fine_start(10+pin,PTOB_N30_FINE_FACTORY,fake_now,1000,1));assert(error);}
  // A failure on either pad cannot leave the other output enabled.
  assert(!GPIO.oe[4]&&!GPIO.oe[5]&&fake_output_disables[4]&&fake_output_disables[5]);clear_error();
 }
 for(int channel=0;channel<2;++channel){fake_enable_fail=channel;assert(!n30_fine_start(20+channel,PTOB_N30_FINE_FACTORY,fake_now,1000,1));assert(error==-7&&n30_fine_released()&&!GPIO.oe[4]&&!GPIO.oe[5]);clear_error();}
 fake_start_delay=2000000;const auto calls=fake_gpio_calls;assert(!n30_fine_start(30,PTOB_N30_FINE_FACTORY,fake_now,1000,1));assert(n30_fine_released()&&fake_gpio_calls==calls);fake_start_delay=0;clear_error();
 assert(n30_fine_start(31,PTOB_N30_FINE_FACTORY,fake_now,1000,1));n30_fine_stop();assert(n30_fine_released()&&!GPIO.oe[4]&&!GPIO.oe[5]);

 // A known fixed, slow GPIO source isolates CH0/1 independently of native RF.
 assert(!n30_fine_probe(50,PTOB_N30_FINE_FACTORY,fake_now,49,16));
 assert(!n30_fine_probe(50,PTOB_N30_FINE_FACTORY,fake_now,100,15));
 assert(n30_fine_probe(50,PTOB_N30_FINE_FACTORY,fake_now,100,16));assert(probe.synthetic&&probe.initial_low==3&&history.seen()==0);
 assert(GPIO.func_out_sel_cfg[4].val==1280&&GPIO.func_out_sel_cfg[5].val==1280);
 assert(!n30_fine_start(51,PTOB_N30_FINE_FACTORY,fake_now,1000,1)&&probe.synthetic);
 const auto lease_end=history.until();
 auto advance=[](uint64_t us){fake_now+=us;fake_raw+=uint32_t(us*80);};
 for(unsigned i=0;i<16;++i){advance(100000);n30_fine_poll(0);assert(probe.attempted==i+1&&history.seen()==i+1&&history.until()==lease_end);}
 assert(probe.issued[0]==8&&probe.issued[1]==8&&probe.rise[0]==4&&probe.fall[0]==4&&probe.rise[1]==4&&probe.fall[1]==4&&probe.verified_levels==0);
 assert(history.rx()==8&&history.tx()==8&&history.overwritten()==0);
 advance(200000);n30_fine_poll(0);assert(probe.attempted==16);n30_fine_stop();const auto stopped=probe.stopped_us;assert(probe.synthetic&&stopped-probe.last_issue_us>=200000&&n30_fine_released());
 frame={};n30_fine_frame(frame,0,0);assert(!frame.failed&&std::strstr(frame.bytes,"mode=SYNTHETIC_GPIO")&&std::strstr(frame.bytes,"provenance=SYNTHETIC_GPIO")&&std::strstr(frame.bytes,"RX=GPIO4_SYNTHETIC"));
 {FILE* out=std::fopen("fine-native-frame.txt","w");assert(out);std::fwrite(frame.bytes,1,frame.length,out);std::fclose(out);}
 // Refused or failed starts cannot relabel retained history after stop.
 const auto old_first=first_raw32,old_inc=incarnation;const auto old_latest=last_raw80;
 fake_complete=false;assert(!n30_fine_start(52,PTOB_N30_FINE_FACTORY,fake_now,1000,1)&&probe.synthetic&&probe.stopped_us==stopped&&first_raw32==old_first&&last_raw80==old_latest&&incarnation==old_inc&&history.seen()==16&&last_start.timeouts==1);clear_error();
 fake_start_delay=2000000;assert(!n30_fine_start(52,PTOB_N30_FINE_FACTORY,fake_now,1000,1)&&probe.synthetic&&probe.stopped_us==stopped&&first_raw32==old_first&&last_raw80==old_latest&&incarnation==old_inc&&history.seen()==16);fake_start_delay=0;clear_error();
 assert(n30_fine_probe(53,PTOB_N30_FINE_FACTORY,fake_now,100,4));advance(400000);n30_fine_poll(0);assert(probe.attempted==1);n30_fine_poll(0);assert(probe.attempted==1); // no catchup
 fake_now=history.until();const auto attempts=probe.attempted;n30_fine_poll(0);assert(probe.attempted==attempts&&n30_fine_released());
 // A missing capture edge remains missing, never invented from CPU writes.
 clear_error();fake_gpio_capture=false;assert(n30_fine_probe(54,PTOB_N30_FINE_FACTORY,fake_now,100,4));advance(100000);n30_fine_poll(0);assert(probe.attempted==1&&probe.issued[0]==1&&history.seen()==0);n30_fine_stop();fake_gpio_capture=true;clear_error();
 // Initial/readback/API failures preserve attempted-versus-verified counts.
 for(int pin=4;pin<=5;++pin)for(int failure=9;failure<=10;++failure){fake_gpio_failure_pin=pin;fake_gpio_failure=failure;assert(!n30_fine_probe(55,PTOB_N30_FINE_FACTORY,fake_now,100,4)&&error==(failure==9?-21:-22)&&n30_fine_released());clear_error();}
 assert(n30_fine_probe(57,PTOB_N30_FINE_FACTORY,fake_now,100,4));fake_gpio_failure_pin=4;fake_gpio_failure=9;advance(100000);n30_fine_poll(0);assert(error==-21&&probe.attempted==1&&probe.issued[0]==0&&contained&&n30_fine_released());clear_error();
 assert(n30_fine_probe(56,PTOB_N30_FINE_FACTORY,fake_now,100,4));fake_gpio_failure_pin=4;fake_gpio_failure=10;advance(100000);n30_fine_poll(0);assert(error==-23&&probe.attempted==1&&probe.issued[0]==0&&contained&&n30_fine_released());clear_error();
 for(unsigned i=166;i<=168;++i)assert(GPIO.func_in_sel_cfg[i].val==0xabcd0000+i);
 std::puts("PASS actual NATIVE branch: GPIO4/5 route/readback, all8 API/matrix failure classes eachpad, partial2ndchannel enable, bothpad failclosed cleanup, startupIRQstorm/expiredlease IRQ containment, ISR preservesCH2 and group1-only API guards");
}

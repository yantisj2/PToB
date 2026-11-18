// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Actual watch owner with fake registers; packet endpoint and loss behavior.
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#define V12_R2_ENABLED 1
#define PTOB_N30 1
#include "../main/fine_capture.cpp"
#include "g1_corrected_clock.hpp"
#include "watch_holdover.hpp"
namespace v12 {
bool ConsoleFrame::format(const char* fmt,...){va_list args;va_start(args,fmt);const int n=std::vsnprintf(bytes+length,sizeof(bytes)-length,fmt,args);va_end(args);if(n<=0||unsigned(n)>=sizeof(bytes)-length){failed=true;return false;}for(int i=0;i<n;++i)rows+=bytes[length+i]=='\n';length+=uint16_t(n);return true;}
}
void advance(uint64_t us){fake_now+=us;fake_raw+=uint32_t(us*80);}
void edge(uint64_t raw,unsigned polarity){fake_radio_capture[0]=uint32_t(raw);const mcpwm_capture_event_data_t e{uint32_t(raw),polarity};fake_channels[0].cb(&fake_channels[0],&e,fake_channels[0].context);}
watch::HoldoverMonitor monitor;
v12::n30_probe::Pair last_pair;
void observe(const v12::n30_probe::Pair& p){last_pair=p;monitor.pair(p);}
void late_readback(int channel){if(channel==2)fake_now+=100;}
void backward_readback(int channel){if(channel==2)fake_now-=2;}
unsigned bridge_reads=0;int bridge_mode=0;
bool reference_read(uint64_t* out){
 ++bridge_reads;if(bridge_reads==2&&bridge_mode==1)advance(30);
 *out=fake_now*20;return !(bridge_reads==2&&bridge_mode==2);
}
int main(){
 static_assert(sizeof(packet_edges)==1);
 assert(n30_fine_backend_selected()&&n30_fine_released());
 GPIO.func_out_sel_cfg[17].val=SIG_GPIO_OUT_IDX;
 for(unsigned i=166;i<=168;++i)GPIO.func_in_sel_cfg[i].val=0xabcd0000+i;
 assert(!n30_fine_start(1,PTOB_N30_FINE_FACTORY,fake_now,1000,1));
 assert(!n30_fine_window_start(1,PTOB_N30_FINE_FACTORY,fake_now,1000));
 assert(n30_fine_backend_start(2,PTOB_N30_FINE_FACTORY,fake_now));
 assert(active&&production_owned&&history.until()==UINT64_MAX&&routed&&fake_callbacks==2&&!fake_channels[1].fsm);
 assert(!n30_fine_window_start(3,PTOB_N30_FINE_FACTORY,fake_now,1000));
 assert(!n30_fine_window_begin(1,1,2,incarnation)&&!n30_fine_window_refuse(1)&&!n30_fine_window_rearm());
 const uint64_t origin=first_raw32;uint64_t current20=0;
 assert(n30_fine_backend_current(&current20)&&current20>=1000000);
 WatchClockBridge paired;bridge_reads=0;
 assert(watch_clock_bridge(reference_read,paired)&&paired.valid&&paired.incarnation==incarnation&&paired.fine20);
 assert(bridge_reads==2&&paired.xtal_after20>=paired.xtal_before20&&paired.xtal_after20-paired.xtal_before20<=400);
 for(int refusal:{1,2}){bridge_mode=refusal;bridge_reads=0;
  assert(!watch_clock_bridge(reference_read,paired)&&!paired.valid);
  assert(active&&!contained&&!error&&n30_fine_backend_current(&current20));
 }
 bridge_mode=0;
 // Real33 failed with poll-ready but4153 CPU cycles. CPU latency does not
 // imply peripheral timeout; readback still has a monotonic100us deadline.
 const auto prior_wall_peak=wall_max_us;
 fake_cycle_step=4153;uint32_t current_raw=0;
 assert(latch(current_raw)&&cycles_max>=4153&&wall_max_us==prior_wall_peak&&!error);
 fake_cycle_step=32;
 const auto saved_now=fake_now;const auto saved_raw=fake_raw;
 for(const auto hook:{late_readback,backward_readback}){
  fake_capture_read_inject=hook;current_raw=0xdeadbeef;
  assert(!latch(current_raw)&&error==-28&&current_raw==0xdeadbeef);
  fake_capture_read_inject=nullptr;error=0;fake_now=saved_now;fake_raw=saved_raw;
 }
 fake_complete=false;current_raw=0xdeadbeef;
 assert(!latch(current_raw)&&error==-1&&current_raw==0xdeadbeef);
 fake_complete=true;error=0;
 fake_device.int_raw.val|=1u<<29;fake_clear_ok=false;
 assert(!latch(current_raw)&&error==-14&&current_raw==0xdeadbeef);
 fake_clear_ok=true;fake_device.int_raw.val=0;error=0;
 // Timeout negatives are isolated register tests, not an operational restart.
 timeouts=0;wall_max_us=1;
 const auto center=origin+800000;advance(12000);edge(center-10000,0);edge(center+PTOB_N30_FINE_TRANSLATION80+10,1);
 v12::periodic_rx::Stamp stamp{};stamp.tick20=1199920;stamp.radio_extra_us=4;stamp.epoch=1;stamp.sequence=1;
 stamp.actual=stamp.association_observed=stamp.radio_reference_applied=stamp.radio_reference_valid=true;stamp.map_enclosure_ns=0;stamp.phy=0;stamp.pdu_length=191;
 // Unsupported endpoint classes cannot consume a fall or contaminate G1;
 // signed/coarse packet data and the live peripheral remain intact.
 for(unsigned bad=0;bad<8;++bad){auto other=stamp;
  if(bad==0)other.pdu_length=159;
  if(bad==1)other.phy=1;
  if(bad==2)other.ext_header=1;
  if(bad==3)other.radio_extra_us=0;
  if(bad==4)other.radio_reference_valid=false;
  if(bad==5)other.radio_reference_applied=false;
  if(bad==6)other.actual=false;
  if(bad==7)other.association_observed=false;
  const auto before=other;
  assert(n30_fine_backend_receive(other)==0&&active&&!error&&receiver.status().claimed==0);
  assert(other.tick20==before.tick20&&other.actual==before.actual&&other.association_observed==before.association_observed&&other.epoch==before.epoch);
 }
 assert(n30_fine_backend_receive(stamp)==1);advance(2000);
 assert(n30_fine_backend_receive(stamp)==2&&stamp.tick20==1200003&&stamp.tick20%10==3);
 assert(stamp.available20>stamp.tick20&&!stamp.edge_calibrated&&stamp.radio_extra_us==0&&!stamp.refinement_applied);
 assert(stamp.map_enclosure_ns==4175&&backend_stats.fall80==center+PTOB_N30_FINE_TRANSLATION80+10&&receiver.status().claimed==1);
 static v12::ConsoleFrame f;f={};n30_fine_frame(f,0,fake_now);
 assert(!f.failed&&std::strstr(f.bytes,"mode=TWATCH_NATIVE_RW_RX17_G1")&&std::strstr(f.bytes,"clock_update=G1_RX")&&std::strstr(f.bytes,"backend=1 phase80=125808"));
 {FILE* file=std::fopen("fine-backend-frame.txt","w");assert(file);std::fwrite(f.bytes,1,f.length,file);std::fclose(file);}
 // Missing/ambiguous associations retain the source packet but supply no fine
 // sample. A later unique packet still works without a peripheral reset.
 auto second=stamp;second.tick20=1400000-80;second.radio_extra_us=4;second.sequence=2;
 advance(20000);assert(n30_fine_backend_receive(second)==0&&active&&!error);
 const auto next=origin+2400000;advance(10000);edge(next-10000,0);edge(next-9000,0);edge(next+PTOB_N30_FINE_TRANSLATION80,1);advance(5000);
 second.tick20=1600000-80;second.sequence=3;
 assert(n30_fine_backend_receive(second)==0&&backend_stats.ambiguous==1&&active&&!error);
 // End to end: actual substituted fall -> G1 corrected observation -> Pair ->
 // the real watch holdover monitor. Descriptor ticks must not leak back into
 // the empirical observer after fine_sink/rolling-correction association.
 v12::G1CorrectedClock clock;v12::G1Anchor anchor{true,111,55,0,0,0,2,3,40000};
 v12::n30_probe::pair_sink=observe;
 uint64_t prior_remote=0,prior_fine=0,prior_descriptor=0;uint16_t prior_event=0;
 for(unsigned i=0;i<1000;++i){
  const auto d=last_raw80+5000000;advance(70000);
  edge(d-10000,0);edge(d+PTOB_N30_FINE_TRANSLATION80+(i%4)*10,1);advance(4000);
  auto fine=stamp;fine.tick20=n30::fine::Counter::Base20+(d-origin)/4-80;fine.radio_extra_us=4;
  fine.actual=fine.association_observed=fine.radio_reference_applied=fine.radio_reference_valid=true;
  fine.epoch=2;fine.sequence=i+1;fine.event=uint16_t(i+1);fine.channel=uint8_t(i%37);fine.map_enclosure_ns=1000;
  const auto descriptor=fine.tick20;assert(n30_fine_backend_receive(fine)==2);
  const auto remote=UINT64_C(1000000000000)+fine.tick20*50;
  r2_relay_packet p{};
  p.origin={0,1,55,2,remote-500000000,remote+1000000000,0,INT64_C(1700000000000000000),0,0};
  p.route={0,0,0,0,55,3,i+1,9,3,UINT32_MAX};
  if(i){p.count=1;p.correction[0]={i,prior_event,uint8_t((i-1)%37),prior_remote*2/125,3};}
  anchor.at=fake_now;anchor.until=fake_now+1500000;
  clock.receive(p,fine,fine.available20,fake_now,anchor);
  if(i){assert(last_pair.local20==prior_fine&&last_pair.local20!=prior_descriptor&&last_pair.epoch==2);}
  const auto prediction=clock.prediction();
  v12::R2Status s{};s.requested=s.following=s.clock_valid=true;s.g1_frequency_trained=clock.frequency_trained();
  s.root_identity=111;s.root_boot=55;s.source_epoch=2;s.publication=3;s.g1_clock_origin=111;s.rx_capture.epoch=2;
  s.follower_snapshot_us=fake_now;s.follower_utc_us=UINT64_C(1700000000000000)+fake_now;
  s.g1_anchor_deadline_us=s.service_until_us=s.g1_corrected_until_us=fake_now+1500000;
  s.g1_clock_model=prediction;s.g1_relative_quality=clock.quality(fake_now,fine.available20);
  s.g1_relative_quality.evaluated20=fine.available20; // native snapshot's actual CURRENT read
  monitor.update(s,fake_now,false);
  prior_remote=remote;prior_fine=fine.tick20;prior_descriptor=descriptor;prior_event=fine.event;
 }
 assert(clock.frequency_trained()&&clock.quality(fake_now,prior_fine).rms_ns<200);
 const auto h=monitor.status();std::fprintf(stderr,"PIPELINE reason=%s ready=%u ms=%u started=%u completed=%u unknown=%u ignored=%u resets=%u\n",h.reason,unsigned(h.view.ready),h.view.duration_ms,h.view.started,h.view.completed,h.view.unknown,h.ignored,h.resets);
 assert(monitor.status().view.ready&&monitor.status().view.duration_ms>=2000&&monitor.status().view.unknown==0);
 // Persist beyond a finite observer lease and32-bit wrap without imposing its
 // callback budget. CH1/TX and the LoRa group0 routes stay untouched.
 for(unsigned i=0;i<61;++i){advance(10000000);assert(n30_fine_backend_current(&current20));n30_fine_poll(0);}
 total_callbacks=500000;edge(last_raw80-100,0);assert(active&&!error&&total_callbacks==500001);
 assert(history.until()==UINT64_MAX&&!fake_channels[1].fsm&&history.tx()==0);
 for(unsigned i=166;i<=168;++i)assert(GPIO.func_in_sel_cfg[i].val==0xabcd0000+i);
 // PPS CH1 is independent of RX0 and the production clock. Its pending IRQ
 // must neither starve packet association nor become a late radio marker.
 GPIO.func_out_sel_cfg[13].val=SIG_GPIO_OUT_IDX;
 assert(watch_pps_start(99,PTOB_N30_FINE_FACTORY,fake_now));
 assert(pps_enabled&&!pps_clock_owned&&production_owned);
 const auto radio_seen=history.seen(),claimed_before=receiver.status().claimed;
 fake_device.int_raw.val|=1u<<28;uint64_t with_pps=0;
 assert(n30_fine_backend_current(&with_pps));
 mcpwm_capture_event_data_t physical{fake_raw,0};channels[1]->cb(channels[1],&physical,nullptr);
 WatchPpsCapture observed;assert(watch_pps_take(observed,nullptr)&&observed.valid&&observed.enclosure_ns==50);
 assert(history.seen()==radio_seen&&receiver.status().claimed==claimed_before&&!receiver.status().fault);
 assert(watch_pps_stop()&&production_owned&&active&&!channel_enabled[1]);
 // A fresh accepted window followed by a late pending edge must withdraw the
 // owner before CURRENT/finalize can release an invalid value.
 const auto d=last_raw80+800000;advance(12000);edge(d-10000,0);edge(d+PTOB_N30_FINE_TRANSLATION80,1);advance(2000);
 second.tick20=n30::fine::Counter::Base20+(d-origin)/4-80;second.radio_extra_us=4;second.epoch=3;second.sequence=1;
 assert(n30_fine_backend_receive(second)==2);
 fake_radio_capture[0]=uint32_t(d+120000);fake_device.int_raw.val|=1u<<27;
 assert(!n30_fine_backend_current(&current20)&&contained&&!active&&receiver.status().fault&&!GPIO.oe[17]);
 n30_fine_stop();assert(n30_fine_released());

 std::puts("PASS actual watch continuous RX17 G1: endpoint guards, pending/unique/missing/ambiguous,125ns fall to50ns coordinate, lifetime enclosure,persistent wrap,TXzero/LoRa isolation,late failclosed");
}

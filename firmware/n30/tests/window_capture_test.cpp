// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "fine_fake.hpp"
#include "../main/window_diagnostics.cpp"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <initializer_list>
namespace v12 {
TransportStatus transport_status(){return {};}
bool ConsoleFrame::format(const char* fmt,...){va_list a;va_start(a,fmt);const int n=std::vsnprintf(bytes+length,sizeof(bytes)-length,fmt,a);va_end(a);if(n<=0||unsigned(n)>=sizeof(bytes)-length){failed=true;return false;}for(int i=0;i<n;++i)rows+=bytes[length+i]=='\n';length+=uint16_t(n);return true;}
}
void advance(uint32_t us){fake_now+=us;fake_raw+=us*80;}
bool read20(uint64_t* out){*out=fake_now*20;return true;}
void edge(unsigned ch,uint32_t raw,uint32_t polarity){const mcpwm_capture_event_data_t e{raw,polarity};assert(fake_channels[ch].cb);fake_channels[ch].cb(&fake_channels[ch],&e,fake_channels[ch].context);}
void save(const char* name,const v12::ConsoleFrame& f){FILE* o=std::fopen(name,"w");assert(o);std::fwrite(f.bytes,1,f.length,o);std::fclose(o);}
void selected(unsigned sequence,unsigned event){
 v12::periodic_rx::Stamp c{};c.tick20=fake_now*20-20000;c.available20=fake_now*20;c.epoch=3;c.sequence=sequence;c.actual=true;c.association_observed=true;c.event=uint16_t(event);c.channel=7;c.map_enclosure_ns=5000;c.pdu_length=159;c.phy=0;c.radio_reference_valid=true;
 const adv_rx::BleAnchor a{fake_now*20-40,fake_now*20,{1,2}};
 n30_window_capture(c,a,-72);v12::n30_probe::Pair pair{};pair.root=1;pair.boot=2;pair.source=4;pair.local20=c.tick20;pair.available20=c.available20;pair.epoch=c.epoch;pair.event=c.event;pair.channel=c.channel;pair.causal=true;n30_window_pair(pair);
 v12::Report r{};r.kind=6;r.callback_us=fake_now;r.length=r.actual_length=158;r.bytes[0]=157;r.bytes[1]=r.bytes[2]=r.bytes[3]=255;r.rx_stamp=c;r.rssi=-72;
 n30_window_report(r);
}
int main(){
 {n30::fine::Window core;n30::fine::History h;assert(h.start(13,1,1000,1));
  h.append({4000,1000750,10,0,4000,1,0,0,true});core.reset({1000,0,1000,1000,13,1000001,1,80000000});
  assert(core.begin(1,3000,5000,h,1));core.seal(6000,1,0,0,4,0);assert(core.skip(2));auto s=core.status();
  assert(s.id==2&&s.count==0&&s.lower80==0&&s.upper80==0&&s.seal80==0&&s.begin_sequence==0&&s.seal_sequence==0&&s.pending==0&&s.irq==0&&s.channels==0&&!s.from_origin);
  core.observe({4000,1000750,20,2,4000,1,0,1,true});assert(core.status().late_mask==1);
 }
 //128 immutable interval fences, first-late provenance and paged IDs.
 {static n30::fine::Window core;n30::fine::History h;assert(h.start(14,1,1000,1));
  core.reset({1,0,1,1,14,600000001,1,80000000});
  for(unsigned i=1;i<=128;++i){const uint64_t lo=1000+uint64_t(i)*10000;assert(core.begin(i,lo,lo+100,h,1));core.seal(lo+101,1000+i,0,0,4,0);}
  assert(!core.skip(129));unsigned seq=5000;for(unsigned id:{1u,32u,33u,64u,65u,96u,97u,128u}){
   const uint64_t raw=1050+uint64_t(id)*10000;core.observe({raw,1000000+(raw-1)/4,1,seq++,uint32_t(raw),1,0,1,true});}
  const auto s=core.status();assert(s.late_count==8&&s.late_callbacks==8&&s.late_words[3]==0x80000001&&s.late_mask==0x80000001);
  n30::fine::Window::Late late[4]{};assert(core.late(0,late,4)==4&&late[0].id==1&&late[3].id==64);
  const auto first=late[0].sequence;core.observe({11050,1002762,2,9999,11050,1,0,1,true});assert(core.late(0,late,4)==4&&late[0].sequence==first&&core.status().late_callbacks==9);
  assert(core.late(64,late,4)==4&&late[0].id==65&&late[3].id==128&&core.late(128,late,4)==0);
 }
 assert(sizeof(v12::periodic_rx::Stamp)==48);assert(crc32(reinterpret_cast<const uint8_t*>("123456789"),9)==0xcbf43926);
 n30_window_init(read20);assert(!n30_window_start(1,0,fake_now,1000,2));assert(fake_allocated==0);
 assert(n30_window_start(9,PTOB_N30_FINE_FACTORY,fake_now,10000,2));const auto inc=n30_fine_window_status().incarnation;
 assert(!n30_window_start(10,PTOB_N30_FINE_FACTORY,fake_now,1000,1));assert(!n30_fine_probe(10,PTOB_N30_FINE_FACTORY,fake_now,100,4));
 advance(10000);edge(0,fake_raw-1000,0);edge(1,fake_raw-800,1);selected(1,16);assert(stats.selected==1&&stats.state==Collecting&&slot.pair_valid);
 edge(0,fake_raw-40,1);edge(1,fake_raw-20,0);advance(5000);n30_window_poll();
 auto w=n30_fine_window_status();assert(w.sealed&&!w.fault&&w.count==4&&w.channels==4&&w.irq==0&&stats.state==Frozen&&fake_device.cap_timer_cfg.cap_timer_en);
 static v12::ConsoleFrame frame;frame={};n30_window_frame(frame,false,0);assert(frame.rows==6&&!frame.failed);save("window-packet.txt",frame);
 frame={};n30_window_frame(frame,true,0);assert(frame.rows==6&&!frame.failed);save("window-edges.txt",frame);const auto first_lower=w.lower80,first_upper=w.upper80;
 assert(!n30_window_ack(2));assert(n30_window_ack(1));assert(stats.state==Armed&&n30_fine_window_status().incarnation==inc&&fake_device.cap_chn_cfg[0].capn_en&&fake_device.cap_chn_cfg[1].capn_en&&(fake_device.int_ena.val&(3u<<27))==(3u<<27));
 advance(10000);selected(2,32);edge(0,fake_raw-40,0);edge(1,fake_raw-20,1);advance(5000);n30_window_poll();w=n30_fine_window_status();assert(w.sealed&&!w.fault&&stats.state==Frozen&&w.incarnation==inc);
 // Already-dispatched callback after quiescence must not mutate frozen rows;
 // its raw-time membership invalidates the earlier retained interval.
 const auto count=w.count;edge(0,uint32_t(first_lower+(first_upper-first_lower)/2),0);w=n30_fine_window_status();assert(w.late_mask==1&&w.count==count);
 frame={};n30_window_frame(frame,false,0);assert(frame.rows==6&&!frame.failed);save("window-packet-second.txt",frame);
 frame={};n30_window_frame(frame,false,0,true);assert(frame.rows==6&&!frame.failed);save("window-late.txt",frame);
 assert(n30_window_ack(2));assert(!token.load()&&n30_fine_released()&&n30_fine_window_status().late_mask==1);
 // Default/failed starts do not create physical resource ownership or relabel.
 assert(!n30_window_start(11,PTOB_N30_FINE_FACTORY,fake_now,1000,129));assert(n30_fine_released());
 assert(n30_window_start(12,PTOB_N30_FINE_FACTORY,fake_now,1000,128));assert(stats.goal==128);advance(1000001);n30_window_poll();assert(!token.load()&&n30_fine_released());
 // Actual formatter worst-width data, while ordinary fields keep their layout.
 stats.id=UINT64_MAX;stats.goal=stats.selected=128;stats.acked=127;stats.state=Invalid;stats.active=false;slot.present=true;slot.reason=UINT32_MAX;
 stats.seen=stats.candidates=stats.decimated=stats.refused=stats.prelease=stats.occupied=stats.cache_overwritten=stats.cache_missing=stats.pair_missing=stats.pair_ambiguous=stats.pair_late=stats.queue_base=stats.queue_selected=stats.queue_current=UINT32_MAX;stats.until_us=UINT64_MAX;
 slot.report.length=slot.report.actual_length=190;slot.report.bytes[0]=189;slot.report.bytes[1]=slot.report.bytes[2]=slot.report.bytes[3]=255;slot.crc=crc32(slot.report.bytes,190);slot.report.callback_us=slot.selected_us=slot.cache_us=UINT64_MAX;slot.report.handle=UINT16_MAX;slot.report.rssi=INT8_MIN;
 slot.report.rx_stamp.tick20=slot.report.rx_stamp.available20=UINT64_MAX;slot.report.rx_stamp.epoch=slot.report.rx_stamp.sequence=slot.report.rx_stamp.coarse=slot.report.rx_stamp.map_enclosure_ns=UINT32_MAX;slot.report.rx_stamp.fine=slot.report.rx_stamp.raw_metadata=UINT16_MAX;slot.report.rx_stamp.event=65520;slot.report.rx_stamp.channel=36;slot.report.rx_stamp.activity=slot.report.rx_stamp.phy=255;slot.report.rx_stamp.pdu_length=0;
 slot.anchor={UINT64_MAX,UINT64_MAX,{UINT32_MAX,UINT32_MAX}};slot.worker={UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT32_MAX,UINT32_MAX};slot.main=slot.worker;slot.pair.root=slot.pair.boot=slot.pair.local20=slot.pair.available20=slot.pair.remote_ns=slot.pair.created_ns=slot.pair.expires_ns=UINT64_MAX;slot.pair.predicted_ns=INT64_MIN;slot.pair.source=slot.pair.train=slot.pair.publication=slot.pair.epoch=UINT32_MAX;slot.pair.event=UINT16_MAX;slot.pair.channel=255;slot.pair_valid=false;
 frame={};n30_window_frame(frame,false,UINT32_MAX);assert(frame.rows==6&&!frame.failed&&frame.length<6000);save("window-max-packet.txt",frame);
 std::printf("Window bytes=%zu; status bytes=%zu; retained windows=%u; journal buffers=%zu\n",sizeof(n30::fine::Window),sizeof(n30::fine::WindowStatus),n30::fine::Window::Maximum,sizeof(cache)+sizeof(slot)+sizeof(exported)+sizeof(stats)+sizeof(exported_stats)+sizeof(batch)+sizeof(late_batch)+sizeof(ad_hex));
 std::printf("PASS actual RW window adapter: continuouscounter/quiesce/rearm/immutableAD+Pair/exactcursors/latefence/expiry; max6rows=%u\n",frame.length);
}

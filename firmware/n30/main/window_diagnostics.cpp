// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
// Discovery evidence only. No calls to clock receive(), model or permissions.
#include "window_diagnostics_api.hpp"
#include "fine_capture.hpp"
#include "fine_backend_api.hpp"
#include "native_transport.hpp"
#include "bounded_log.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include <atomic>
#include <cinttypes>
#include <cstring>
namespace {
enum State : uint8_t {Off,Armed,Collecting,Frozen,Invalid,Done};
enum Reason : uint32_t {AnchorMissing=1,WorkerBridgeMissing=2,SearchRefused=4,WindowRefused=8,OwnerWithdrawn=16,WindowIncomplete=32};
struct Cache {v12::periodic_rx::Stamp capture{};adv_rx::BleAnchor anchor{};n30::fine::Bridge bridge{};v12::n30_probe::Pair pair{};uint64_t at_us=0;int8_t rssi=127;bool bridge_valid=false,pair_valid=false;};
struct Slot {v12::Report report{};adv_rx::BleAnchor anchor{};n30::fine::Bridge worker{},main{};v12::n30_probe::Pair pair{};uint64_t selected_us=0,cache_us=0;uint32_t crc=0,reason=0;bool present=false,anchor_valid=false,main_valid=false,pair_valid=false;};
struct Stats {uint64_t id=0,admitted_us=0,until_us=0,armed_us=0;uint32_t goal=0,seen=0,candidates=0,decimated=0,refused=0,prelease=0,prearm=0,occupied=0,selected=0,acked=0,cache_overwritten=0,cache_missing=0,pair_missing=0,pair_ambiguous=0,pair_late=0,queue_base=0,queue_selected=0,queue_current=0;State state=Off;bool active=false;};
portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint64_t> token{0};bool (*read_clock20)(uint64_t*)=nullptr;
Cache cache[8]{};unsigned cache_head=0,cache_count=0;Slot slot{};Stats stats{};
// Sole main response owner: no Report/64-edge array on ISR/main/NimBLE stack.
Slot exported{};Stats exported_stats{};n30::fine::Edge batch[4]{};n30::fine::Window::Late late_batch[4]{};char ad_hex[381]{};
static_assert(sizeof(cache)+sizeof(slot)+sizeof(exported)+sizeof(stats)+sizeof(exported_stats)+sizeof(batch)+sizeof(late_batch)+sizeof(ad_hex)<5120,"Packet journal static buffers exceed5KiB");
uint64_t now_us(){const auto t=esp_timer_get_time();return t>0?uint64_t(t):0;}
void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
uint32_t crc32(const uint8_t* p,unsigned n){uint32_t c=UINT32_MAX;for(unsigned i=0;i<n;++i){c^=p[i];for(unsigned k=0;k<8;++k)c=(c>>1)^(UINT32_C(0xedb88320)&uint32_t(-int32_t(c&1)));}return ~c;}
bool same(const v12::periodic_rx::Stamp& s,const v12::n30_probe::Pair& p){return s.epoch==p.epoch&&s.tick20==p.local20&&s.event==p.event&&s.channel==p.channel;}
bool ready(){return slot.present&&(stats.state==Frozen||stats.state==Invalid||stats.state==Done||stats.state==Off);}
}
void n30_window_init(bool (*read20)(uint64_t*)){read_clock20=read20;}
bool n30_window_start(uint64_t id,uint64_t factory,uint64_t admitted,uint32_t ms,uint32_t count){
 if(token.load(std::memory_order_relaxed)||!read_clock20||!id||!admitted||count<1||count>n30::fine::Window::Maximum||ms<1000||ms>600000)return false;
 if(!n30_fine_window_start(id,factory,admitted,ms))return false;
 const auto queue=v12::transport_status().queue_drops;
 portENTER_CRITICAL(&mux);stats={};stats.id=id;stats.admitted_us=admitted;stats.until_us=admitted+uint64_t(ms)*1000;stats.armed_us=now_us();stats.goal=count;stats.queue_base=queue;stats.state=Armed;stats.active=true;
 cache_head=cache_count=0;slot.present=false;token.store(id,std::memory_order_release);portEXIT_CRITICAL(&mux);return true;
}
void n30_window_stop(){
 token.store(0,std::memory_order_release);n30_fine_window_stop();
 portENTER_CRITICAL(&mux);stats.active=false;if(stats.state==Collecting){slot.reason|=OwnerWithdrawn;stats.state=Invalid;}else if(stats.state!=Done&&!ready()){stats.state=Off;}portEXIT_CRITICAL(&mux);
}
void n30_window_capture(const v12::periodic_rx::Stamp& s,const adv_rx::BleAnchor& a,int8_t rssi){
 const auto id=token.load(std::memory_order_acquire);if(!id)return;
 n30::fine::Bridge b{};const bool valid=n30_fine_window_bridge(read_clock20,b);const auto now=now_us();
 portENTER_CRITICAL(&mux);
 if(stats.active&&stats.id==id&&token.load(std::memory_order_relaxed)==id&&now>=stats.admitted_us&&now<stats.until_us&&(!valid||b.lease==id)){
  if(cache_count==8)inc(stats.cache_overwritten);else ++cache_count;
  auto& row=cache[cache_head];row.capture=s;row.anchor=a;row.bridge=b;row.at_us=now;row.rssi=rssi;row.bridge_valid=valid;row.pair={};row.pair_valid=false;cache_head=(cache_head+1)%8;
 }
 portEXIT_CRITICAL(&mux);
}
void n30_window_pair(const v12::n30_probe::Pair& p){
 if(!token.load(std::memory_order_acquire))return;
 portENTER_CRITICAL(&mux);unsigned matches=0,index=0;
 for(unsigned i=0;i<cache_count;++i){const auto k=(cache_head+8-cache_count+i)%8;if(same(cache[k].capture,p)){++matches;index=k;}}
 if(matches==1){cache[index].pair=p;cache[index].pair_valid=true;}else if(matches>1)inc(stats.pair_ambiguous);
 if(slot.present&&same(slot.report.rx_stamp,p)){
  if(matches==1&&stats.state==Collecting&&!slot.pair_valid){slot.pair=p;slot.pair_valid=true;}else if(!slot.pair_valid)inc(stats.pair_late);
 }
 portEXIT_CRITICAL(&mux);
}
void n30_window_report(const v12::Report& r){
 const auto id=token.load(std::memory_order_acquire);if(!id||r.kind!=6)return;
 const auto& c=r.rx_stamp;const bool complete=r.actual_length==r.length&&(r.length==158||r.length==190)&&v12::periodic_rx::complete_ad(r.bytes,r.length);
 const auto crc=complete?crc32(r.bytes,r.length):0;const auto queue=v12::transport_status().queue_drops;const auto now=now_us();
 portENTER_CRITICAL(&mux);
 if(!stats.active||stats.id!=id||now<stats.admitted_us||now>=stats.until_us){portEXIT_CRITICAL(&mux);return;}
 inc(stats.seen);stats.queue_current=queue;
 if(r.callback_us<stats.admitted_us){inc(stats.prelease);portEXIT_CRITICAL(&mux);return;}
 if(r.callback_us<stats.armed_us){inc(stats.prearm);portEXIT_CRITICAL(&mux);return;}
 if(r.refusal||r.status||!complete||!c.actual||!c.epoch||!c.sequence||!c.association_observed){inc(stats.refused);portEXIT_CRITICAL(&mux);return;}
 inc(stats.candidates);if(c.event&15){inc(stats.decimated);portEXIT_CRITICAL(&mux);return;}
 if(stats.state!=Armed){inc(stats.occupied);portEXIT_CRITICAL(&mux);return;}
 inc(stats.selected);stats.state=Collecting;stats.queue_selected=queue;
 slot.report=r;slot.anchor={};slot.worker={};slot.main={};slot.pair={};slot.selected_us=now;slot.cache_us=0;slot.crc=crc;slot.reason=0;slot.present=true;slot.anchor_valid=slot.main_valid=slot.pair_valid=false;
 unsigned matches=0,index=0;for(unsigned i=0;i<cache_count;++i){const auto k=(cache_head+8-cache_count+i)%8;if(cache[k].capture.epoch==c.epoch&&cache[k].capture.sequence==c.sequence){++matches;index=k;}}
 if(matches==1){const auto& found=cache[index];slot.anchor=found.anchor;slot.anchor_valid=true;slot.worker=found.bridge;slot.cache_us=found.at_us;slot.pair=found.pair;slot.pair_valid=found.pair_valid;if(!found.bridge_valid)slot.reason|=WorkerBridgeMissing;}
 else {slot.reason|=AnchorMissing;inc(stats.cache_missing);}
 const auto number=stats.selected;portEXIT_CRITICAL(&mux);
 // Main task is the sole packet selector. Worker hooks may only annotate Pair.
 slot.main_valid=n30_fine_window_bridge(read_clock20,slot.main);
 uint64_t lower=0,upper=0;
 if(!slot.reason&&!n30::fine::search(slot.worker,c.raw_tick20(),c.map_enclosure_ns,lower,upper))slot.reason|=SearchRefused;
 if(!slot.reason&&!n30_fine_window_begin(number,lower,upper,slot.worker.incarnation))slot.reason|=WindowRefused;
 if(slot.reason){n30_fine_window_refuse(number);portENTER_CRITICAL(&mux);stats.state=Invalid;portEXIT_CRITICAL(&mux);}
}
void n30_window_poll(){
 n30_fine_poll(now_us());if(!token.load(std::memory_order_acquire))return;
 const auto w=n30_fine_window_status();const auto now=now_us();
 bool stop=false;portENTER_CRITICAL(&mux);
 if(stats.active&&(now<stats.admitted_us||now>=stats.until_us||!w.active||w.error||w.contained||w.current_incarnation!=w.incarnation)){
  stop=true;stats.active=false;if(stats.state==Collecting){slot.reason|=OwnerWithdrawn;stats.state=Invalid;}
 }
 if(stats.state==Collecting&&w.sealed&&(slot.pair_valid||(now>=slot.selected_us&&now-slot.selected_us>=v12::periodic_rx::MaxAgeUs))){
  if(!slot.pair_valid){inc(stats.pair_missing);}if(w.fault){slot.reason|=WindowIncomplete;}stats.state=slot.reason?Invalid:Frozen;
 }
 portEXIT_CRITICAL(&mux);if(stop)n30_window_stop();
}
bool n30_window_ack(uint32_t number){
 n30_window_poll();portENTER_CRITICAL(&mux);
 const bool ok=stats.active&&ready()&&number==stats.selected&&stats.acked+1==number;
 portEXIT_CRITICAL(&mux);if(!ok)return false;
 if(number<stats.goal&&!n30_fine_window_rearm())return false;
 portENTER_CRITICAL(&mux);stats.acked=number;slot.present=false;stats.state=number<stats.goal?Armed:Done;stats.armed_us=now_us();portEXIT_CRITICAL(&mux);
 if(number==stats.goal){n30_window_stop();}return true;
}
void n30_window_frame(v12::ConsoleFrame& f,bool edges,uint32_t after,bool late){
 n30_window_poll();auto w=n30_fine_window_status();
 portENTER_CRITICAL(&mux);exported=slot;exported_stats=stats;const bool present=ready();portEXIT_CRITICAL(&mux);
 const auto& q=exported_stats;const auto& p=exported;const auto& c=p.report.rx_stamp;
 const unsigned count=late?n30_fine_window_late(after,late_batch,4,&w):(edges&&present?n30_fine_window_read(after,batch,4):0);
 f.format("N30_WINDOW_STATUS schema=1 id=%016" PRIx64 " active=%u state=%u goal=%u selected=%u acked=%u window=%u packet_crc=%08" PRIx32 " ready=%u reason=%" PRIu32 " until_us=%" PRIu64 " incarnation=%" PRIu32 " origin80=%" PRIu64 " hz=%" PRIu32 " current80=%" PRIu64 " current_incarnation=%" PRIu32 " owner_active=%u owner_error=%d timeout=%" PRIu32 " invalid=%" PRIu32 " inactive=%" PRIu32 " contained=%u lower80=%" PRIu64 " upper80=%" PRIu64 " capture_from80=%" PRIu64 " seal80=%" PRIu64 " fault=%" PRIu32 " total_edges=%" PRIu32 " begin_seq=%" PRIu32 " seal_seq=%" PRIu32 " oldest=%" PRIu32 " overwritten=%" PRIu32 " pending=%" PRIu32 " IRQ=%" PRIu32 " channels=%" PRIu32 " pred=%" PRIu32 "/%" PRIu32 " pred80=%" PRIu64 "/%" PRIu64 " succ=%" PRIu32 "/%" PRIu32 " succ80=%" PRIu64 "/%" PRIu64 " from_origin=%u late_mask=%" PRIu32 " late=%" PRIu32 "/%" PRIu32 "/%" PRIu32 "/%" PRIu32 "/%" PRIu32 "/%" PRIu32 "/%" PRIu32 "/%" PRIu32 " seen=%" PRIu32 " candidates=%" PRIu32 " decimated=%" PRIu32 " refused=%" PRIu32 " prelease=%" PRIu32 " occupied=%" PRIu32 " cache_overwritten=%" PRIu32 " cache_missing=%" PRIu32 " pair_missing=%" PRIu32 " pair_ambiguous=%" PRIu32 " pair_late=%" PRIu32 " queue=%" PRIu32 "/%" PRIu32 "/%" PRIu32 " after=%" PRIu32 " count=%u armed_us=%" PRIu64 " prearm=%" PRIu32 " late_words=%" PRIu32 "/%" PRIu32 "/%" PRIu32 "/%" PRIu32 " late_count=%" PRIu32 " late_callbacks=%" PRIu32 "\n",
 q.id,unsigned(q.active),unsigned(q.state),unsigned(q.goal),unsigned(q.selected),unsigned(q.acked),unsigned(q.selected),present?p.crc:0,unsigned(present),p.reason,q.until_us,w.incarnation,w.origin80,w.hz,w.current80,w.current_incarnation,unsigned(w.active),w.error,w.timeouts,w.invalid,w.inactive,unsigned(w.contained),w.lower80,w.upper80,w.capture_from80,w.seal80,w.fault,w.count,w.begin_sequence,w.seal_sequence,w.oldest,w.overwritten,w.pending,w.irq,w.channels,w.predecessor[0],w.predecessor[1],w.predecessor80[0],w.predecessor80[1],w.successor[0],w.successor[1],w.successor80[0],w.successor80[1],unsigned(w.from_origin),w.late_mask,w.late_sequence[0],w.late_sequence[1],w.late_sequence[2],w.late_sequence[3],w.late_sequence[4],w.late_sequence[5],w.late_sequence[6],w.late_sequence[7],q.seen,q.candidates,q.decimated,q.refused,q.prelease,q.occupied,q.cache_overwritten,q.cache_missing,q.pair_missing,q.pair_ambiguous,q.pair_late,q.queue_base,q.queue_selected,q.queue_current,after,count,q.armed_us,q.prearm,w.late_words[0],w.late_words[1],w.late_words[2],w.late_words[3],w.late_count,w.late_callbacks);
 if(late){
  for(unsigned i=0;i<4;++i){if(i>=count){f.format("N30_WINDOW_LATE%u present=0\n",i);continue;}
   f.format("N30_WINDOW_LATE%u present=1 window=%" PRIu32 " sequence=%" PRIu32 "\n",i,late_batch[i].id,late_batch[i].sequence);}
 }else if(edges){
  for(unsigned i=0;i<4;++i){if(i>=count){f.format("N30_WINDOW_EDGE%u present=0\n",i);continue;}const auto& e=batch[i];
   f.format("N30_WINDOW_EDGE%u present=1 window=%u packet_crc=%08" PRIx32 " n=%" PRIu32 " raw32=%" PRIu32 " raw80=%" PRIu64 " tick20=%" PRIu64 " seen_us=%" PRIu64 " incarnation=%" PRIu32 " kind=%u polarity=%u extended=%u\n",i,unsigned(q.selected),p.crc,e.number,e.raw32,e.raw80,e.tick20,e.seen_us,e.incarnation,unsigned(e.kind),unsigned(e.polarity),unsigned(e.valid));}
 }else if(!present){f.format("N30_WINDOW_PACKET present=0\n");f.format("N30_WINDOW_STAMP present=0\n");f.format("N30_WINDOW_BRIDGE present=0\n");f.format("N30_WINDOW_PAIR present=0\n");
 }else{
  constexpr char digits[]="0123456789abcdef";for(unsigned i=0;i<p.report.length;++i){ad_hex[2*i]=digits[p.report.bytes[i]>>4];ad_hex[2*i+1]=digits[p.report.bytes[i]&15];}ad_hex[2*p.report.length]=0;
  f.format("N30_WINDOW_PACKET present=1 window=%u crc=%08" PRIx32 " length=%u actual_length=%u callback_us=%" PRIu64 " selected_us=%" PRIu64 " handle=%u status=%u refusal=%u rssi=%d address_type=%u address_le=%02x%02x%02x%02x%02x%02x ad=%s\n",unsigned(q.selected),p.crc,unsigned(p.report.length),unsigned(p.report.actual_length),p.report.callback_us,p.selected_us,unsigned(p.report.handle),unsigned(p.report.status),unsigned(p.report.refusal),int(p.report.rssi),unsigned(p.report.address_type),p.report.address_le[0],p.report.address_le[1],p.report.address_le[2],p.report.address_le[3],p.report.address_le[4],p.report.address_le[5],ad_hex);
  f.format("N30_WINDOW_STAMP present=1 tick20=%" PRIu64 " raw20=%" PRIu64 " available20=%" PRIu64 " epoch=%" PRIu32 " seq=%" PRIu32 " coarse=%" PRIu32 " fine=%u metadata=%u activity=%u event=%u channel=%u actual=%u association=%u event_valid=%u channel_valid=%u calibrated=%u enclosure_ns=%" PRIu32 " refine20=%d refined=%u phy=%u extra_us=%u reference_valid=%u reference_applied=%u pdu_length=%u ext_header=%u descriptor_status=%u pdu_known=%u\n",c.tick20,c.raw_tick20(),c.available20,c.epoch,c.sequence,c.coarse,unsigned(c.fine),unsigned(c.raw_metadata),unsigned(c.activity),unsigned(c.event),unsigned(c.channel),unsigned(c.actual),unsigned(c.association_observed),unsigned(c.event_valid),unsigned(c.channel_valid),unsigned(c.edge_calibrated),c.map_enclosure_ns,int(c.refinement_ticks),unsigned(c.refinement_applied),unsigned(c.phy),unsigned(c.radio_extra_us),unsigned(c.radio_reference_valid),unsigned(c.radio_reference_applied),unsigned(c.pdu_length),unsigned(c.ext_header),unsigned(c.descriptor_status),unsigned(c.pdu_length!=0));
  f.format("N30_WINDOW_BRIDGE present=1 anchor_valid=%u before20=%" PRIu64 " after20=%" PRIu64 " bt_slot=%" PRIu32 " bt_hus=%" PRIu32 " cache_us=%" PRIu64 " workerA80=%" PRIu64 " workerG20=%" PRIu64 " workerB80=%" PRIu64 " worker_incarnation=%" PRIu32 " main_valid=%u mainA80=%" PRIu64 " mainG20=%" PRIu64 " mainB80=%" PRIu64 " main_incarnation=%" PRIu32 "\n",unsigned(p.anchor_valid),p.anchor.before,p.anchor.after,p.anchor.time.half_slots,p.anchor.time.half_us,p.cache_us,p.worker.before80,p.worker.gpt20,p.worker.after80,p.worker.incarnation,unsigned(p.main_valid),p.main.before80,p.main.gpt20,p.main.after80,p.main.incarnation);
  const auto& a=p.pair;f.format("N30_WINDOW_PAIR present=1 matched=%u root=%016" PRIx64 " boot=%016" PRIx64 " source=%" PRIu32 " train=%" PRIu32 " publication=%" PRIu32 " local20=%" PRIu64 " available20=%" PRIu64 " remote_ns=%" PRIu64 " predicted_ns=%" PRId64 " epoch=%" PRIu32 " event=%u channel=%u causal=%u created_ns=%" PRIu64 " expires_ns=%" PRIu64 "\n",unsigned(p.pair_valid),a.root,a.boot,a.source,a.train,a.publication,a.local20,a.available20,a.remote_ns,a.predicted_ns,a.epoch,unsigned(a.event),unsigned(a.channel),unsigned(a.causal),a.created_ns,a.expires_ns);
 }
 f.format("N30_WINDOW_CONTRACT observer=PACKET_CANDIDATES_ONLY domain=MCPWM1_80MHZ marker=RW238_237 edge=BOTH selection=FIRST_EVENT_AND15_ZERO slots=128 callback_capacity=64 search_us=3100_PLUS_OLD_ENCLOSURE bridge=SEARCH_ONLY cursor=NONCONSUMING capture_between_windows=QUIESCED counter=CONTINUOUS polarity=UNSELECTED endpoint=UNKNOWN coalesced=UNKNOWN RF_bound=UNKNOWN UTC_bound=UNKNOWN clock_update=NONE production=%s offset=%s PDU=ORIGINAL_LOADED_SCALARS_ONLY flags=UNKNOWN late=FIRST_SEQUENCE_STICKY128\n",n30_fine_backend_selected()?"MCPWM1_20":"GPTIMER20",n30_fine_backend_selected()?"FINE_SPECIFIC":"LEGACY_ONLY");
}

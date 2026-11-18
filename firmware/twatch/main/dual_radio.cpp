// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "dual_radio.hpp"
#include "dual_profile.hpp"
#include "g1_ble_eligibility.hpp"
#include "bounded_log.hpp"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include <cinttypes>
#include <new>
#if __has_include("twatch_dual_clock_config.h")
#include "twatch_dual_clock_config.h"
#else
namespace watchdual {static constexpr Profile profile{};}
#endif
extern "C" bool n30_lora_reference_read(uint64_t*);
namespace watchdual {
namespace {
using namespace watch::dual;
struct RadioRow {
 uint64_t scope=0,domain=0,available=0,until=0,edge=0,id=0,epoch=0,utc_ns=0;
 uint32_t generation=0,enclosure=UINT32_MAX,settled=0;int32_t phase=0,correction=0;
 int32_t rate_milli_ppb=0;bool rate_valid=false,calibration_match=false;
 uint32_t worker_bracket_ns=UINT32_MAX,main_bracket_ns=UINT32_MAX;uint64_t bridge_epoch=0;
 uint8_t reason=0;bool eligible=false,auth=false,strict=false,map=false,requested=false,owner=false;
};
struct Row {
 uint64_t seen=0,edge=0,epoch=0;uint32_t serial=0,number=0;
 RadioRow ble,lora;ClockView clock;
 bool reference=false,capture=false;
};
struct Frozen {n30_lora_view radio{};Bridge bridge{};uint64_t active=0,lo=0,scope=0,until=0,published=0;bool changed=false;};
struct BleMeta {uint64_t active=0,id=0,domain=0,scope=0;uint32_t generation=0;uint64_t model_tick=0;int64_t model_epoch=0,model_phase=0;double model_rate=0;bool calibrated=false;};
struct Lineage {
 uint64_t root=0,root_boot=0,parent=0,parent_boot=0,emitter=0,emitter_boot=0,publication=0;
 uint32_t source_epoch=0;uint8_t depth=0;bool depth_known=false;
};
struct Innovation {uint64_t tick=0,id=0,scope=0;int64_t ns=0;};
struct Snapshot {
 RadioRow ble,lora;Lineage ble_path,lora_path;ClockView clock;
 Innovation ble_event,lora_event;
 uint64_t sequence=0,sampled_us=0,ble_model_tick=0,lora_model_tick16=0,lora_metadata_us=0,lora_created_ns=0,lora_expires_ns=0;uint32_t ble_refusal=0,lora_pairs=0;
 bool ble_new=false,lora_new=false,ble_innovation_valid=false,lora_innovation_valid=false;
 int64_t ble_innovation_ns=0,lora_innovation_ns=0;
};
struct Settler {
 uint64_t scope=0,epoch=0,start=0;uint32_t number=0;
 void apply(RadioRow& r,const Row& row){
  // An unmapped edge has no authenticated scope. Reject that pulse, but do
  // not mistake missing evidence for a new source. A returning nonzero scope
  // must still match; reference/capture/sequence discontinuities always reset.
  if((r.scope&&scope!=r.scope)||epoch!=row.epoch||!row.reference||!row.capture||
     (number&&row.number!=number+1)||row.seen<start){if(r.scope)scope=r.scope;epoch=row.epoch;start=0;}
  number=row.number;
  if(r.scope&&r.eligible&&row.reference&&row.capture){if(!start)start=row.seen;
   r.settled=uint32_t(std::min<uint64_t>(UINT32_MAX,(row.seen-start)/1000));}
  r.eligible=r.eligible&&r.scope&&row.reference&&row.capture&&r.settled>=10000;
 }
};
struct State {
 v12::G1PpsDiagnostic ble;v12::R2Status ble_status;
 Frozen frozen[8]{};BleMeta ble_meta[8]{};
 Clock clock,clocks[8];ClockView delivered;
 Row rows[32]{};Settler settlers[2];
 Snapshot snapshot;
 uint64_t clock_ticks[8]{},boot=0,last_seen=0;
 unsigned head=0,bhead=0,chead=0,rhead=0,count=0;
 uint32_t serial=0,overwritten=0,ble_refusal=0;bool ble_ready=false,lora_ready=false;bool (*read)(uint64_t*)=nullptr;
};
static_assert(sizeof(State)<32768,"Bounded PSRAM dual-radio observer workspace");
State* state=nullptr;
uint64_t mix(uint64_t h,uint64_t x){for(unsigned i=0;i<8;++i){h^=uint8_t(x);h*=UINT64_C(1099511628211);x>>=8;}return h;}
template<class... V> uint64_t fingerprint(V... values){uint64_t h=UINT64_C(14695981039346656037);((h=mix(h,uint64_t(values))),...);return h?h:1;}
uint64_t deadline(uint64_t tick,uint64_t now,uint64_t until){return until>now&&until-now<(UINT64_MAX-tick)/20?tick+(until-now)*20:tick;}
bool ble_calibrated(const v12::R2Status& s){return profile.enabled&&s.root_identity==profile.ble_root&&
 (s.rc7_bound?s.joined_peer:0)==profile.ble_parent&&uint8_t(s.rc7_bound?s.rc7_depth:0)==profile.ble_depth;}
bool lora_calibrated(const n30_lora_view& s){return profile.enabled&&s.emitter==profile.lora_emitter&&s.root==profile.lora_root;}
int32_t ble_extra(bool match){return profile.enabled&&(match||profile.study_fixed_priors)?profile.ble_incremental_ns:0;}
int32_t lora_extra(bool match){return profile.enabled&&(match||profile.study_fixed_priors)?profile.lora_correction_ns:0;}
bool corrected(uint64_t ns,int32_t correction,uint64_t& out){
 if(!ns||ns>uint64_t(INT64_MAX)||int64_t(ns)<=-int64_t(correction)||
    (correction>0&&ns>uint64_t(INT64_MAX)-uint64_t(correction)))return false;
 out=uint64_t(int64_t(ns)+correction);return true;
}
void radio_json(v12::ConsoleFrame& f,const char* name,const RadioRow& r,bool pps=true){
 f.format("\"%s\":{\"eligible\":%s,\"authenticated\":%s,\"strict\":%s,\"map_valid\":%s,\"requested\":%s,\"owner_active\":%s,\"scope\":\"%016" PRIx64 "\",\"counter_domain\":\"common20_%016" PRIx64 "\",\"settled_ms\":%" PRIu32 ",\"capture_epoch\":%" PRIu64 ",\"sample_epoch\":%" PRIu64 ",\"owner_generation\":%" PRIu32 ",\"sample_generation\":%" PRIu32 ",\"available_tick\":%" PRIu64 ",\"until_tick\":%" PRIu64 ",\"permission_tick\":%" PRIu64 ",\"edge_tick\":%" PRIu64 ",\"mapping_enclosure_ns\":%" PRIu32 ",\"current_correction_ns\":%" PRId32 ",\"observation_id\":%" PRIu64 ",\"reason\":%u",
 name,r.eligible?"true":"false",r.auth?"true":"false",r.strict?"true":"false",r.map?"true":"false",r.requested?"true":"false",r.owner?"true":"false",r.scope,r.domain,r.settled,r.epoch,r.epoch,r.generation,r.generation,r.available,r.until,r.until,r.edge,r.enclosure,r.correction,r.id,unsigned(r.reason));
 f.format(",\"worker_bracket_ns\":%" PRIu32 ",\"main_bracket_ns\":%" PRIu32 ",\"bridge_epoch\":%" PRIu64,r.worker_bracket_ns,r.main_bracket_ns,r.bridge_epoch);
 f.format(",\"utc_ns\":%" PRIu64,r.utc_ns);
 f.format(",\"projected_rate_milli_ppb\":%" PRId32 ",\"rate_valid\":%s",r.rate_milli_ppb,r.rate_valid?"true":"false");
 if(pps)f.format(",\"phase_ns\":%" PRId32,r.phase);
 f.format(",\"calibration_match\":%s}",r.calibration_match?"true":"false");

}
}
void init(uint64_t boot,bool (*reader)(uint64_t*)){
 if(state||!boot||!reader)return;
 const auto memory=heap_caps_malloc(sizeof(State),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);if(!memory)return;
 state=new(memory) State;state->boot=boot;state->read=reader;
 state->clock=Clock(boot,boot,profile.enabled);
}
[[gnu::noinline]] void poll(uint64_t now){
 if(!state)return;
 auto& s=*state;
 uint64_t before=0,after=0;if(!s.read(&before))return;
 auto& ble=s.ble_status;new(&ble) v12::R2Status(v12::r2_status());
 if(!ble.client.running||ble.client.stopping||ble.client.deadline_exceeded)ble.clock_valid=false;
 if(!s.read(&after)||after<before)return;
 now=uint64_t(esp_timer_get_time());s.ble.observe(ble,before,after,now);
 const auto bs=fingerprint(ble.group_id,ble.key_epoch,ble.g1_clock_origin,ble.root_boot,ble.source_epoch,ble.publication,
  ble.g1_relative_quality.scope_generation,ble.client.generation,ble.rx_capture.epoch,ble.rc7_bound?ble.joined_peer:0,ble.rc7_bound?ble.joined_boot:0,ble.rc7_bound?ble.rc7_depth:0,
  profile.ble_existing_ns,profile.ble_incremental_ns);
 const auto& bm=s.ble_meta[(s.bhead+7)%8];
 if(bm.scope!=bs||bm.model_tick!=ble.g1_clock_model.tick||bm.model_epoch!=ble.g1_clock_model.epoch||bm.model_phase!=ble.g1_clock_model.phase||bm.model_rate!=ble.g1_clock_model.rate){s.ble_meta[s.bhead]={after,ble.g1_corrected_original20,ble.rx_capture.epoch,bs,ble.client.generation,ble.g1_clock_model.tick,ble.g1_clock_model.epoch,ble.g1_clock_model.phase,ble.g1_clock_model.rate,ble_calibrated(ble)};s.bhead=(s.bhead+1)%8;}
 auto& f=s.frozen[s.head];const auto& prior=s.frozen[(s.head+7)%8];
 f={};WatchClockBridge counter_bridge;
 const bool mapped=watch_clock_bridge(n30_lora_reference_read,counter_bridge);
 const auto a=counter_bridge.fine20;
 watch_lora_dual_snapshot(f.radio,f.bridge.radio);
 uint64_t completed=0;if(!s.read(&completed))return;
 f.active=completed;f.bridge.before20=f.bridge.after20=mapped?a:0;f.bridge.available20=completed;
 f.bridge.xtal_lower20=counter_bridge.xtal_before20;f.bridge.xtal_upper20=counter_bridge.xtal_after20;
 if(!mapped)f.bridge.xtal20=0; // Keep refusal widths/epochs visible, never fabricate a map.
 const auto& l=f.radio;
 f.scope=fingerprint(l.emitter,l.emitter_boot,l.root,l.root_boot,l.stream,l.source_epoch,l.capture_epoch,l.request_generation,lora_extra(lora_calibrated(l)));
 for(auto v:l.context)f.scope=mix(f.scope,v);
 f.changed=!prior.active||prior.radio.model_tick16!=l.model_tick16||prior.radio.model_ns!=l.model_ns||prior.scope!=f.scope||prior.radio.state!=l.state||prior.radio.until_us!=l.until_us;
 f.published=f.changed?completed:prior.published;
 f.lo=prior.active?prior.active:before;
 const auto observed=uint64_t(esp_timer_get_time());f.until=deadline(a,observed,l.until_us);
 s.head=(s.head+1)%8;
 uint64_t tick=0;if(!s.read(&tick))return;
 Observation bo,lo;uint64_t ns=0;
 const auto bu=std::min({ble.g1_anchor_deadline_us,ble.g1_corrected_until_us,ble.service_until_us});
 s.ble_refusal=ptob_ble::ble_lock_refusal(ble,now);
 bo.eligible=ble.client.running&&!ble.client.stopping&&ble.client.generation&&!s.ble_refusal&&v12::r2_clock_witness(ble)&&ble.following&&ble.requested&&!ble.fault&&ble.rx_capture.epoch&&bu>now;
 bo.calibrated=ble_calibrated(ble);bo.tick=after;bo.until=deadline(before,now,bu);bo.scope=bs;
 bo.id=ble.g1_corrected_original20;bo.domain=ble.rx_capture.epoch;bo.enclosure_ns=50;bo.rate_ppb=ble.g1_clock_model.rate;
 const auto projected=ble.g1_clock_model.at(after);if(projected>0)corrected(uint64_t(projected),ble_extra(bo.calibrated),bo.utc_ns);
 uint64_t native=0;uint32_t enclosure=UINT32_MAX;
 double lora_rate=0;
 lo.eligible=strict_lora(l,observed)&&f.bridge.radio.epoch==l.capture_epoch&&f.bridge.convert(tick,native,enclosure)&&lora_project(l,native,ns,&lora_rate);
 lo.calibrated=lora_calibrated(l);lo.tick=tick;lo.until=f.until;lo.scope=f.scope;lo.id=l.edge_tick;lo.domain=l.capture_epoch;
 lo.enclosure_ns=enclosure;lo.rate_ppb=lora_rate;
 if(lo.eligible)corrected(ns,lora_extra(lo.calibrated),lo.utc_ns);
 s.ble_ready=bo.live(tick);s.lora_ready=lo.live(tick);
 // All three values below refer to this causal, post-publication tick. This
 // is a model snapshot, never a PPS edge or an additional physical RF sample.
 auto& snap=s.snapshot;const auto old_ble=snap.ble,old_lora=snap.lora;
 const auto prior_clock=s.clock.view(tick);
 snap.ble={};snap.lora={};++snap.sequence;snap.sampled_us=observed;snap.ble_refusal=s.ble_refusal;
 auto& br=snap.ble;br.scope=bo.scope;br.domain=br.epoch=bo.domain;br.generation=ble.client.generation;
 br.available=s.ble_meta[(s.bhead+7)%8].active;br.until=bo.until;br.edge=tick;br.id=bo.id;br.enclosure=50;
 br.requested=ble.requested;br.owner=ble.client.running&&!ble.client.stopping;
 br.auth=v12::r2_clock_witness(ble)&&ble.following;br.strict=s.ble_ready;br.map=ble.g1_clock_model.tick&&after<=tick;
 br.correction=profile.ble_existing_ns+ble_extra(bo.calibrated);br.calibration_match=bo.calibrated;br.reason=!br.requested?10:!br.owner?11:!br.auth?12:bu<=now?13:uint8_t(s.ble_refusal);
 const auto bt=ble.g1_clock_model.at(tick);if(br.map&&bt>0)corrected(uint64_t(bt),ble_extra(bo.calibrated),br.utc_ns);
 br.eligible=s.ble_ready&&br.utc_ns;br.rate_valid=std::isfinite(bo.rate_ppb)&&std::abs(bo.rate_ppb)<100000;
 if(br.rate_valid)br.rate_milli_ppb=int32_t(std::llround(bo.rate_ppb*1000));
 auto& lr=snap.lora;lr.scope=lo.scope;lr.domain=lr.epoch=lo.domain;lr.generation=l.request_generation;
 lr.available=f.published;lr.until=lo.until;lr.edge=tick;lr.id=lo.id;lr.enclosure=lo.enclosure_ns;
 lr.requested=l.requested;lr.owner=l.active&&!l.fault;lr.auth=lr.strict=strict_lora(l,observed);
 lr.map=lo.eligible;lr.eligible=s.lora_ready;lr.utc_ns=lo.utc_ns;lr.correction=lora_extra(lo.calibrated);lr.calibration_match=lo.calibrated;
 lr.reason=lr.eligible?0:!lr.strict?3:!lr.map?4:5;lr.bridge_epoch=f.bridge.radio.epoch;
 if(f.bridge.radio.after>=f.bridge.radio.before&&f.bridge.radio.after-f.bridge.radio.before<=UINT32_MAX/50)lr.worker_bracket_ns=uint32_t((f.bridge.radio.after-f.bridge.radio.before)*50);
 if(f.bridge.xtal_upper20>=f.bridge.xtal_lower20&&f.bridge.xtal_upper20-f.bridge.xtal_lower20<=UINT32_MAX/50)lr.main_bracket_ns=uint32_t((f.bridge.xtal_upper20-f.bridge.xtal_lower20)*50);
 lr.rate_valid=lo.eligible&&std::isfinite(lora_rate)&&std::abs(lora_rate)<100000;
 if(lr.rate_valid)lr.rate_milli_ppb=int32_t(std::llround(lora_rate*1000));
 snap.ble_path={ble.root_identity,ble.root_boot,ble.joined_peer,ble.joined_boot,0,0,ble.publication,ble.source_epoch,uint8_t(ble.rc7_bound?ble.rc7_depth:0),br.auth};
 snap.lora_path={l.root,l.root_boot,l.parent,l.parent_boot,l.emitter,l.emitter_boot,l.stream,l.source_epoch,l.hop,l.parent_quality};
 snap.ble_model_tick=ble.g1_clock_model.tick;snap.lora_model_tick16=l.model_tick16;
 snap.lora_metadata_us=l.metadata_us;snap.lora_created_ns=l.created_ns;snap.lora_expires_ns=l.expires_ns;snap.lora_pairs=l.model_samples;
 snap.ble_new=br.id&&(br.id!=old_ble.id||br.scope!=old_ble.scope||br.domain!=old_ble.domain);
 snap.lora_new=lr.id&&(lr.id!=old_lora.id||lr.scope!=old_lora.scope||lr.domain!=old_lora.domain);
 snap.ble_innovation_valid=br.eligible&&prior_clock.valid;snap.lora_innovation_valid=lr.eligible&&prior_clock.valid;
 if(snap.ble_innovation_valid)snap.ble_innovation_ns=int64_t(br.utc_ns)-int64_t(prior_clock.utc_ns);
 if(snap.lora_innovation_valid)snap.lora_innovation_ns=int64_t(lr.utc_ns)-int64_t(prior_clock.utc_ns);
 if(snap.ble_new&&snap.ble_innovation_valid)snap.ble_event={tick,br.id,br.scope,snap.ble_innovation_ns};
 if(snap.lora_new&&snap.lora_innovation_valid)snap.lora_event={tick,lr.id,lr.scope,snap.lora_innovation_ns};
 s.delivered=s.clock.update(tick,s.boot,bo,lo);s.clocks[s.chead]=s.clock;s.clock_ticks[s.chead]=tick;s.chead=(s.chead+1)%8;
 snap.clock=s.delivered;
}
void reference_lost(){if(state)for(auto& v:state->settlers)v={};}
void edge(const WatchPpsCapture& e,const v12::G1GpsShadow::Reference& ref,bool fix){
 if(!state||state->serial==UINT32_MAX)return;
 auto& s=*state;Row row;
 row.serial=++s.serial;row.number=e.number;row.seen=e.seen_us;row.edge=e.tick20;
 row.epoch=(uint64_t(ref.epoch)<<32)|e.incarnation;row.capture=e.valid;row.reference=ref.ready&&fix&&ref.rmc20&&ref.rmc20<e.tick20&&e.tick20-ref.rmc20<30000000;
 const auto score=s.ble.edge(e.tick20,e.number,e.enclosure_ns,e.valid,ref);
 auto& br=row.ble;br.edge=e.tick20;br.available=score.map.active;br.until=std::min(score.map.until,score.map.permission);br.enclosure=e.enclosure_ns;
 br.auth=score.map_valid&&score.row.following;br.strict=br.auth&&score.row.permission;br.map=score.map_valid;br.requested=br.owner=br.auth;
 br.reason=score.row.reason;
 for(unsigned i=0;i<8;++i){const auto& m=s.ble_meta[(s.bhead+7-i)%8];if(m.active&&m.active<=score.map.active){br.scope=m.scope;br.domain=m.domain;br.epoch=m.domain;br.generation=m.generation;br.id=m.id;br.calibration_match=m.calibrated;break;}}
 uint64_t ble_adjusted=0;const auto incremental=ble_extra(br.calibration_match);
 const bool ble_projected=score.utc_ns>0&&corrected(uint64_t(score.utc_ns),incremental,ble_adjusted);
 if(ble_projected){br.utc_ns=ble_adjusted;br.phase=v12::G1PpsDiagnostic::phase(int64_t(ble_adjusted));}
 if(std::isfinite(score.map.clock.rate)&&std::abs(score.map.clock.rate)<100000){br.rate_milli_ppb=int32_t(std::llround(score.map.clock.rate*1000));br.rate_valid=score.map_valid;}
 br.correction=profile.ble_existing_ns+incremental;
 br.eligible=ble_projected&&br.strict&&br.map&&br.epoch&&br.generation&&br.id&&br.enclosure!=UINT32_MAX&&br.until>br.edge&&br.until-br.edge>(uint64_t(br.enclosure)+49)/50;
 br.domain=fingerprint(s.boot,e.incarnation,br.epoch,0);
 auto& lr=row.lora;lr.edge=e.tick20;lr.reason=1;
 for(unsigned i=0;i<8;++i){const auto& f=s.frozen[(s.head+7-i)%8];if(!f.active)continue;
  if(f.changed&&e.tick20>=f.lo&&e.tick20<=f.active){lr.reason=2;break;}
  if(f.active>=e.tick20)continue;
  const auto& l=f.radio;lr.calibration_match=lora_calibrated(l);lr.correction=lora_extra(lr.calibration_match);lr.scope=f.scope;lr.domain=lr.epoch=l.capture_epoch;lr.generation=l.request_generation;lr.id=l.edge_tick;
  lr.available=f.active;lr.until=f.until;lr.requested=l.requested;lr.owner=l.active&&!l.fault;
  lr.auth=lr.strict=strict_lora(l,e.seen_us);
  lr.bridge_epoch=f.bridge.radio.epoch;
  if(f.bridge.radio.after>=f.bridge.radio.before&&f.bridge.radio.after-f.bridge.radio.before<=UINT32_MAX/50)lr.worker_bracket_ns=uint32_t((f.bridge.radio.after-f.bridge.radio.before)*50);
  if(f.bridge.xtal_upper20>=f.bridge.xtal_lower20&&f.bridge.xtal_upper20-f.bridge.xtal_lower20<=UINT32_MAX/50)lr.main_bracket_ns=uint32_t((f.bridge.xtal_upper20-f.bridge.xtal_lower20)*50);
  uint64_t native=0,ns=0,adjusted=0;uint32_t enclosure=UINT32_MAX;
  lr.map=e.valid&&f.bridge.radio.epoch==l.capture_epoch&&f.bridge.convert(e.tick20,native,enclosure);lr.enclosure=enclosure;
  double projected_rate=0;
  const bool projected=lr.map&&lora_project(l,native,ns,&projected_rate)&&corrected(ns,lr.correction,adjusted);
  if(projected){lr.utc_ns=adjusted;lr.phase=v12::G1PpsDiagnostic::phase(int64_t(adjusted));}
  if(projected&&std::isfinite(projected_rate)&&std::abs(projected_rate)<100000){lr.rate_milli_ppb=int32_t(std::llround(projected_rate*1000));lr.rate_valid=true;}
  lr.eligible=lr.strict&&projected&&e.tick20<f.until&&e.enclosure_ns!=UINT32_MAX;
  if(lr.enclosure<=UINT32_MAX-e.enclosure_ns)lr.enclosure+=e.enclosure_ns;else lr.eligible=false;
  lr.eligible=lr.eligible&&lr.until>lr.edge&&lr.until-lr.edge>(uint64_t(lr.enclosure)+49)/50;
  lr.domain=fingerprint(s.boot,e.incarnation,lr.epoch,1);
  lr.reason=lr.eligible?0:!lr.strict?3:!lr.map?4:5;break;
 }
 for(unsigned i=0;i<8;++i){const auto n=(s.chead+7-i)%8;if(s.clock_ticks[n]&&s.clock_ticks[n]<e.tick20){row.clock=s.clocks[n].view(e.tick20);break;}}
 s.settlers[0].apply(br,row);s.settlers[1].apply(lr,row);
 if(s.count==32)++s.overwritten;else ++s.count;s.rows[s.rhead]=row;s.rhead=(s.rhead+1)%32;
}
watch::dual::ClockView clock(){watch::dual::ClockView unavailable;unavailable.enabled=profile.enabled;if(!state)return unavailable;if(!profile.enabled)return state->delivered;uint64_t tick=0;if(!state->read(&tick))return unavailable;auto v=state->clock.view(tick);v.sampled_us=uint64_t(esp_timer_get_time());return v;}
namespace {
void lineage_json(v12::ConsoleFrame& f,const char* name,const Lineage& p){
 f.format("\"%s_lineage\":{\"root\":\"%016" PRIx64 "\",\"root_boot\":\"%016" PRIx64 "\",\"parent\":\"%016" PRIx64 "\",\"parent_boot\":\"%016" PRIx64 "\",\"emitter\":\"%016" PRIx64 "\",\"emitter_boot\":\"%016" PRIx64 "\",\"publication\":%" PRIu64 ",\"source_epoch\":%" PRIu32 ",\"depth\":%u,\"depth_known\":%s}",name,p.root,p.root_boot,p.parent,p.parent_boot,p.emitter,p.emitter_boot,p.publication,p.source_epoch,unsigned(p.depth),p.depth_known?"true":"false");
}
void innovation_json(v12::ConsoleFrame& f,const char* name,const Innovation& p){
 f.format(",\"%s_causal_event\":{\"tick\":%" PRIu64 ",\"observation_id\":%" PRIu64 ",\"scope\":\"%016" PRIx64 "\",\"innovation_ns\":%" PRId64 "}",name,p.tick,p.id,p.scope,p.ns);
}
}
[[gnu::noinline]] void snapshot_frame(v12::ConsoleFrame& f){
 f.format("TWATCH_CLOCK_SNAPSHOT version=1 contract=TWATCH_GNSS_FREE_SNAPSHOT_V1 available=%u",unsigned(state&&state->snapshot.sequence));
 if(!state||!state->snapshot.sequence){f.format("\n");return;}
 const auto& s=state->snapshot;const auto& c=s.clock;
 f.format(" json={\"schema\":1,\"contract\":\"TWATCH_GNSS_FREE_SNAPSHOT_V1\",\"gnss_witness\":false,\"sequence\":%" PRIu64 ",\"sampled_us\":%" PRIu64 ",\"tick\":%" PRIu64 ",\"study_fixed_priors\":%s,\"ble_existing_ns\":%" PRId32 ",\"ble_incremental_ns\":%" PRId32 ",\"lora_extra_ns\":%" PRId32 ",\"ble_refusal\":%" PRIu32 ",",s.sequence,s.sampled_us,c.tick,profile.study_fixed_priors?"true":"false",profile.ble_existing_ns,profile.ble_incremental_ns,profile.lora_correction_ns,s.ble_refusal);
 f.format("\"ble_model_tick\":%" PRIu64 ",\"lora_model_tick16\":%" PRIu64 ",\"lora_metadata_us\":%" PRIu64 ",\"lora_created_ns\":%" PRIu64 ",\"lora_expires_ns\":%" PRIu64 ",\"lora_physical_pairs\":%" PRIu32 ",",s.ble_model_tick,s.lora_model_tick16,s.lora_metadata_us,s.lora_created_ns,s.lora_expires_ns,s.lora_pairs);
 radio_json(f,"ble",s.ble,false);f.format(",");radio_json(f,"lora",s.lora,false);f.format(",");
 lineage_json(f,"ble",s.ble_path);f.format(",");lineage_json(f,"lora",s.lora_path);
 innovation_json(f,"ble",s.ble_event);innovation_json(f,"lora",s.lora_event);
 f.format(",\"ble_new_physical\":%s,\"lora_new_physical\":%s,\"ble_innovation_valid\":%s,\"lora_innovation_valid\":%s,\"ble_innovation_ns\":%" PRId64 ",\"lora_innovation_ns\":%" PRId64,s.ble_new?"true":"false",s.lora_new?"true":"false",s.ble_innovation_valid?"true":"false",s.lora_innovation_valid?"true":"false",s.ble_innovation_ns,s.lora_innovation_ns);
 f.format(",\"clock\":{\"valid\":%s,\"enabled\":%s,\"instance\":\"%016" PRIx64 "\",\"counter_domain\":\"common20_%016" PRIx64 "\",\"counter_tick\":%" PRIu64 ",\"utc_ns\":%" PRIu64 ",\"mode\":\"%s\",\"rate_ppb\":%.6f,\"authoritative\":%s,\"fine\":%s,\"calibrated\":%s,\"holdover_age_us\":%" PRIu64 ",\"updates\":%" PRIu64 ",\"rejected\":%" PRIu64 "}}\n",c.valid?"true":"false",c.enabled?"true":"false",c.instance,c.domain,c.tick,c.utc_ns,mode_name(c.mode),c.rate_ppb,c.authoritative?"true":"false",c.fine?"true":"false",c.calibrated?"true":"false",c.holdover_age_us,c.updates,c.rejected);
}
void frame(v12::ConsoleFrame& f,uint32_t after){
 const Row* row=nullptr;if(state)for(unsigned i=0;i<state->count;++i){const auto& r=state->rows[(state->rhead+32-state->count+i)%32];if(r.serial>after){row=&r;break;}}
 f.format("TWATCH_DUAL_PPS version=1 contract=TWATCH_DUAL_RADIO_PPS_V1 clock_contract=TWATCH_COMMON_CLOCK_V1 enabled=%u available=%u latest=%u overwritten=%u psram_bytes=%u",unsigned(profile.enabled),unsigned(row!=nullptr),unsigned(state?state->serial:0),unsigned(state?state->overwritten:0),unsigned(sizeof(State)));
 const auto live=clock();
 f.format(" live_mode=%s live_valid=%u live_calibrated=%u live_utc_ns=%" PRIu64 " live_tick=%" PRIu64 " live_updates=%" PRIu64 " live_ble_ready=%u live_lora_ready=%u live_ble_refusal=%u",
 mode_name(live.mode),unsigned(live.valid),unsigned(live.calibrated),live.utc_ns,live.tick,live.updates,unsigned(state&&state->ble_ready),unsigned(state&&state->lora_ready),unsigned(state?state->ble_refusal:0));
 if(!row){f.format("\n");return;}const auto& r=*row;const auto& c=r.clock;
 f.format(" cursor=%u json={\"schema\":1,\"contract\":\"TWATCH_DUAL_RADIO_PPS_V1\",\"pps\":{\"number\":%u,\"seen_us\":%" PRIu64 ",\"capture_epoch\":%" PRIu64 ",\"capture_valid\":%s,\"reference_valid\":%s,\"aided\":false},",unsigned(r.serial),unsigned(r.number),r.seen,r.epoch,r.capture?"true":"false",r.reference?"true":"false");
 radio_json(f,"ble",r.ble);f.format(",");radio_json(f,"lora",r.lora);
 f.format(",\"clock\":{\"valid\":%s,\"enabled\":%s,\"instance\":\"%016" PRIx64 "\",\"counter_domain\":\"common20_%016" PRIx64 "\",\"counter_tick\":%" PRIu64 ",\"utc_ns\":%" PRIu64 ",\"mode\":\"%s\",\"rate_ppb\":%.6f,\"authoritative\":%s,\"fine\":%s,\"calibrated\":%s,\"holdover_age_us\":%" PRIu64,
 c.valid?"true":"false",c.enabled?"true":"false",c.instance,c.domain,c.tick,c.utc_ns,mode_name(c.mode),c.rate_ppb,c.authoritative?"true":"false",c.fine?"true":"false",c.calibrated?"true":"false",c.holdover_age_us);
 if(c.valid&&r.reference&&r.capture)f.format(",\"phase_ns\":%" PRId32,v12::G1PpsDiagnostic::phase(int64_t(c.utc_ns)));
 f.format("}}\n");
}
}

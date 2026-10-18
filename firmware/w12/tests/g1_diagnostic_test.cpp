#include "g1_diagnostic_render.hpp"
#include "r2_discipline.hpp"
#include "g1_record_budget.hpp"
#include <cassert>
#include <cstring>
#include <limits>
int main(){
  v12::R2Status s;s.client.running=true;s.g1_diagnostics.first=s.g1_diagnostics.last=v12::G1Reason::PeriodicScope;
  s.g1_diagnostics.first_us=s.g1_diagnostics.last_us=UINT64_MAX;
  for(auto& n:s.g1_diagnostics.reasons)n=UINT32_MAX;
  char line[v12::G1DiagnosticLineBytes];
  v12::G1ParentEvent parent{};parent.sequence=UINT32_MAX;parent.at=UINT64_MAX;parent.reason=v12::G1ParentReason::ClockUnavailable;
  parent.from={UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT64_MAX,UINT32_MAX,UINT32_MAX,2,true};parent.to=parent.from;
  parent.current_score=parent.best_score=INT16_MIN;parent.decision=UINT8_MAX;parent.poor=true;
  const int parent_bytes=v12::g1_render_parent(line,sizeof line,parent,UINT32_MAX);
  assert(parent_bytes>0&&unsigned(parent_bytes)<sizeof line&&std::strstr(line,"CLOCK_UNAVAILABLE")&&std::strstr(line,"old_parent=ffffffffffffffff")&&std::strstr(line,"history=RAM8"));
  for(unsigned row=0;row<3;row++){const int n=v12::g1_render_summary(line,sizeof line,s,UINT64_MAX,row);assert(n>0&&unsigned(n)<sizeof line);assert(std::strchr(line,'\n'));}
  v12::LogStatus log;log.peak=log.dropped=log.expired=UINT32_MAX;
  s.service_until_us=UINT64_MAX;s.g1_refresh_needed=true;
  s.root_identity=s.joined_peer=s.g1_diagnostics.last_candidate=UINT64_MAX;
  s.join_attempts=s.join_accepts=s.g1_diagnostics.descriptor_seen=s.g1_diagnostics.connect_ok=s.g1_diagnostics.connect_drain=UINT32_MAX;
  s.g1_pending_disconnects=s.g1_quiet_faults=s.g1_relay_authenticated=s.g1_relay_refused=s.discipline_accepted=s.discipline_refused=UINT32_MAX;
  s.g1_corrected_accepted=s.g1_corrected_refused=s.g1_frequency_pairs=s.g1_frequency_scatter_ns=UINT32_MAX;s.g1_frequency_trained=true;s.g1_clock_model.rate=-50000;
  s.g1_clock_predictions=UINT32_MAX;s.g1_clock_causal_squares=s.g1_clock_nominal_squares=s.g1_clock_causal_max_ns=UINT64_MAX;
  s.g1_clock_discontinuities=UINT32_MAX;s.g1_clock_last_discontinuity_ns=s.g1_clock_fit_tick20=UINT64_MAX;
  s.g1_corrected_until_us=s.g1_corrected_original20=s.g1_clock_model.tick=UINT64_MAX;
  s.g1_clock_model.epoch=s.g1_clock_model.phase=s.g1_clock_model.utc_offset=INT64_MAX;
  s.g1_relative_quality.original20=s.g1_relative_quality.evaluated20=UINT64_MAX;
  s.g1_relative_quality.samples=s.g1_relative_quality.rms_ns=s.g1_relative_quality.max_ns=UINT32_MAX;
  s.g1_relative_quality.rate_guard_ppb=UINT32_MAX;
  assert(v12::g1_render_clock(line,sizeof line,s,UINT64_MAX)>0&&std::strlen(line)<sizeof line-1);
  s.g1_fit_points=s.g1_fit_span_ms=UINT16_MAX;s.g1_fit_reason=UINT8_MAX;
  assert(v12::g1_render_fit(line,sizeof line,s,UINT64_MAX)>0&&std::strlen(line)<sizeof line-1&&std::strstr(line,"pairs="));
  char ui_line[1536]; // Existing logger record ceiling; native UI adds no buffer.
  const int ui=v12::g1_emit_ui_budget([&](const char* format,auto... args){return std::snprintf(ui_line,sizeof ui_line,format,args...);},
    s,UINT64_MAX,"LOCK_EST",UINT32_MAX,UINT32_MAX,true,7);
  assert(ui>0&&unsigned(ui)+128<sizeof ui_line&&std::strstr(ui_line,"evaluated20=")&&std::strstr(ui_line,"raw_hold_ms=")&&std::strstr(ui_line,"ui_hold_ms=")&&std::strstr(ui_line,"precision=UNQUALIFIED"));
  s.g1_recovery.rearms=s.g1_recovery.resets=s.g1_recovery.syncs=s.g1_recovery.refused=s.g1_recovery.old_generation=UINT32_MAX;
  s.g1_recovery.reset_at=s.g1_recovery.sync_at=s.g1_recovery.quiet_until=s.g1_recovery.old_stop_requested=s.g1_recovery.old_stop_deadline=s.g1_recovery.old_completed=UINT64_MAX;
  assert(v12::g1_render_recovery(line,sizeof line,s)>0&&std::strlen(line)<sizeof line-1);
  assert(v12::g1_render_summary(line,sizeof line,s,UINT64_MAX,2)>0&&std::strlen(line)<sizeof line-1);
  v12::G1HciStatus hci;hci.pending_opcode=UINT16_MAX;hci.pending_at_us=1;hci.dropped=UINT32_MAX;
  const int live=v12::g1_render_live(line,sizeof line,s,log,UINT64_MAX,hci);assert(live>0&&unsigned(live)<sizeof line&&std::strstr(line,"precision=UNQUALIFIED"));
  s.g1_admission_policy.no_source=s.g1_admission_policy.foreign_root=s.g1_admission_policy.unusable=UINT32_MAX;
  s.g1_admission_policy.starts[0]=s.g1_admission_policy.starts[1]=s.g1_admission_policy.retry_at=UINT64_MAX;
  assert(v12::g1_render_admission(line,sizeof line,s,UINT64_MAX)>0&&std::strlen(line)<sizeof line-1);
  s.g1_receive_loss.begin();for(unsigned i=0;i<12;i++)s.g1_receive_loss.observe(UINT64_MAX,UINT32_MAX,UINT32_MAX);s.g1_receive_loss.freeze(UINT64_MAX);
  for(unsigned i=0;i<8;i++){const auto n=v12::g1_render_loss(line,sizeof line,s.g1_receive_loss,i);assert(n>0&&unsigned(n)<sizeof line&&std::strstr(line,"CRC=UNKNOWN"));}
  v12::Report r{};r.kind=8;r.length=32;r.callback_us=UINT64_MAX;r.cycle=UINT32_MAX;r.handle=UINT16_MAX;
  std::memset(r.bytes,255,32);r.bytes[0]=7;r.bytes[5]=2;
  const int n=v12::g1_render_link(line,sizeof line,r);assert(n>0&&unsigned(n)<sizeof line&&std::strstr(line,"TERMINATE_ACK")&&std::strstr(line,"rc=-1"));
  for(unsigned event=15;event<=30;event++){r.bytes[0]=event;const int size=v12::g1_render_link(line,sizeof line,r);assert(size>0&&unsigned(size)<sizeof line);}
  r.bytes[0]=1;std::memset(r.bytes+1,0,4);r.bytes[7]=32;
  assert(v12::g1_render_link(line,sizeof line,r)>0&&std::strstr(line,"CONNECT_DRAIN_ACK"));
  r.bytes[7]=2;assert(v12::g1_render_link(line,sizeof line,r)>0&&std::strstr(line,"event=CONNECT rc=0"));
  r.length=5;assert(!v12::g1_render_link(line,sizeof line,r));
  r.kind=22;r.length=64;r.callback_us=UINT64_MAX;r.cycle=UINT32_MAX;r.handle=UINT16_MAX;
  std::memset(r.bytes,255,64);r.bytes[0]=1;r.bytes[1]=1;
  const int owner=v12::g1_render_relay_owner(line,sizeof line,r);
  assert(owner>0&&unsigned(owner)<sizeof line&&std::strstr(line,"event=CONNECT")&&std::strstr(line,"descriptor_rc=-1")&&std::strstr(line,"upstream=ffffffffffffffff")&&std::strstr(line,"service=0xff"));
  for(unsigned event=2;event<=4;event++){r.bytes[1]=event;const int bytes=v12::g1_render_relay_owner(line,sizeof line,r);assert(bytes>0&&unsigned(bytes)<sizeof line);}
  r.bytes[0]=2;assert(!v12::g1_render_relay_owner(line,sizeof line,r));
  r.bytes[0]=1;r.length=63;assert(!v12::g1_render_relay_owner(line,sizeof line,r));
  v12::Report tx{};tx.kind=23;tx.length=184;tx.callback_us=UINT64_MAX;
  std::memset(tx.bytes,255,sizeof tx.bytes);tx.bytes[0]=1;tx.bytes[1]=1;tx.bytes[3]=5;
  for(unsigned offset:{132u,140u}){for(unsigned i=0;i<8;++i)tx.bytes[offset+i]=i==7?128:0;}
  for(unsigned i=0;i<4;++i)tx.bytes[180+i]=i==3?128:0;
  const int txbytes=v12::g1_render_tx_estimate(line,sizeof line,tx);
  assert(txbytes>0&&unsigned(txbytes)<sizeof line&&std::strstr(line,"reference=PROGRAMMED_HALF_SLOT actual=0 bound=UNKNOWN")&&std::strstr(line,"pub=4294967295")&&std::strstr(line,"event=65535"));
  tx.bytes[1]=0;assert(!v12::g1_render_tx_estimate(line,sizeof line,tx));
  tx.bytes[1]=1;tx.bytes[3]=4;assert(!v12::g1_render_tx_estimate(line,sizeof line,tx));

  v12::Report path{};path.kind=24;path.length=96;path.callback_us=UINT64_MAX;
  std::memset(path.bytes,255,sizeof path.bytes);path.bytes[0]=1;
  for(unsigned j=0;j<4;j++)path.bytes[20+j]=uint8_t(uint32_t(47000)>>(8*j));
  const int pathbytes=v12::g1_render_tx_path(line,sizeof line,path);
  assert(pathbytes>0&&unsigned(pathbytes)<sizeof line&&std::strstr(line,"path_offset_ns=47000")&&std::strstr(line,"raw_tx_ns=")&&std::strstr(line,"path_profile=ffffffffffffffff")&&std::strstr(line,"scope=AGGREGATE_PROVISIONAL jitter=SEPARATE actual=0 bound=UNKNOWN absolute_UTC=UNKNOWN"));
  path.length=95;assert(!v12::g1_render_tx_path(line,sizeof line,path));path.length=96;path.bytes[0]=2;assert(!v12::g1_render_tx_path(line,sizeof line,path));
  path.length=124;
  const int path2bytes=v12::g1_render_tx_path(line,sizeof line,path);
  assert(path2bytes>0&&unsigned(path2bytes)<sizeof line&&std::strstr(line,"address20=")&&std::strstr(line,"address_ns=")&&
    std::strstr(line,"base_ns=")&&std::strstr(line,"residual_ns=")&&std::strstr(line,"applied_ns=")&&std::strstr(line,"reference=NOMINAL_ADDRESS_1M delay=UNKNOWN"));

  v12::R2Discipline discipline;v12::periodic_rx::Stamp capture;r2_precision_packet p{};
  assert(!discipline.authenticated_receive(p,capture,0,0,0));const auto q=discipline.view(0,0);
  assert(q.refused==1&&q.first_refusal==q.last_refusal);
  assert(q.last_refusal&v12::R2Discipline::Event);assert(q.last_refusal&v12::R2Discipline::Channel);assert(q.last_refusal&v12::R2Discipline::Edge);
  assert(q.reason_counts[3]==1&&q.reason_counts[4]==1&&q.reason_counts[5]==1&&!q.fit&&!q.accepted);
  discipline.scope({});assert(discipline.view(0,0).first_refusal==q.first_refusal);
  v12::G1RecordBudget budget;v12::Report ad{};ad.kind=5;ad.length=25;ad.bytes[0]=24;ad.bytes[1]=0xff;ad.bytes[2]='G';ad.bytes[3]='1';ad.bytes[17]=1;
  assert(budget.emit(ad));ad.callback_us=999999;assert(!budget.emit(ad));ad.callback_us=1000000;assert(budget.emit(ad));
  ad.bytes[17]=2;assert(budget.emit(ad));assert(!budget.emit(ad));
  assert(budget.discovery_total==5&&budget.discovery_sampled==3&&budget.discovery_aggregated==2);
  ad.kind=6;assert(budget.emit(ad));ad.kind=8;assert(budget.emit(ad));ad.kind=10;assert(budget.emit(ad));
  assert(budget.discovery_total==5); // No PA/capture/link/scope throttling.
}

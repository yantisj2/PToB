#pragma once
#include <cstdint>
extern "C" {
#include "group_join.h"
}
namespace v12 {
// Outcomes are recorded only after the existing JOIN MAC/exchange checks.
// None of these observations grants clock, replay or source permission.
enum class G1AdmissionReply:uint8_t {None,Direct,NoSource,ForeignRoot,Unusable};
inline G1AdmissionReply g1_admission_reply(const r2_join_record& r){
  if(r.control)return G1AdmissionReply::Unusable;
  if(!r.source_flags&&!r.root&&!r.root_boot)return G1AdmissionReply::NoSource;
  if(r.root&&r.root!=r.sender)return G1AdmissionReply::ForeignRoot;
  if(r.root==r.sender&&r.root_boot==r.sender_boot)return G1AdmissionReply::Direct;
  return G1AdmissionReply::Unusable;
}
struct G1AdmissionPolicy {
  uint64_t starts[2]{},older[4]{},retry_at=0;
  uint32_t failures=0,no_source=0,foreign_root=0,unusable=0;
  G1AdmissionReply last=G1AdmissionReply::None;
  uint64_t reacquire_after=0;
  bool warm=false;
  // Retire the old leader's backoff once. Repeated authenticated changes
  // cannot grant more than one fresh recovery admission every five seconds.
  void leader_lost(uint64_t now){warm=false;failures=0;retry_at=now<reacquire_after?reacquire_after:now;
    reacquire_after=retry_at>UINT64_MAX-5000000?UINT64_MAX:retry_at+5000000;}
  void acquired(){warm=true;} // Actual corrected RX, never membership alone.
  bool allowed(uint64_t now,bool continuous_direct=false)const {
    if(now<retry_at)return false;
    if(!warm)return true;
    if(starts[1]&&(now<starts[1]||now-starts[1]<(continuous_direct?5000000:20000000)))return false;
    const auto oldest=continuous_direct?older[0]:starts[0];
    return !oldest||(now>=oldest&&now-oldest>=60000000);
  }
  void started(uint64_t now){for(unsigned i=0;i<3;i++)older[i]=older[i+1];older[3]=starts[0];starts[0]=starts[1];starts[1]=now;}
  void failed(uint64_t now,uint64_t random,uint64_t replay_until=0,bool continuous_direct=false){
    if(failures!=UINT32_MAX)++failures;
    const uint64_t base=failures<=1?250000:failures==2?500000:failures==3?1000000:10000000;
    // Background direct BLE100 control has its own recovery cadence. Keep
    // precision's authority/expiry gates separate from these waits and retries.
    const uint64_t floor=continuous_direct?5000000:20000000;
    uint64_t jitter=continuous_direct?5000000:20000000;
    // A reserved stream may cap jitter, leaving5s for a4s attempt and slack.
    // The rate gate and minimum delay remain independent; no deadline grows.
    if(warm&&replay_until>now){
      const auto left=replay_until-now;
      const auto ceiling=left>floor+5000000?left-floor-5000000:0;
      if(ceiling<jitter)jitter=ceiling;
    }
    const auto delay=warm?floor+random%(jitter+1):base+random%(base/2+1);
    retry_at=now>UINT64_MAX-delay?UINT64_MAX:now+delay;
  }
  void reply(G1AdmissionReply value){
    last=value;
    auto* n=value==G1AdmissionReply::NoSource?&no_source:value==G1AdmissionReply::ForeignRoot?&foreign_root:
      value==G1AdmissionReply::Unusable?&unusable:nullptr;
    if(n&&*n!=UINT32_MAX)++*n;
    if(value==G1AdmissionReply::Direct)failures=0;
  }
};
}

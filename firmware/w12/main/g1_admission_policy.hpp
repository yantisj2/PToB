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
  uint64_t starts[2]{},retry_at=0;
  uint32_t failures=0,no_source=0,foreign_root=0,unusable=0;
  G1AdmissionReply last=G1AdmissionReply::None;
  bool warm=false;
  void acquired(){warm=true;} // Actual corrected RX, never membership alone.
  bool allowed(uint64_t now)const {
    if(now<retry_at)return false;
    if(!warm)return true;
    if(starts[1]&&(now<starts[1]||now-starts[1]<20000000))return false;
    return !starts[0]||(now>=starts[0]&&now-starts[0]>=60000000);
  }
  void started(uint64_t now){starts[0]=starts[1];starts[1]=now;}
  void failed(uint64_t now,uint64_t random){
    if(failures!=UINT32_MAX)++failures;
    const uint64_t base=failures<=1?250000:failures==2?500000:failures==3?1000000:10000000;
    const auto delay=warm?20000000+random%20000001:base+random%(base/2+1);
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

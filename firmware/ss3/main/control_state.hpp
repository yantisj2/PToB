#pragma once
#include "control_codec.h"
#include "client_lifecycle.hpp"
#include <cstring>
namespace v12 {
enum class LinkRole:uint8_t { None=0,GM=1,TimeRelay=2 };
enum class ControlPhase:uint8_t { Client,Bound,Prepared,Committed,Active,Stopping,Fault };
struct DirectBinding {
  bool authenticated=false,encrypted=false,direct=false;
  uint8_t self=255,coordinator=4;LinkRole role=LinkRole::None;
  uint64_t boot=0,coordinator_boot=0,challenge=0;uint32_t config=0;
  uint8_t receive_key[16]{},send_key[16]{};
};
struct AdmissionBounds {
  bool measured=false,source=false,capture=false,phase=false,clock_map=false;
  uint64_t source_epoch=0;uint32_t map_us=UINT32_MAX,stop_us=UINT32_MAX,rate_ppm=UINT32_MAX;
  bool ready()const{return measured&&source&&capture&&phase&&clock_map&&source_epoch&&
    source_epoch<=UINT32_MAX&&map_us!=UINT32_MAX&&stop_us!=UINT32_MAX&&rate_ppm<=1000000;}
};
struct ControlState {
  DirectBinding binding;AdmissionBounds bounds;ControlPhase phase=ControlPhase::Client;
  uint64_t replay=0,send_sequence=0,prepared=0,decision=0,retired=0,stop_at=0;
  uint64_t original_until=0,last_report_second=0;
  bool prepared_receipt=false,start_receipt=false,stop_receipt=false;
  uint32_t accepted=0,refused=0,replays=0,queue_expired=0;
  struct Queued {uint8_t bytes[110]{};uint64_t deadline=0;};
  Ring<Queued,8> outbound;
  void fence(){if(decision>retired)retired=decision;if(prepared>retired)retired=prepared;prepared=0;stop_at=0;original_until=0;
    prepared_receipt=start_receipt=stop_receipt=false;outbound={};binding={};phase=ControlPhase::Client;}
  bool bind(const DirectBinding& link){
    fence();binding={};
    if(!link.authenticated||!link.encrypted||!link.direct||link.self==255||link.coordinator!=4||
       link.self==link.coordinator||!link.boot||!link.coordinator_boot||!link.challenge||!link.config||
       (link.role!=LinkRole::GM&&link.role!=LinkRole::TimeRelay))return false;
    binding=link;replay=send_sequence=0;phase=ControlPhase::Bound;return true;
  }
  bool receive(const uint8_t* bytes,size_t length,uint64_t coordinator_now,uint64_t local_now){
    v12_control_record m{};
    if(!binding.authenticated||binding.role!=LinkRole::GM||
       v12_control_decode(&m,bytes,length,binding.receive_key)){inc(refused);return false;}
    if(m.sender!=binding.coordinator||m.target!=binding.self||m.root!=binding.self||
       m.boot!=binding.coordinator_boot||m.coordinator_boot!=binding.coordinator_boot||
       m.challenge!=binding.challenge||m.config_epoch!=binding.config||
       m.created_us>coordinator_now||coordinator_now>=m.expires_us||m.sequence==UINT64_MAX){inc(refused);return false;}
    if(m.sequence<=replay){inc(replays);return false;}
    if(m.kind!=2&&m.kind!=4&&m.kind!=6){inc(refused);return false;}
    // Replay is consumed after cryptographic/direct-role/TTL admission, even
    // when the semantic operation is refused. A new command needs a new seq.
    replay=m.sequence;
    if(m.kind==6){
      if(m.decision<retired||m.decision<decision){inc(refused);return false;}
      retired=m.decision;decision=m.decision;prepared=0;prepared_receipt=start_receipt=stop_receipt=false;
      phase=ControlPhase::Stopping;inc(accepted);return true;
    }
    if(!bounds.ready()||m.root_boot!=binding.boot||m.source_epoch!=bounds.source_epoch||
       !m.decision||m.decision<=retired){inc(refused);return false;}
    if(m.kind==2){
      if(phase!=ControlPhase::Bound&&phase!=ControlPhase::Prepared){inc(refused);return false;}
      prepared=m.decision;prepared_receipt=false;phase=ControlPhase::Prepared;inc(accepted);return true;
    }
    if((phase!=ControlPhase::Prepared&&phase!=ControlPhase::Active)||
       (phase==ControlPhase::Prepared&&(!prepared_receipt||prepared!=m.decision))||(phase==ControlPhase::Active&&m.decision<=decision)||
       m.permit_until_us<=coordinator_now||m.permit_until_us<=m.created_us||
       m.permit_until_us-m.created_us>3000000){inc(refused);return false;}
    const uint64_t remaining=m.permit_until_us-coordinator_now;
    const uint64_t margin=uint64_t(bounds.map_us)+bounds.stop_us+(remaining*bounds.rate_ppm+999999)/1000000;
    if(remaining<=margin||local_now>UINT64_MAX-(remaining-margin)){inc(refused);return false;}
    // Account for an adverse slow local timer as well as original remote expiry.
    const uint64_t duration=(remaining-margin)*1000000/(1000000+bounds.rate_ppm);
    if(!duration||local_now>UINT64_MAX-duration){inc(refused);return false;}
    original_until=m.permit_until_us;stop_at=local_now+duration;decision=m.decision;
    prepared=0;prepared_receipt=false;start_receipt=stop_receipt=false;
    phase=ControlPhase::Committed;inc(accepted);return true;
  }
  // These receipts must come from the registered controller adapter. Merely
  // sending HCI commands or changing an application state is insufficient.
  bool preparation_complete(uint64_t for_decision,bool resources_reserved){
    if(phase!=ControlPhase::Prepared||for_decision!=prepared||!resources_reserved||!bounds.ready())return false;
    prepared_receipt=true;return true;
  }
  bool started(uint64_t for_decision,bool actual_enabled,bool payload_bound,uint64_t now){
    if(phase!=ControlPhase::Committed||for_decision!=decision||!actual_enabled||!payload_bound||!bounds.ready()||now>=stop_at)return false;
    start_receipt=true;phase=ControlPhase::Active;return true;
  }
  void poll(uint64_t now){if((phase==ControlPhase::Active||phase==ControlPhase::Committed)&&now>=stop_at){if(decision>retired)retired=decision;
    phase=ControlPhase::Stopping;prepared=0;prepared_receipt=start_receipt=stop_receipt=false;}}
  bool stopped(bool actual_disabled,bool actual_drained){
    if(phase!=ControlPhase::Stopping||!actual_disabled||!actual_drained)return false;
    stop_at=original_until=0;stop_receipt=true;phase=ControlPhase::Bound;return true;
  }
  bool queue(v12_control_record m,uint64_t coordinator_now){
    if(!binding.authenticated||binding.role!=LinkRole::GM||send_sequence==UINT64_MAX||
       (m.kind!=1&&m.kind!=3&&m.kind!=5&&m.kind!=7)||
       coordinator_now>UINT64_MAX-250000)return false;
    if((m.kind==3&&(phase!=ControlPhase::Prepared||!prepared_receipt||m.decision!=prepared))||
       (m.kind==5&&(phase!=ControlPhase::Active||!start_receipt||m.decision!=decision))||
       (m.kind==7&&(!stop_receipt||m.decision!=decision)))return false;
    if(m.kind==1&&!bounds.ready())m.flags=0;
    m.sender=binding.self;m.target=binding.coordinator;m.boot=binding.boot;
    m.coordinator_boot=binding.coordinator_boot;m.challenge=binding.challenge;m.config_epoch=binding.config;
    m.sequence=send_sequence+1;m.created_us=coordinator_now;m.expires_us=coordinator_now+250000;
    if(m.kind!=1)m.root=binding.self;
    Queued q;q.deadline=m.expires_us;
    if(v12_control_encode(q.bytes,&m,binding.send_key)||!outbound.push(q))return false;
    ++send_sequence;return true;
  }
  bool pop(Queued& q,uint64_t coordinator_now){
    while(outbound.pop(q)){if(coordinator_now<q.deadline)return true;inc(queue_expired);}return false;
  }
  static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
};
}

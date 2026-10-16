#pragma once
#include "r2_client_service.hpp"
namespace v12 {
// An SDK reset is a new controller incarnation, not a successful old drain.
// Only one admission follows each proven reset/sync/closure/cache-check cycle.
struct G1ResetRecovery {
  enum Stage:uint8_t {Idle,AwaitReset,AwaitSync,AwaitDrain,Quiet,Rearmed,Refused};
  uint64_t reset_at=0,sync_at=0,quiet_until=0;
  uint64_t old_stop_requested=0,old_stop_deadline=0,old_completed=0;
  uint32_t resets=0,syncs=0,rearms=0,refused=0,old_generation=0;
  int32_t reason=0;Stage stage=Idle;bool eligible=false;
  static bool increment(uint32_t& n){if(n==UINT32_MAX)return false;++n;return true;}
  void timeout(uint32_t generation,bool desired){
    if(!desired||!generation||stage==AwaitReset||stage==AwaitSync||stage==AwaitDrain||stage==Quiet)return;
    old_generation=generation;eligible=true;stage=AwaitReset;
  }
  void refuse(){eligible=false;stage=Refused;increment(refused);}
  void reset(int why,uint32_t generation,uint64_t now,bool desired){
    reason=why;reset_at=now;
    const bool expected=eligible&&stage==AwaitReset&&old_generation==generation&&desired&&why==19;
    if(!increment(resets)||!expected){refuse();return;}
    stage=AwaitSync;
  }
  void synced(uint64_t now){
    if(stage!=AwaitSync)return;
    if(now<reset_at||!increment(syncs)){refuse();return;}
    sync_at=now;stage=AwaitDrain;
  }
  bool drained(const R2ClientService& c,uint64_t now){
    if(stage!=AwaitDrain||!eligible||c.generation!=old_generation||!c.off()||
       !c.stop_requested_us||c.stop_deadline_us-c.stop_requested_us!=2000000||
       c.completed_us<c.stop_requested_us||c.completed_us>=c.stop_deadline_us||
       now<c.completed_us||now<sync_at||now>UINT64_MAX-500000)return false;
    old_stop_requested=c.stop_requested_us;old_stop_deadline=c.stop_deadline_us;old_completed=c.completed_us;
    quiet_until=now+500000;stage=Quiet;return true;
  }
  bool due(uint64_t now)const{return eligible&&stage==Quiet&&now>=quiet_until;}
  void rearmed(){if(!increment(rearms)){refuse();return;}eligible=false;stage=Rearmed;}
};
static_assert(sizeof(G1ResetRecovery)<=80,"Reset recovery exceeds bounded RAM budget");
}

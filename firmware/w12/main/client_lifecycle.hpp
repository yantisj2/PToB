#pragma once
#include <cstdint>
#include <limits>
namespace v12 {
constexpr uint64_t SecondUs=1000000, PeriodicUs=31250, RelayPhaseUs=15625;
constexpr uint16_t MaxData=191,RootBytes=158,RelayBytes=182,GrantBytes=190;
enum class Mode:uint8_t { Full, Half350, Half400, Half450, Coarse, GM, Relay };
enum class Lifecycle:uint8_t { Off, Acquire, Following, Stopping, Quiet, Fault };
constexpr uint32_t window(Mode m){
  return m==Mode::Half350?350000:m==Mode::Half400?400000:m==Mode::Half450?450000:
    m==Mode::Coarse?100000:1000000;
}
constexpr bool half(Mode m){return m==Mode::Half350||m==Mode::Half400||m==Mode::Half450;}
constexpr bool online(Mode m){return m==Mode::Full||m==Mode::GM||m==Mode::Relay;}
// This is a controller-API reservation ledger, never an RF listening proof.
// All operation times are callback/task diagnostics, never physical references.
struct ClientLifecycle {
  Lifecycle state=Lifecycle::Off; Mode mode=Mode::Full;
  uint64_t start=0, stop_by=0, cycle_end=0, shutdown_started=0;
  uint32_t shutdown_allowance_us=5000, cycle=0, misses=0, faults=0;
  bool requested=false,scan=false,pending=false,synced=false;
  bool configure(Mode next){
    if(state!=Lifecycle::Off||requested)return false;
    mode=next;return true;
  }
  // Half-duty diagnostic reception reserves its local interval. Production
  // phase coordination needs a qualified run-epoch map, which is absent here.
  bool open(uint64_t now){
    if(!requested||state==Lifecycle::Fault||state==Lifecycle::Stopping||
       scan||pending||synced||shutdown_allowance_us>=window(mode))return false;
    if(state==Lifecycle::Quiet&&now<cycle_end)return false;
    if(now>UINT64_MAX-(mode==Mode::Coarse?4000000:SecondUs))return fail();
    start=now;stop_by=now+window(mode)-shutdown_allowance_us;
    cycle_end=now+(mode==Mode::Coarse?4000000:SecondUs);
    if(cycle==UINT32_MAX)return fail();
    ++cycle;state=Lifecycle::Acquire;return true;
  }
  bool should_stop(uint64_t now)const{
    return state!=Lifecycle::Off&&state!=Lifecycle::Quiet&&state!=Lifecycle::Fault&&
      (!requested||(!online(mode)&&now>=stop_by));
  }
  void stop_begin(uint64_t now){
    if(state==Lifecycle::Fault)return;
    if(state!=Lifecycle::Stopping){shutdown_started=now;state=Lifecycle::Stopping;}
  }
  void drained(uint64_t now){
    if(scan||pending||synced||state==Lifecycle::Fault)return;
    if(!online(mode)&&requested&&now>start+window(mode)){fail();return;}
    state=requested?Lifecycle::Quiet:Lifecycle::Off;
  }
  bool late(uint64_t now)const{
    return state==Lifecycle::Stopping&&now>=shutdown_started&&
      now-shutdown_started>shutdown_allowance_us;
  }
  bool accept_report(uint64_t now)const{
    return requested&&synced&&state==Lifecycle::Following&&
      (online(mode)||now<stop_by);
  }
  bool fail(){if(faults!=UINT32_MAX)++faults;requested=false;state=Lifecycle::Fault;return false;}
};
template<typename T,unsigned Capacity> struct Ring {
  T records[Capacity]{};unsigned head=0,tail=0,count=0,peak=0;
  uint32_t drops=0;
  bool push(const T& value){
    if(count==Capacity){if(drops!=UINT32_MAX)++drops;return false;}
    records[head]=value;head=(head+1)%Capacity;++count;if(count>peak)peak=count;return true;
  }
  bool pop(T& value){if(!count)return false;value=records[tail];tail=(tail+1)%Capacity;--count;return true;}
};
}

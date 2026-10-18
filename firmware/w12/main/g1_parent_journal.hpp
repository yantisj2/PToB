#pragma once
#include <cstdint>
namespace v12 {
enum class G1ParentReason:uint8_t {
  Scope,Startup,HoldGM,RecoverGM,ReturnGM,LinkPoor,ClockUnavailable,
  ClockStep,SourceChange,ReplayExpired,RootHint,Manual,RelayOnly,Stop,BestCandidate,LockTrial
};
inline const char* g1_parent_reason(G1ParentReason r){
  constexpr const char* names[]={"SCOPE_CHANGE","STARTUP","HOLD_GM","RECOVER_GM","RETURN_GM","LINK_POOR","CLOCK_UNAVAILABLE",
    "CLOCK_STEP","SOURCE_CHANGE","REPLAY_EXPIRED","ROOT_HINT","MANUAL_PARENT","RELAY_ONLY","STOP","BEST_CANDIDATE","LOCK_TRIAL_EXPIRED"};
  return unsigned(r)<sizeof(names)/sizeof(names[0])?names[unsigned(r)]:"UNKNOWN";
}
struct G1ParentScope {
  uint64_t peer=0,boot=0,root=0,root_boot=0;
  uint32_t source=0,train=0;
  uint8_t depth=0;bool relay=false;
  bool same(const G1ParentScope& b)const{return peer==b.peer&&boot==b.boot&&root==b.root&&root_boot==b.root_boot&&
    source==b.source&&train==b.train&&depth==b.depth&&relay==b.relay;}
};
struct G1ParentEvent {
  G1ParentScope from{},to{};uint64_t at=0;uint32_t sequence=0;
  G1ParentReason reason=G1ParentReason::Scope;uint8_t decision=0;bool poor=false;
  int16_t current_score=INT16_MIN,best_score=INT16_MIN;
};
// Worker-owned bounded RAM only. Equal-parent renewals consume no entry.
// Source/boot/path changes and actual withdrawal are retained even when USB
// logging is off. The reader uses the existing short publication critical section.
class G1ParentJournal {
 public:static constexpr unsigned Capacity=8;
 private:G1ParentEvent rows_[Capacity]{};uint32_t sequence_=0;
 public:
  uint32_t observe(const G1ParentScope& next,uint64_t now,G1ParentReason reason,uint8_t decision,
                   int16_t current,int16_t best,bool poor){
    const auto old=sequence_?rows_[(sequence_-1)%Capacity].to:G1ParentScope{};
    if(old.same(next)||sequence_==UINT32_MAX)return 0;
    const auto sequence=sequence_+1;auto& e=rows_[(sequence-1)%Capacity];
    e={old,next,now,sequence,reason,decision,poor,current,best};sequence_=sequence;return sequence;
  }
  bool get(uint32_t sequence,G1ParentEvent& out)const{
    if(!sequence||sequence>sequence_||sequence_-sequence>=Capacity)return false;
    const auto& e=rows_[(sequence-1)%Capacity];if(e.sequence!=sequence)return false;out=e;return true;
  }
  bool next(uint32_t after,G1ParentEvent& out)const{
    if(after>=sequence_)return false;
    const auto oldest=sequence_>Capacity?sequence_-Capacity+1:1;
    return get(after<oldest?oldest:after+1,out);
  }
};
static_assert(sizeof(G1ParentJournal)<=1024,"Bounded parent history SRAM");
}

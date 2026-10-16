#pragma once
#include <cstring>
#include "g1_clock_prediction.hpp"
#include "rc7_radio_phase.hpp"
namespace v12 {
// Operator-authorized functional TX estimate experiment. All corrections have
// UNKNOWN bounds. No actual RF TX or absolute precision claim is made.
class Rc7RelayEstimate {
  rc7_path upstream_{};
  uint32_t train_=0;
  uint8_t desired_slot_=0;
 public:
  static constexpr unsigned SnapshotCapacity=4;
  struct PreparedPublication {
    rc7_path path{};rc5_origin origin{};G1ClockPrediction map{};
    uint64_t original20=0,queued_at20=0;
    uint32_t inherited_age_us=0,publication=0;
  };
  enum class SelectionReason:uint8_t {None,CurrentSource,Time,Decode,Publication,
    MissingSnapshot,SnapshotScope,SnapshotOrigin,SnapshotTime,SnapshotAge,SnapshotEnvelope};
 private:
  PreparedPublication snapshots_[SnapshotCapacity]{};uint8_t snapshot_head_=0;
  SelectionReason selected_reason_=SelectionReason::None;
  bool timing_suspended_=false;
  static bool same_origin(const rc5_origin& a,const rc5_origin& b){
    return a.root==b.root&&a.boot==b.boot&&a.epoch==b.epoch&&a.issued==b.issued&&
      a.expires==b.expires&&a.utc_offset==b.utc_offset&&a.frame==b.frame&&
      a.hold_us==b.hold_us&&a.hold_ns==b.hold_ns;
  }
  const PreparedPublication* snapshot(uint32_t publication)const{
    for(const auto& p:snapshots_)if(p.publication==publication)return &p;
    return nullptr;
  }
 public:
  SelectionReason selected_reason()const{return selected_reason_;}
  rc7_path path{};rc5_origin origin{};uint8_t proof[16]{};
  G1ClockPrediction map{};Rc7RadioPhase phase;
  uint64_t original20=0;uint32_t inherited_age_us=0,publication=0;
  rc5_correction sent[2]{};bool valid=false;
  void fence(){for(auto& s:snapshots_)s={};snapshot_head_=0;selected_reason_=SelectionReason::None;timing_suspended_=false;valid=false;path={};upstream_={};origin={};map={};phase.clear();original20=0;publication=0;sent[0]={};sent[1]={};}
  // A timing gap withdraws corrections and READY, not the authenticated radio
  // context. Canonical count0 PA packets preserve synchronization. Only a new
  // accepted parent original/map can resume time; source/path changes fence().
  bool withdraw_time(){
    if(!valid||timing_suspended_)return false;
    timing_suspended_=true;for(auto& s:snapshots_)s={};snapshot_head_=0;
    sent[0]={};sent[1]={};return true;
  }
  // Demand expiry retires the downstream train only. Keep the causal upstream
  // map/original age and phase reference; a new child gets a fresh train and
  // cannot consume any stopped publication or its delayed TX observation.
  bool retire_publication(){
    if(!valid)return true;
    if(train_==UINT32_MAX){fence();return false;}
    ++train_;auto& own=path.node[path.depth];own.train=own.epoch=train_;
    for(auto& s:snapshots_)s={};
    snapshot_head_=0;
    selected_reason_=SelectionReason::None;publication=0;sent[0]={};sent[1]={};
    return true;
  }
  // Only the serialized owner may select a slot after its owned scan. A
  // legal retained choice survives scope fencing without supplying any time.
  bool select_slot(uint8_t slot){
    if(!valid||slot<=upstream_.node[upstream_.depth].slot||slot>3)return false;
    desired_slot_=slot;
    if(path.node[path.depth].slot!=slot)fence();
    return true;
  }
  bool input(const rc7_path& parent,const rc5_origin& o,const uint8_t* root_proof,
             const G1ClockPrediction& model,uint64_t original,uint32_t incarnation,
             uint64_t available,uint32_t inherited_age,uint64_t self,uint64_t boot,
             const r2_group_config& group){
    if(!rc7_path_valid(&parent)||parent.depth>=2||parent.node[parent.depth].slot>=3||
       !self||!boot||!model.tick||o.root!=parent.node[0].id||o.boot!=parent.node[0].boot||
       o.epoch!=parent.source_epoch||o.hold_us||o.hold_ns||!original||available<original||
       available-original>=Rc7RadioPhase::MaxAge20||inherited_age>=500000)return false;
    for(unsigned i=0;i<=parent.depth;i++)if(parent.node[i].id==self)return false;
    uint8_t signed_proof[16];if(rc7_origin_sign(signed_proof,&o,&group)||
       (root_proof&&std::memcmp(signed_proof,root_proof,16)))return false;
    const bool same=valid&&rc7_path_same(&upstream_,&parent);
    if(!same){
      if(train_==UINT32_MAX){fence();return false;}
      fence();++train_;upstream_=parent;path=parent;++path.depth;
      const auto parent_slot=parent.node[parent.depth].slot;
      const uint8_t slot=desired_slot_>parent_slot&&desired_slot_<=3?desired_slot_:uint8_t(parent_slot+1);
      path.node[path.depth]={self,boot,train_,train_,slot,1};desired_slot_=slot;
      if(!rc7_path_valid(&path)||!phase.scope(parent,boot)){fence();return false;}
    }
    if(!phase.accepted(original,incarnation,available,true))return false;
    origin=o;std::memcpy(proof,signed_proof,16);map=model;original20=original;inherited_age_us=inherited_age;valid=true;timing_suspended_=false;return true;
  }
  bool live(uint64_t now20)const{
    if(!valid||timing_suspended_||!original20||now20<original20||now20-original20>=Rc7RadioPhase::MaxAge20||
       uint64_t(inherited_age_us)+(now20-original20+19)/20>=500000||now20<map.tick)return false;
    const auto n=map.relative_at(now20);
    return n>0&&uint64_t(n)>=origin.issued&&uint64_t(n)<origin.expires;
  }
  bool ready(uint64_t now20)const{return live(now20)&&publication&&sent[0].publication&&
    sent[0].generation==path.node[path.depth].train;}
  // Worker builds frozen bytes and a causal publication snapshot. Only commit
  // after expected-bank reservation and successful public PA data queuing.
  bool prepare_packet(uint8_t wire[190],PreparedPublication& prepared,
                      uint64_t now20,const r2_group_config& group)const{
    prepared={};
    if(!valid||!now20||publication==UINT32_MAX)return false;
    const bool timing=live(now20);
    rc7_packet p{};p.origin=origin;std::memcpy(p.proof,proof,16);
    const auto& own=path.node[path.depth];p.sender=own.id;p.boot=own.boot;p.train=own.train;
    p.epoch=own.epoch;p.depth=path.depth;p.slot=own.slot;p.publication=publication+1;
    for(const auto& c:sent)if(timing&&c.publication&&c.tx_ns>=origin.issued&&c.tx_ns<origin.expires&&
      c.generation==own.train&&uint64_t(inherited_age_us)+(now20-original20+19)/20<500000)p.correction[p.count++]=c;
    if(rc7_encode(wire,&p,&path,&group))return false;
    prepared={path,origin,timing?map:G1ClockPrediction{},timing?original20:0,now20,
      timing?inherited_age_us:0,p.publication};return true;
  }
  bool commit_packet(const PreparedPublication& prepared,uint64_t now20){
    if(!valid||publication==UINT32_MAX||prepared.publication!=publication+1||
       !rc7_path_same(&prepared.path,&path)||!same_origin(prepared.origin,origin)||
       !prepared.queued_at20||prepared.queued_at20>now20)return false;
    const bool timing=prepared.map.tick!=0;
    if(timing?(!prepared.original20||prepared.map.tick>prepared.queued_at20||
       prepared.original20>prepared.queued_at20||prepared.inherited_age_us>=500000):
       (prepared.original20||prepared.inherited_age_us))return false;
    // The SDK already owns these exact bytes. Burn their publication even if
    // queuing crossed expiry, so the next count0 update cannot reuse a pending
    // bank's publication. Expired/standby bytes get no usable timing snapshot.
    snapshots_[snapshot_head_]=timing&&live(now20)?prepared:PreparedPublication{};
    snapshot_head_=uint8_t((snapshot_head_+1)%SnapshotCapacity);
    publication=prepared.publication;return true;
  }
  // Initial-start convenience only. A queue/start failure must fence this owner.
  bool packet(uint8_t wire[190],uint64_t now20,const r2_group_config& group){
    PreparedPublication prepared{};
    return prepare_packet(wire,prepared,now20,group)&&commit_packet(prepared,now20);
  }
  bool selected(const uint8_t wire[190],uint16_t event,uint8_t channel,uint64_t estimate20,
                uint64_t now20,const r2_group_config& group){
    selected_reason_=SelectionReason::None;
    auto refuse=[&](SelectionReason reason){selected_reason_=reason;return false;};
    if(!live(now20))return refuse(SelectionReason::CurrentSource);
    if(!estimate20||estimate20>now20||channel>36)return refuse(SelectionReason::Time);
    rc7_packet selected{};
    if(rc7_decode(&selected,wire,190,&path,&group))return refuse(SelectionReason::Decode);
    if(!selected.publication||selected.publication>publication)return refuse(SelectionReason::Publication);
    const auto* frozen=snapshot(selected.publication);
    if(!frozen)return refuse(SelectionReason::MissingSnapshot);
    const auto& own=path.node[path.depth];
    if(!rc7_path_same(&frozen->path,&path)||selected.boot!=own.boot||
       selected.train!=own.train||selected.epoch!=own.epoch)return refuse(SelectionReason::SnapshotScope);
    if(!same_origin(selected.origin,frozen->origin))return refuse(SelectionReason::SnapshotOrigin);
    if(estimate20<frozen->map.tick||estimate20<frozen->original20)
      return refuse(SelectionReason::SnapshotTime);
    const auto n=frozen->map.relative_at(estimate20);
    const auto age=uint64_t(frozen->inherited_age_us)+(estimate20-frozen->original20+19)/20;
    if(age>=500000)return refuse(SelectionReason::SnapshotAge);
    if(n<=0||uint64_t(n)<selected.origin.issued||uint64_t(n)>=selected.origin.expires)
      return refuse(SelectionReason::SnapshotEnvelope);
    sent[1]=sent[0];sent[0]={selected.publication,event,channel,selected.train,uint32_t(age),uint64_t(n),UINT32_MAX};return true;
  }
  rc7_control capability(uint64_t self,uint64_t boot,uint64_t challenge,bool publishing)const{
    rc7_control c{};c.type=RC7_CAPABILITY;c.sender=self;c.sender_boot=boot;c.next_challenge=challenge;
    if(valid)c.path=path;
    c.publishing=publishing&&valid;return c;
  }
  bool response(rc7_control& out,const rc7_control& request,uint64_t next,uint64_t now20)const{
    out={};out.type=RC7_RESPONSE;out.sender=request.target;out.sender_boot=request.target_boot;
    out.target=request.sender;out.target_boot=request.sender_boot;out.challenge=request.challenge;
    out.nonce=request.nonce;out.sequence=request.sequence;out.lease_us=request.lease_us;out.next_challenge=next;
    if(!ready(now20))return true;
    const auto n=map.relative_at(now20);const uint64_t guard=1000000+(now20-original20)*50/3000;
    if(n<=0||uint64_t(n)>UINT64_MAX-guard||origin.expires<=uint64_t(n)+guard)return true;
    for(unsigned i=0;i<=path.depth;i++)if(path.node[i].id==request.sender)return false;
    out.ready=true;out.path=path;out.publication_floor=publication;
    out.root_lo_ns=uint64_t(n)>guard?uint64_t(n)-guard:0;out.root_hi_ns=uint64_t(n)+guard;
    out.source_expires_ns=origin.expires;return true;
  }
};
}

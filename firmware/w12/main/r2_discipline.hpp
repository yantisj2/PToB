#pragma once
#include "periodic_rx_model.hpp"
#include "clock_discipline.hpp"
extern "C" {
#include "precision_codec.h"
#include "r2/r2-g1/relay_codec.h"
}
namespace v12 {
// Physical packet association and original/correction assembly precede the
// existing 20MHz oscillator servo. Remote16MHz ticks are converted exactly;
// callback/USB times and C3 GPS samples can never become fit observations.
template<unsigned Pending=16> class R2DisciplineBase {
  static_assert(Pending>=8&&Pending<=16,"Bounded precision association storage");
 public:
  struct Scope {uint64_t identity=0,boot=0,until_us=0;uint32_t source=0,publication=0,local_epoch=0;uint8_t root=255;bool following=false;bool continuous_clock=false;};
  enum Refusal:uint32_t {MissingScope=1,Expiry=2,Capture=4,Event=8,Channel=16,Edge=32,Map=64,
    Epoch=128,Causality=256,PacketScope=512,Protocol=1024,Age=2048,Conflict=4096,
    InvalidCorrection=8192,Servo=16384};
  uint64_t model_until_us()const{return model_until_us_;}
  struct View {uint32_t accepted=0,refused=0,duplicates=0,overflow=0,rms_ns=UINT32_MAX,precision_ns=UINT32_MAX,hold_ms=0,hold_samples=0;int32_t trim_mppb=0;bool fit=false;uint64_t available20=0;ClockPrediction prediction{};
    uint32_t first_refusal=0,last_refusal=0,reason_counts[16]{};};
 private:
  struct Original {uint64_t local=0,created=0,expires=0;int64_t offset=0;uint32_t enclosure_ns=UINT32_MAX,publication=0;uint16_t event=0;uint8_t channel=255;bool used=false;};
  struct Correction {r2_precise_correction value{};uint64_t available=0;bool used=false;};
  Original originals_[Pending]{};Correction corrections_[Pending]{};
  Scope scope_{};CardputerDiscipline servo_{};
  uint32_t train_=0,remote_epoch_=0,session_=0,accepted_=0,refused_=0,duplicates_=0,overflow_=0;
  uint64_t available_=0,last_local_=0,model_until_us_=0;
  uint32_t first_refusal_=0,last_refusal_=0,reason_counts_[16]{};
  static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
  bool reject(uint32_t reason=Protocol){inc(refused_);if(!first_refusal_)first_refusal_=reason;last_refusal_=reason;
    for(unsigned i=0;i<16;i++){if(reason&(1u<<i))inc(reason_counts_[i]);}
    return false;}
  void clear(){for(auto& o:originals_)o={};for(auto& c:corrections_)c={};servo_.reset();available_=last_local_=model_until_us_=0;train_=remote_epoch_=0;
    if(session_!=UINT32_MAX)++session_;}
  static bool stale(uint64_t now,uint64_t before){return before>now||now-before>5000000;}
 public:
  void scope(Scope next){
    if(scope_.identity!=next.identity||scope_.boot!=next.boot||scope_.source!=next.source||scope_.publication!=next.publication||
       (scope_.local_epoch!=next.local_epoch&&!next.continuous_clock)||scope_.root!=next.root||
       (scope_.following!=next.following&&!next.continuous_clock))clear();
    scope_=next;
  }
  bool receive(const r2_precision_packet& p,const periodic_rx::Stamp& capture,uint64_t available20,uint64_t now_us,int64_t coarse_utc_ns,uint64_t maximum_scope_ns=1500000000){
    uint32_t refusal=0;
    if(!scope_.following||!scope_.identity||!scope_.boot||!scope_.source||!scope_.publication)refusal|=MissingScope;
    if(now_us>=scope_.until_us)refusal|=Expiry;
    if(!capture.actual||!capture.tick20)refusal|=Capture;
    if(!capture.event_valid)refusal|=Event;
    if(!capture.channel_valid||capture.channel>36)refusal|=Channel;
    if(!capture.edge_calibrated)refusal|=Edge;
    if(capture.map_enclosure_ns==UINT32_MAX)refusal|=Map;
    if(capture.epoch!=scope_.local_epoch)refusal|=Epoch;
    if(capture.available20<capture.tick20||available20<capture.available20||stale(available20,capture.tick20))refusal|=Causality;
    if(p.root!=scope_.root||p.boot!=scope_.boot||p.source_epoch!=scope_.source||p.train!=scope_.publication||!p.publication||!p.capture_epoch)refusal|=PacketScope;
    if(p.flags!=1||p.count>2||coarse_utc_ns<0||session_==UINT32_MAX)refusal|=Protocol;
    if(refusal)return reject(refusal);
    if(p.created_ns>INT64_MAX||p.utc_offset_ns==INT64_MIN||
       (p.utc_offset_ns>0&&p.created_ns>uint64_t(INT64_MAX-p.utc_offset_ns))||
       (p.utc_offset_ns<0&&p.created_ns<uint64_t(-p.utc_offset_ns)))return reject();
    const int64_t creation=int64_t(p.created_ns)+p.utc_offset_ns;
    const uint64_t distance=creation>coarse_utc_ns?uint64_t(creation-coarse_utc_ns):uint64_t(coarse_utc_ns-creation);
    if(distance>=(maximum_scope_ns==1500000000?250000000:maximum_scope_ns)||p.expires_ns<=p.created_ns||p.expires_ns-p.created_ns>maximum_scope_ns)return reject(Age);
    if(train_&&(train_!=p.train||remote_epoch_!=p.capture_epoch))clear();
    train_=p.train;remote_epoch_=p.capture_epoch;
    unsigned slot=Pending;
    for(unsigned i=0;i<Pending;i++){const auto& o=originals_[i];
      if(o.used&&!stale(available20,o.local)&&o.publication==p.publication&&o.event==capture.event&&o.channel==capture.channel){inc(duplicates_);return false;}
      if(!o.used||stale(available20,o.local))slot=i;}
    if(slot==Pending){inc(overflow_);clear();return false;}
    originals_[slot]={capture.tick20,p.created_ns,p.expires_ns,p.utc_offset_ns,capture.map_enclosure_ns,p.publication,capture.event,capture.channel,true};
    for(unsigned k=0;k<p.count;k++){const auto& v=p.correction[k];slot=Pending;bool duplicate=false;
      for(unsigned i=0;i<Pending;i++){const auto& c=corrections_[i];
        if(c.used&&!stale(available20,c.available)&&c.value.publication==v.publication&&c.value.event==v.event&&c.value.channel==v.channel){
          // Conflicting correction of the same tuple fences the entire model.
          if(c.value.ticks!=v.ticks||c.value.generation!=v.generation||c.value.expires_ns!=v.expires_ns){clear();return reject(Conflict);}
          duplicate=true;break;}
        if(!c.used||stale(available20,c.available))slot=i;}
      if(duplicate)continue;
      if(slot==Pending){inc(overflow_);clear();return false;}
      corrections_[slot]={v,available20,true};}
    return step(available20,now_us);
  }
  bool step(uint64_t available20,uint64_t now_us){
    if(!scope_.following||now_us>=scope_.until_us||available20<=available_)return false;
    Original* best=nullptr;Correction* correction=nullptr;
    for(auto& o:originals_)if(o.used&&!stale(available20,o.local)&&o.local>last_local_&&o.local>available_)
      for(auto& c:corrections_)if(c.used&&!stale(available20,c.available)&&o.publication==c.value.publication&&o.event==c.value.event&&o.channel==c.value.channel&&(!best||o.local<best->local)){best=&o;correction=&c;}
    if(!best)return false;
    const auto c=correction->value;correction->used=false;
    // Keep the original identity in the duplicate population after consumption.
    last_local_=best->local;
    if(c.ticks>uint64_t(INT64_MAX)/125||!c.generation||c.expires_ns>best->expires)return reject(InvalidCorrection);
    const uint64_t ns=c.ticks*125/2;
    if(ns<best->created||ns>=c.expires_ns)return reject(InvalidCorrection);
    return apply(*best,c,available20,now_us);
  }
 private:
  bool apply(const Original& o,const r2_precise_correction& c,uint64_t available20,uint64_t now_us){
    const uint64_t raw=c.ticks*125/2;
    if(o.offset==INT64_MIN||(o.offset>0&&raw>uint64_t(INT64_MAX-o.offset))||(o.offset<0&&raw<uint64_t(-o.offset)))return reject();
    const int64_t utc=int64_t(raw)+o.offset;
    if(o.enclosure_ns==UINT32_MAX||o.enclosure_ns>2000000000)return reject();
    // The corrected model has its own immutable signed expiry. A fresh JOIN
    // may renew a coarse bootstrap of the same scope, never this old model.
    if(available20<o.local||available20-o.local>UINT64_MAX/50)return reject(Causality);
    const uint64_t elapsed_ns=(available20-o.local)*50;
    const uint64_t permission_ns=c.expires_ns-raw;
    if(permission_ns<=elapsed_ns||permission_ns-elapsed_ns<=o.enclosure_ns)return reject(Expiry);
    const uint64_t carried_guard_ns=elapsed_ns/500+100000; // Same conservative2000ppm+100us bootstrap guard.
    if(permission_ns-elapsed_ns-o.enclosure_ns<=carried_guard_ns)return reject(Expiry);
    const uint64_t remaining_us=(permission_ns-elapsed_ns-o.enclosure_ns-carried_guard_ns)/1000;
    if(!remaining_us||now_us>UINT64_MAX-remaining_us)return reject(Expiry);
    const uint32_t uncertainty=(o.enclosure_ns+999)/1000;
    const bool ok=servo_.observe_ns(o.local,utc,uncertainty,CardputerDiscipline::NetworkBle,session_,true,false,o.enclosure_ns);
    available_=available20;if(ok){model_until_us_=now_us+remaining_us;servo_.reference_available(available20);inc(accepted_);}else reject(Servo);return ok;
  }
 public:
  bool authenticated_receive(const r2_precision_packet& p,const periodic_rx::Stamp& c,uint64_t available20,uint64_t now_us,int64_t expected){
    return receive(p,c,available20,now_us,expected);
  }
  // Caller has verified both GR3 MACs and the direct root/boot/scope. Physical
  // capture flags remain mandatory; a signed correction cannot supply them.
  bool authenticated_gr3(const r2_relay_packet& g,const periodic_rx::Stamp& c,uint64_t available20,uint64_t now_us,int64_t expected){
    if(g.route.depth||g.route.slot||g.route.transmitter!=g.origin.root||g.route.parent!=g.origin.root||
       g.route.boot!=g.origin.boot||g.route.map_generation!=g.route.train||g.origin.hold_horizon_us||g.origin.hold_budget_ns)return reject();
    r2_precision_packet p{};p.boot=g.origin.boot;p.created_ns=g.origin.created_ns;p.expires_ns=g.origin.expires_ns;
    p.utc_offset_ns=g.origin.utc_offset_ns;p.source_epoch=g.origin.source_epoch;p.train=g.route.train;
    p.publication=g.route.publication;p.capture_epoch=g.route.capture_epoch;p.root=g.origin.root;p.flags=1;p.count=g.count;
    for(unsigned i=0;i<g.count&&i<2;i++){const auto& x=g.correction[i];
      if(x.generation!=g.route.map_generation)return reject();
      p.correction[i]={x.publication,x.event,x.channel,x.ticks,x.generation,g.origin.expires_ns};}
    return receive(p,c,available20,now_us,expected,1650000000);
  }
  static bool delivered(const View& q,uint64_t now20,uint64_t& utc_us) {
    if(!q.fit||!q.available20||now20<q.available20)return false;
    const int64_t ns=q.prediction.at(now20);
    if(ns<=0)return false;
    utc_us=uint64_t(ns)/1000;return true;
  }
  View view(uint64_t now20,uint64_t now_us)const{
    View v;v.accepted=accepted_;v.refused=refused_;v.duplicates=duplicates_;v.overflow=overflow_;
    v.first_refusal=first_refusal_;v.last_refusal=last_refusal_;for(unsigned i=0;i<16;i++)v.reason_counts[i]=reason_counts_[i];
    if(!scope_.following||now_us>=scope_.until_us||!model_until_us_||now_us>=model_until_us_||!servo_.fresh(now20))return v;
    const auto q=servo_.quality(now20);v.rms_ns=q.rms_ns;v.precision_ns=q.precision_ns;v.fit=q.fit;v.trim_mppb=q.trim_mppb;
    v.hold_ms=servo_.hold_budget(now20,0,false,99);v.hold_samples=servo_.hold_samples(now20,99);
    v.available20=available_;v.prediction=servo_.prediction();return v;
  }
};
using R2Discipline=R2DisciplineBase<>;
// G1 has the same16Hz/250ms association horizon as its functional assembler.
// Eight entries cover five originals and the two-correction frontier. Overflow
// still clears/refuses; event/channel/edge and all precision gates are unchanged.
using G1StrictDiscipline=R2DisciplineBase<8>;
}

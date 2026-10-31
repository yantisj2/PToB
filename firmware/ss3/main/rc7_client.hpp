#pragma once
#include "periodic_rx_model.hpp"
extern "C" {
#include "r2/rc7/protocol/rc_dynamic.h"
}
namespace v12 {
// Codec history uses a normalized 16MHz counter only for age/replay checks.
// Exact 20MHz captures are retained for the existing W12 oscillator model.
// floor(4*t/5) cannot overflow and loses <62.5ns; it is not a calibration.
inline uint64_t rc7_tick16(uint64_t t){return t/5*4+(t%5)*4/5;}
struct Rc7Client {
  struct Capture {periodic_rx::Stamp stamp{};uint32_t publication=0;};
  struct Challenge {uint64_t id=0,boot=0,high=0;};
  rc7_receiver receiver{};rc7_control request{};
  Capture captures[RC5_HISTORY]{},completed{};Challenge challenges[8]{};
  uint64_t lease_until=0,renew_at=0,progress_at=0,retry_at=0,request_at=0;
  uint32_t sequence=0,capabilities=0,requests=0,responses=0,pending=0,rejected=0;
  uint32_t local_epoch=0;bool utc_known=false;
  bool admission_due(uint64_t now,bool poor,bool locked)const {
    // Five-minute renewal is distinct from recovery. Delivery hysteresis
    // alone must not reconnect a still-locked clock every cooldown interval.
    return now>=renew_at||(!locked&&now>=retry_at&&
      (poor||(now>=progress_at&&now-progress_at>=2000000)));
  }
  bool path_usable(const rc7_path& p,uint64_t self)const {
    if(rc7_path_empty(&p))return true;
    if(!rc7_path_valid(&p)||!p.depth)return false;
    for(unsigned i=0;i<=p.depth;i++)if(p.node[i].id==self)return false;
    return true;
  }
  void fence(){receiver={};for(auto& c:captures)c={};local_epoch=0;utc_known=false;lease_until=renew_at=progress_at=0;}
  bool make_request(const rc7_control& cap,uint64_t self,uint64_t boot,uint64_t nonce,uint64_t now,bool rescan){
    if(cap.type!=RC7_CAPABILITY||!path_usable(cap.path,self)||sequence==UINT32_MAX||!nonce)return false;
    unsigned at=8;for(unsigned i=0;i<8;i++)if(challenges[i].id==cap.sender){at=i;break;}
    if(at==8){at=0;for(unsigned i=0;i<8;i++)if(!challenges[i].id){at=i;break;}}
    auto& h=challenges[at];
    if(h.id==cap.sender&&h.boot==cap.sender_boot&&cap.next_challenge<=h.high)return false;
    h={cap.sender,cap.sender_boot,cap.next_challenge};
    request={};request.type=RC7_REQUEST;request.sender=self;request.target=cap.sender;
    request.sender_boot=boot;request.target_boot=cap.sender_boot;request.challenge=cap.next_challenge;
    request.nonce=nonce;request.sequence=++sequence;request.lease_us=RC_DYNAMIC_LEASE_US;request.path=cap.path;
    request.operation=rescan?1:0;request_at=now;return true;
  }
  bool response_matches(const rc7_control& c,uint64_t now)const {
    return c.type==RC7_RESPONSE&&c.sender==request.target&&c.target==request.sender&&
      c.sender_boot==request.target_boot&&c.target_boot==request.sender_boot&&
      c.challenge==request.challenge&&c.nonce==request.nonce&&c.sequence==request.sequence&&
      c.lease_us<=request.lease_us&&now>=request_at&&now-request_at<2000000&&path_usable(c.path,request.sender);
  }
  bool bind(const rc7_control& c,uint64_t now,uint64_t actual20){
    if(!c.ready||!response_matches(c,now)||!actual20||rc7_path_empty(&c.path))return false;
    const auto age=(now-request_at)*1001; // full causal exchange, conservatively >300ppm
    if(c.root_hi_ns>UINT64_MAX-age||now>UINT64_MAX-c.lease_us)return false;
    rc5_admission a{};a.parent_boot=c.sender_boot;a.root_boot=c.path.node[0].boot;
    a.root_epoch=c.path.source_epoch;a.parent_train=c.path.node[c.path.depth].train;
    a.parent_epoch=c.path.node[c.path.depth].epoch;a.publication_floor=c.publication_floor;
    a.now_tick=rc7_tick16(actual20);a.root_lo_ns=c.root_lo_ns;a.root_hi_ns=c.root_hi_ns+age;
    a.root_expires_ns=c.source_expires_ns;
    const bool same=receiver.clock.bound&&rc7_path_same(&receiver.path,&c.path);
    if(!rc7_bind(&receiver,&c.path,request.sender,false,255,&a))return false;
    if(!same){for(auto& row:captures)row={};local_epoch=0;utc_known=false;}
    lease_until=now+c.lease_us;renew_at=now+RC_DYNAMIC_RENEW_US;
    // A renewal is admission work, never a corrected timing update. Preserve
    // the last actual stream progress so repeated renewals cannot hide a gap.
    if(!same)progress_at=now;
    return true;
  }
  static bool captured(const periodic_rx::Stamp& s,uint64_t available20){
    return s.actual&&s.association_observed&&s.tick20&&s.epoch&&s.sequence&&s.channel<=36&&
      s.map_enclosure_ns!=UINT32_MAX&&s.available20>=s.tick20&&available20>=s.available20&&
      available20-s.tick20<=uint64_t(periodic_rx::MaxAgeUs)*20;
  }
  const Capture* receive(const rc7_packet& p,const periodic_rx::Stamp& s,uint64_t available20){
    if(!captured(s,available20))return nullptr;
    if(local_epoch!=s.epoch){for(auto& q:receiver.clock.pending)q={};for(auto& q:captures)q={};local_epoch=s.epoch;}
    const bool complete=rc7_receive(&receiver,&p,rc7_tick16(s.tick20),s.event,s.channel,rc7_tick16(available20));
    const Capture* matched=nullptr;
    if(complete){const auto& r=receiver.clock.reference;for(const auto& q:captures)
      if(q.publication==r.publication&&q.stamp.event==r.event&&q.stamp.channel==r.channel&&
         rc7_tick16(q.stamp.tick20)==r.rx){completed=q;matched=&completed;break;}}
    // Save only originals actually admitted by the portable history. No
    // duplicate/consumed original can overwrite the original physical capture.
    for(unsigned i=0;i<RC5_HISTORY;i++){const auto& q=receiver.clock.pending[i];
      if(q.used&&q.packet.publication==p.publication&&q.event==s.event&&q.channel==s.channel&&
         q.rx==rc7_tick16(s.tick20)&&(captures[i].publication!=p.publication||captures[i].stamp.event!=s.event||captures[i].stamp.channel!=s.channel))captures[i]={s,p.publication};
    }
    return matched;
  }
};
}

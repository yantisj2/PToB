// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "rc9.hpp"
#include <algorithm>
#include <cstring>
namespace v12::rc9 {
struct View {
 uint64_t sampled_us=0,utc_us=0,until_us=0,root=0,root_boot=0,server=0,server_boot=0,emitter=0,emitter_boot=0;
 uint32_t source_epoch=0,publication=0,link_rms_ns=UINT32_MAX;
 uint8_t link_samples=0,assurance=0,authority=0;
 bool valid=false,bound=false;
};
// Original causal exchange bounds publisher monotonic time. 2000ppm is a
// conservative clock policy assumption, NOT measured RF/UTC uncertainty.
// New PA snapshots can advance UTC but cannot renew their original RC8 hold.
class Client {
 struct Challenge {uint64_t id=0,boot=0,high=0;} challenges_[8]{};
 Time sample_{};
 uint64_t request_at_=0,response_at_=0,server_at_=0,origin_at_=0,until_=0,last_sampled_=0,self_=0;
 uint32_t publication_=0;
 bool bound_=false,have_sample_=false,pending_=false,withdrawn_=false;
 static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
 static bool add(uint64_t a,uint64_t b,uint64_t& out){if(b>UINT64_MAX-a)return false;out=a+b;return true;}
 static bool scale(uint64_t us,uint64_t numerator,uint64_t denominator,uint64_t& out){
  if(us>UINT64_MAX/numerator)return false;out=us*numerator/denominator;return true;
 }
 bool interval(uint64_t now,uint64_t& lo,uint64_t& hi)const{
  if(!bound_||now<response_at_||now<origin_at_||now>=lease_until)return false;
  uint64_t a=0,b=0;return scale(now-response_at_,998000,1000000,a)&&scale(now-origin_at_,1002000,1000000,b)&&add(server_at_,a,lo)&&add(server_at_,b,hi);
 }
 bool deadline(uint64_t remote,uint64_t& local)const{
  if(remote<=server_at_)return false;uint64_t delta=0;
  return scale(remote-server_at_,1000000,1002000,delta)&&add(origin_at_,delta,local);
 }
 public:
 Control request{};
 uint64_t lease_until=0,renew_at=0;
 uint32_t sequence=0,accepted=0,rejected=0;
 void fence(){sample_={};request={};request_at_=response_at_=server_at_=origin_at_=until_=last_sampled_=self_=0;publication_=0;bound_=have_sample_=pending_=withdrawn_=false;lease_until=renew_at=0;}
 bool make_request(const Control& cap,uint64_t self,uint64_t boot,uint64_t nonce,uint64_t now){
  if(!valid_control(cap)||cap.type!=Type::Capability||cap.sender==self||!self||!boot||!nonce||sequence==UINT32_MAX)return false;
  unsigned at=8;for(unsigned i=0;i<8;i++)if(challenges_[i].id==cap.sender){at=i;break;}
  if(at==8){at=0;for(unsigned i=0;i<8;i++)if(!challenges_[i].id){at=i;break;}}
  auto& h=challenges_[at];if(h.id==cap.sender&&h.boot==cap.sender_boot&&cap.next_challenge<=h.high)return false;
  h={cap.sender,cap.sender_boot,cap.next_challenge};request={};request.type=Type::Request;
  request.sender=self;request.sender_boot=boot;request.target=cap.sender;request.target_boot=cap.sender_boot;
  request.challenge=cap.next_challenge;request.nonce=nonce;request.sequence=++sequence;request.lease_us=LeaseUs;request_at_=now;pending_=true;return true;
 }
 bool bind(const Control& c,uint64_t now){
  if(!pending_||!valid_control(c)||c.type!=Type::Response||c.sender!=request.target||c.sender_boot!=request.target_boot||
     c.target!=request.sender||c.target_boot!=request.sender_boot||c.challenge!=request.challenge||c.nonce!=request.nonce||
     c.sequence!=request.sequence||c.lease_us>request.lease_us||now<request_at_||now-request_at_>=ExchangeUs||request_at_>UINT64_MAX-c.lease_us)return false;
  const bool same=bound_&&sample_.sender==c.sender&&sample_.sender_boot==c.sender_boot&&now<lease_until;
  if(same&&c.server_us<server_at_)return false;
  if(!same){sample_={};publication_=c.publication_floor-1;have_sample_=withdrawn_=false;until_=last_sampled_=0;}
  else if(c.publication_floor<=publication_){/* Retain replay high-water on renewal. */}
  else publication_=c.publication_floor-1;
  // Separate conversion anchor: a pending renewal never mutates a live map.
  origin_at_=request_at_;response_at_=now;server_at_=c.server_us;
  sample_.sender=c.sender;sample_.sender_boot=c.sender_boot;self_=request.sender;pending_=false;
  lease_until=request_at_+c.lease_us;renew_at=request_at_+std::min<uint32_t>(RenewUs,c.lease_us);bound_=true;
  return now<lease_until;
 }
 bool receive(const Time& p,uint64_t now){
  uint64_t lo=0,hi=0;
  if(!valid_time(p)||!interval(now,lo,hi)||p.sender!=sample_.sender||p.sender_boot!=sample_.sender_boot||
     p.publication<=publication_||p.sampled_us<server_at_||p.sampled_us<last_sampled_||p.sampled_us>hi||
     (p.valid&&(p.root==self_||p.emitter==self_))){inc(rejected);return false;}
  uint64_t expiry=0;
  if(p.valid&&(!deadline(p.until_us,expiry)||now>=expiry)){inc(rejected);return false;}
  const bool same=sample_.valid&&p.generation==sample_.generation&&!std::memcmp(p.context,sample_.context,16);
  if(p.valid&&same){
   if(p.root!=sample_.root||p.emitter!=sample_.emitter||p.source_epoch!=sample_.source_epoch||p.kind!=sample_.kind||
      (sample_.root_boot&&p.root_boot!=sample_.root_boot)||(sample_.emitter_boot&&p.emitter_boot!=sample_.emitter_boot)||
      (sample_.stream&&p.stream!=sample_.stream)){inc(rejected);return false;}
   if(p.corrected_sequence<sample_.corrected_sequence){inc(rejected);return false;}
   if(p.corrected_sequence==sample_.corrected_sequence){
    if(withdrawn_||p.edge_us!=sample_.edge_us||p.event_ns!=sample_.event_ns||p.root!=sample_.root||p.source_epoch!=sample_.source_epoch||p.until_us>sample_.until_us){inc(rejected);return false;}
    expiry=std::min(expiry,until_);
   }else if(p.edge_us<=sample_.edge_us||p.event_ns<=sample_.event_ns){inc(rejected);return false;}
  }
  publication_=p.publication;last_sampled_=p.sampled_us;if(p.valid)sample_=p;have_sample_=p.valid;withdrawn_=!p.valid;until_=p.valid?std::min(expiry,lease_until):0;
  inc(accepted);return true;
 }
 View view(uint64_t now)const{
  View v;v.sampled_us=now;v.server=sample_.sender;v.server_boot=sample_.sender_boot;
  v.bound=bound_&&now>=response_at_&&now<lease_until;
  if(!v.bound||!have_sample_||now>=until_)return v;
  uint64_t lo=0,hi=0;if(!interval(now,lo,hi))return v;
  const auto remote=lo+(hi-lo)/2;
  if(remote<sample_.edge_us||!add(sample_.event_ns/1000,remote-sample_.edge_us,v.utc_us))return v;
  v.until_us=until_;v.root=sample_.root;v.root_boot=sample_.root_boot;v.source_epoch=sample_.source_epoch;
  v.emitter=sample_.emitter;v.emitter_boot=sample_.emitter_boot;v.publication=publication_;v.assurance=sample_.assurance;v.authority=sample_.authority;
  v.link_rms_ns=sample_.link_rms_ns;v.link_samples=sample_.link_samples;v.valid=true;return v;
 }
};
}

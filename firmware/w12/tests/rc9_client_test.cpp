// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "rc9/client.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace v12::rc9;
static const r2_group_config group={{1,2,3},7,9};
static Control cap(){Control c;c.sender=11;c.sender_boot=22;c.next_challenge=7;return c;}
static Control reply(const Client& c,uint64_t remote=5000000){
 auto r=c.request;r.type=Type::Response;std::swap(r.sender,r.target);std::swap(r.sender_boot,r.target_boot);
 r.next_challenge=r.challenge+1;r.server_us=remote;r.publication_floor=1;return r;
}
static Client bound(){Client c;assert(c.make_request(cap(),33,44,99,1000000));assert(c.bind(reply(c),1020000));return c;}
static Time packet(){
 Time p;p.sender=11;p.sender_boot=22;p.publication=1;p.sampled_us=5100000;p.edge_us=4900000;p.until_us=p.edge_us+HoldUs;
 p.event_ns=1700000000000000000;p.created_ns=p.event_ns-1000000000;p.expires_ns=p.event_ns+1000000000;
 p.root=55;p.emitter=66;p.source_epoch=1;p.generation=2;p.corrected_sequence=10;p.context[0]=1;p.kind=2;p.valid=true;return p;
}
template<size_t N,class Decode> void corrupt(const uint8_t (&wire)[N],Decode decode){
 uint8_t copy[N];for(unsigned i=0;i<N;i++){std::memcpy(copy,wire,N);copy[i]^=1;assert(!decode(copy,N));}
 for(size_t n=0;n<N;n++)assert(!decode(wire,n));assert(!decode(wire,N+1));assert(!decode(nullptr,N));
}
static void dump(const char* name,const uint8_t* w,size_t n){std::printf("%s ",name);for(size_t i=0;i<n;i++)std::printf("%02x",w[i]);std::puts("");}
int main(){
 uint8_t cw[ControlBytes],aw[CapabilityBytes],tw[TimeBytes];Control cd;Time td;auto a=cap();auto c=bound();auto p=packet();
 assert(encode_capability(aw,a,group)&&decode_capability(cd,aw,sizeof aw,group));
 assert(cd.sender==11&&cd.next_challenge==7);dump("capability",aw,sizeof aw);
 auto r=reply(c);assert(encode_control(cw,r,group)&&decode_control(cd,cw,sizeof cw,group));dump("response",cw,sizeof cw);
 assert(encode_time(tw,p,group)&&decode_time(td,tw,sizeof tw,group));dump("time",tw,sizeof tw);
 corrupt(aw,[&](const uint8_t* w,size_t n){return decode_capability(cd,w,n,group);});
 corrupt(cw,[&](const uint8_t* w,size_t n){return decode_control(cd,w,n,group);});
 corrupt(tw,[&](const uint8_t* w,size_t n){return decode_time(td,w,n,group);});
 auto wrong=group;wrong.id++;assert(!decode_time(td,tw,sizeof tw,wrong));wrong=group;wrong.psk[0]++;assert(!decode_control(cd,cw,sizeof cw,wrong));
 // Independently authenticated malformed reserved fields/bounds stay rejected.
 tw[156]=0;assert(!r2_group_mac(tw+174,&group,"RC9TIME1",tw,174));assert(!decode_time(td,tw,sizeof tw,group));
 assert(encode_time(tw,p,group));tw[6]=7;assert(!r2_group_mac(tw+174,&group,"RC9TIME1",tw,174));assert(!decode_time(td,tw,sizeof tw,group));
 assert(!decode_control(cd,aw,sizeof aw,group)&&!decode_capability(cd,cw,sizeof cw,group));
 for(unsigned bad=0;bad<12;bad++){
  auto b=p;if(bad==0)b.until_us=b.edge_us+HoldUs+1;if(bad==1)b.edge_us=b.sampled_us+1;if(bad==2)b.until_us=b.sampled_us;
  if(bad==3)b.expires_ns=b.event_ns;if(bad==4)b.root=b.sender;if(bad==5)b.context[0]=0;if(bad==6)b.assurance=3;if(bad==7)b.kind=0;if(bad==8)b.generation=0;if(bad==9)b.assurance=2;if(bad==10)b.authority=1;if(bad==11)b.kind=1;
  assert(!encode_time(tw,b,group));
 }
 // Exact challenge/nonce/factory/boot/lease; finite original exchange, no replay.
 for(unsigned bad=0;bad<10;bad++){
  Client t;assert(t.make_request(a,33,44,99,1000000));auto b=reply(t);
  if(bad==0)b.target++;if(bad==1)b.target_boot++;if(bad==2)b.sender++;if(bad==3)b.sender_boot++;
  if(bad==4)b.nonce++;if(bad==5)b.challenge++;if(bad==6)b.sequence++;if(bad==7)b.lease_us++;
  if(bad==8)b.publication_floor=0;if(bad==9)b.next_challenge=b.challenge;
  assert(!t.bind(b,1020000));assert(!t.view(1020000).valid);
 }
 {Client t;assert(t.make_request(a,33,44,99,1000000));assert(!t.bind(reply(t),1000000+ExchangeUs));}
 assert(!c.bind(r,1020001));assert(!c.make_request(a,33,44,100,1030000));
 assert(c.receive(p,1120000));auto v=c.view(1120000);assert(v.valid&&v.bound&&v.root_boot==0&&v.emitter_boot==0&&v.assurance==0&&v.authority==0);
 assert(v.utc_us>p.event_ns/1000&&v.until_us<1000000+p.until_us-5000000);const auto original=v.until_us;
 for(uint64_t now=1120000;now<original;now+=100000)assert(c.view(now).until_us==original);
 assert(!c.view(original).valid&&c.view(original).bound); // UTC expires; demand lease survives.
 assert(!c.receive(p,1120001));auto old=p;old.publication++;old.until_us++;assert(!c.receive(old,1120001));
 // Same physical pair in newer snapshots cannot renew its local expiry.
 old=p;old.publication++;old.sampled_us+=10000;assert(c.receive(old,1130000));assert(c.view(1130000).until_us==original);
 // Newer authenticated withdrawal revokes now; duplicate/old wire cannot revive.
 auto off=old;off.publication++;off.sampled_us++;off.valid=false;assert(c.receive(off,1130001));
 assert(!c.view(1130001).valid&&c.view(1130001).bound);assert(!c.receive(off,1130002));
 old.publication=off.publication+1;old.sampled_us+=2;assert(!c.receive(old,1130003));
 // Recovery needs a fresh physical pair, with original event and hold.
 old.corrected_sequence++;old.edge_us+=100000;old.event_ns+=100000000;old.expires_ns+=100000000;old.until_us=old.edge_us+HoldUs;
 assert(c.receive(old,1130003));assert(c.view(1130003).valid);
 // Pending/failed renewal cannot mutate a live conversion or extend a hold.
 const auto before=c.view(1130003);a.next_challenge=8;assert(c.make_request(a,33,44,101,1130003));
 const auto pending=c.view(1130003);assert(pending.utc_us==before.utc_us&&pending.until_us==before.until_us);
 auto renew=reply(c,5110000);assert(c.bind(renew,1140003));assert(c.view(1140003).until_us==before.until_us);
 auto repeat=old;repeat.publication++;repeat.sampled_us=5140000;assert(c.receive(repeat,1160003));assert(c.view(1160003).until_us<=before.until_us);
 for(unsigned bad=0;bad<6;bad++){
  auto t=bound();auto b=p;if(bad==0)b.sender++;if(bad==1)b.sender_boot++;if(bad==2)b.root=33;if(bad==3)b.emitter=33;if(bad==4)b.sampled_us=99999999;if(bad==5)b.sampled_us=4999999;
  assert(!t.receive(b,1120000));
 }
 auto t=bound();assert(!t.receive(p,t.lease_until));t.fence();assert(!t.receive(p,1120000)&&!t.view(1120000).bound);
 // Source/context/generation replacement cannot extend an unchanged pair.
 t=bound();assert(t.receive(p,1120000));auto fresh=p;fresh.publication++;fresh.context[0]++;fresh.generation++;
 fresh.root=77;fresh.root_boot=88;fresh.sampled_us++;assert(t.receive(fresh,1120001));assert(t.view(1120001).root==77);
 assert(!t.view(1).valid);std::puts("PASS RC9 codec/authentication/client causal expiry/replay/withdrawal; precision UNKNOWN");
}

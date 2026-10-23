// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "rc9.hpp"
#include <cstring>
namespace v12::rc9 {
namespace {
bool zero(const uint8_t* p,size_t n){uint8_t b=0;while(n--)b|=*p++;return !b;}
void header(uint8_t* w,size_t n,Type type,const r2_group_config& g){
 std::memset(w,0,n);w[0]=uint8_t(n-1);w[1]=w[2]=w[3]=255;w[4]='R';w[5]='9';w[6]=1;w[7]=uint8_t(type);r2_group_put(w+8,g.epoch,4);
}
bool header_valid(const uint8_t* w,size_t n,Type type,const r2_group_config& g){
 return w&&r2_group_valid(&g)&&w[0]==n-1&&w[1]==255&&w[2]==255&&w[3]==255&&w[4]=='R'&&w[5]=='9'&&w[6]==1&&w[7]==uint8_t(type)&&r2_group_get(w+8,4)==g.epoch;
}
void control_write(uint8_t* w,const Control& c){
 const uint64_t fields[]={c.sender,c.sender_boot,c.target,c.target_boot,c.challenge,c.nonce,c.next_challenge,c.server_us};
 for(unsigned i=0;i<8;i++)r2_group_put(w+12+8*i,fields[i],8);
 r2_group_put(w+76,c.sequence,4);r2_group_put(w+80,c.lease_us,4);r2_group_put(w+84,c.publication_floor,4);w[88]=c.ready;
}
Control control_read(const uint8_t* w){
 Control c;c.type=Type(w[7]);c.sender=r2_group_get(w+12,8);c.sender_boot=r2_group_get(w+20,8);
 c.target=r2_group_get(w+28,8);c.target_boot=r2_group_get(w+36,8);c.challenge=r2_group_get(w+44,8);
 c.nonce=r2_group_get(w+52,8);c.next_challenge=r2_group_get(w+60,8);c.server_us=r2_group_get(w+68,8);
 c.sequence=uint32_t(r2_group_get(w+76,4));c.lease_us=uint32_t(r2_group_get(w+80,4));c.publication_floor=uint32_t(r2_group_get(w+84,4));c.ready=w[88]!=0;return c;
}
}
bool valid_control(const Control& c){
 if(!c.sender||!c.sender_boot||c.type<Type::Capability||c.type>Type::Response)return false;
 if(c.type==Type::Capability)return !c.target&&!c.target_boot&&!c.challenge&&!c.nonce&&!c.sequence&&!c.lease_us&&!c.server_us&&!c.publication_floor&&!c.ready&&c.next_challenge;
 if(!c.target||c.target==c.sender||!c.target_boot||!c.challenge||!c.nonce||!c.sequence||!c.lease_us||c.lease_us>LeaseUs)return false;
 if(c.type==Type::Request)return !c.next_challenge&&!c.server_us&&!c.publication_floor&&!c.ready;
 return c.next_challenge>c.challenge&&c.server_us&&c.publication_floor;
}
bool valid_time(const Time& p){
 if(!p.sender||!p.sender_boot||!p.publication||!p.sampled_us||p.assurance>2||p.authority>2||p.link_samples>16)return false;
 if(!p.valid)return true; // Explicit withdrawal, including an unresolved origin.
 if((p.authority&&p.assurance!=2)||(p.assurance==2&&(!p.root_boot||!p.emitter_boot||!p.stream)))return false;
 return p.edge_us&&p.edge_us<=p.sampled_us&&p.until_us>p.sampled_us&&p.until_us-p.edge_us<=HoldUs&&
  p.event_ns&&p.root&&p.root!=p.sender&&p.emitter&&p.emitter!=p.sender&&p.source_epoch&&p.generation&&p.corrected_sequence&&
  !zero(p.context,16)&&((p.kind==1&&p.root==p.emitter)||(p.kind==2&&p.root!=p.emitter))&&p.created_ns<=p.event_ns&&p.event_ns<p.expires_ns&&
  p.event_ns/1000<=UINT64_MAX-(p.sampled_us-p.edge_us);
}
bool encode_control(uint8_t* w,const Control& c,const r2_group_config& g){
 if(!w||!r2_group_valid(&g)||!valid_control(c))return false;
 header(w,ControlBytes,c.type,g);control_write(w,c);return !r2_group_mac(w+196,&g,"RC9CTRL1",w,196);
}
bool decode_control(Control& out,const uint8_t* w,size_t n,const r2_group_config& g){
 out={};if(n!=ControlBytes||!w||w[7]<1||w[7]>3||!header_valid(w,n,Type(w[7]),g)||w[88]>1||!zero(w+89,107)||!r2_group_verify(w+196,&g,"RC9CTRL1",w,196))return false;
 const auto c=control_read(w);if(!valid_control(c))return false;out=c;return true;
}
bool encode_capability(uint8_t* w,const Control& c,const r2_group_config& g){
 if(!w||!r2_group_valid(&g)||c.type!=Type::Capability||!valid_control(c))return false;
 header(w,CapabilityBytes,c.type,g);control_write(w,c);return !r2_group_mac(w+128,&g,"RC9CAP01",w,128);
}
bool decode_capability(Control& out,const uint8_t* w,size_t n,const r2_group_config& g){
 out={};if(n!=CapabilityBytes||!header_valid(w,n,Type::Capability,g)||!zero(w+89,39)||!r2_group_verify(w+128,&g,"RC9CAP01",w,128))return false;
 const auto c=control_read(w);if(!valid_control(c))return false;out=c;return true;
}
bool encode_time(uint8_t* w,const Time& p,const r2_group_config& g){
 if(!w||!r2_group_valid(&g)||!valid_time(p))return false;
 header(w,TimeBytes,Type::Time,g);r2_group_put(w+12,p.sender,8);r2_group_put(w+20,p.sender_boot,8);
 r2_group_put(w+28,p.publication,4);w[32]=p.valid;w[33]=p.assurance;w[34]=p.authority;
 const uint64_t fields[]={p.sampled_us,p.edge_us,p.until_us,p.event_ns,p.root,p.root_boot,p.emitter,p.emitter_boot,p.stream};
 for(unsigned i=0;i<9;i++)r2_group_put(w+36+8*i,fields[i],8);
 r2_group_put(w+108,p.source_epoch,4);r2_group_put(w+112,p.generation,4);std::memcpy(w+116,p.context,16);
 r2_group_put(w+132,p.created_ns,8);r2_group_put(w+140,p.expires_ns,8);r2_group_put(w+148,p.link_rms_ns,4);
 w[152]=p.link_samples;w[153]=p.kind;r2_group_put(w+156,UINT32_MAX,4);r2_group_put(w+160,p.corrected_sequence,4);
 return !r2_group_mac(w+174,&g,"RC9TIME1",w,174);
}
bool decode_time(Time& out,const uint8_t* w,size_t n,const r2_group_config& g){
 out={};if(n!=TimeBytes||!header_valid(w,n,Type::Time,g)||w[32]>1||w[35]||!zero(w+154,2)||r2_group_get(w+156,4)!=UINT32_MAX||!zero(w+164,10)||!r2_group_verify(w+174,&g,"RC9TIME1",w,174))return false;
 Time p;p.sender=r2_group_get(w+12,8);p.sender_boot=r2_group_get(w+20,8);p.publication=uint32_t(r2_group_get(w+28,4));p.valid=w[32]!=0;p.assurance=w[33];p.authority=w[34];
 p.sampled_us=r2_group_get(w+36,8);p.edge_us=r2_group_get(w+44,8);p.until_us=r2_group_get(w+52,8);p.event_ns=r2_group_get(w+60,8);
 p.root=r2_group_get(w+68,8);p.root_boot=r2_group_get(w+76,8);p.emitter=r2_group_get(w+84,8);p.emitter_boot=r2_group_get(w+92,8);p.stream=r2_group_get(w+100,8);
 p.source_epoch=uint32_t(r2_group_get(w+108,4));p.generation=uint32_t(r2_group_get(w+112,4));std::memcpy(p.context,w+116,16);
 p.created_ns=r2_group_get(w+132,8);p.expires_ns=r2_group_get(w+140,8);p.link_rms_ns=uint32_t(r2_group_get(w+148,4));p.link_samples=w[152];p.kind=w[153];p.corrected_sequence=uint32_t(r2_group_get(w+160,4));
 if(!valid_time(p))return false;out=p;return true;
}
}

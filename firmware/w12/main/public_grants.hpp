#pragma once
#include "client_lifecycle.hpp"
#include <cstddef>
#include <cstring>
namespace v12 {
// Preparatory exact-190-byte grant layout. Runtime does not admit this draft
// until the common production wire policy, domain and trust keys are frozen.
struct GrantPolicy {uint16_t company;uint8_t version,root_kind,relay_kind,algorithm,allowed_flags;};
constexpr GrantPolicy CandidatePolicy{0xffff,12,129,130,1,1};
constexpr uint8_t RootTimingDomain[]={0x50,0x54,0x4f,0x42,0x31,0x32,0x52,0x54,1};
constexpr uint8_t RelayTimingDomain[]={0x50,0x54,0x4f,0x42,0x31,0x32,0x45,0x54,1};
constexpr uint8_t RootGrantDomain[]={0x50,0x54,0x4f,0x42,0x31,0x32,0x52,0x47,1};
constexpr uint8_t RelayGrantDomain[]={0x50,0x54,0x4f,0x42,0x31,0x32,0x45,0x47,1};
struct RootGrant {
  uint8_t issuer=0,publisher=0,root=0,flags=0;
  uint64_t coordinator_boot=0,root_boot=0,root_train=0,permit_id=0,expiry_ns=0;
  uint32_t source_epoch=0,config_epoch=0;uint16_t duration_ceil100us=0;
  uint8_t key[64]{},signature[64]{},exact[GrantBytes]{};
};
struct RelayGrant {
  uint8_t issuer=0,publisher=0,root=0,flags=0;
  uint64_t coordinator_boot=0,emitter_boot=0,emitter_train=0,root_permit_id=0,id=0,expiry_ns=0;
  uint16_t duration_ceil100us=0;uint8_t key[64]{},signature[64]{};
};
inline uint16_t u16(const uint8_t* p){return uint16_t(p[0])|uint16_t(p[1])<<8;}
inline uint32_t u32(const uint8_t* p){return uint32_t(u16(p))|uint32_t(u16(p+2))<<16;}
inline uint64_t u64(const uint8_t* p){return uint64_t(u32(p))|uint64_t(u32(p+4))<<32;}
inline bool grant_prefix(const uint8_t* b,size_t n,const GrantPolicy& p,uint8_t kind,uint8_t depth){
  return b&&n==GrantBytes&&b[0]==GrantBytes-1&&b[1]==0xff&&u16(b+2)==p.company&&
    b[4]==p.version&&b[5]==kind&&b[6]!=255&&b[7]!=255&&b[8]!=255&&b[9]==depth&&
    b[10]==1&&!(b[10]&~p.allowed_flags)&&b[11]==p.algorithm;
}
inline bool decode_root(const uint8_t* b,size_t n,const GrantPolicy& p,RootGrant& out){
  if(!grant_prefix(b,n,p,p.root_kind,0)||b[7]!=b[8])return false;
  RootGrant r;r.issuer=b[6];r.publisher=b[7];r.root=b[8];r.flags=b[10];
  r.coordinator_boot=u64(b+12);r.root_boot=u64(b+20);r.root_train=u64(b+28);
  r.source_epoch=u32(b+36);r.permit_id=u64(b+40);r.expiry_ns=u64(b+48);
  r.config_epoch=u32(b+56);r.duration_ceil100us=u16(b+60);
  if(!r.coordinator_boot||!r.root_boot||!r.root_train||!r.source_epoch||!r.permit_id||
     !r.expiry_ns||!r.config_epoch||!r.duration_ceil100us||r.duration_ceil100us>30000||
     r.expiry_ns<uint64_t(r.duration_ceil100us)*100000)return false;
  std::memcpy(r.key,b+62,64);std::memcpy(r.signature,b+126,64);std::memcpy(r.exact,b,GrantBytes);
  out=r;return true;
}
inline bool decode_relay(const uint8_t* b,size_t n,const GrantPolicy& p,RelayGrant& out){
  if(!grant_prefix(b,n,p,p.relay_kind,1)||b[7]==b[8])return false;
  RelayGrant r;r.issuer=b[6];r.publisher=b[7];r.root=b[8];r.flags=b[10];
  r.coordinator_boot=u64(b+12);r.emitter_boot=u64(b+20);r.emitter_train=u64(b+28);
  r.root_permit_id=u64(b+36);r.id=u64(b+44);r.expiry_ns=u64(b+52);r.duration_ceil100us=u16(b+60);
  if(!r.coordinator_boot||!r.emitter_boot||!r.emitter_train||!r.root_permit_id||!r.id||
     !r.expiry_ns||!r.duration_ceil100us||r.duration_ceil100us>30000||
     r.expiry_ns<uint64_t(r.duration_ceil100us)*100000)return false;
  std::memcpy(r.key,b+62,64);std::memcpy(r.signature,b+126,64);out=r;return true;
}
inline bool exact_parent(const RootGrant& root,const RelayGrant& relay){
  return root.issuer==relay.issuer&&root.root==relay.root&&
    root.coordinator_boot==relay.coordinator_boot&&root.permit_id==relay.root_permit_id&&
    root.expiry_ns>=relay.expiry_ns&&root.duration_ceil100us>=relay.duration_ceil100us&&
    (relay.flags&~root.flags)==0;
}
enum class GrantState:uint8_t { Missing,AuthenticatedFresh,ColdFreshnessUnknown,Expired,Ambiguous };
struct RootCache {
  struct Entry {RootGrant root{};GrantState state=GrantState::Missing;};
  Entry entries[4]{};
  bool add_authenticated(const RootGrant& root,bool trusted_coarse_utc,uint64_t now_ns){
    Entry* empty=nullptr;
    for(auto& e:entries){
      if(e.state==GrantState::Missing){if(!empty)empty=&e;continue;}
      if(e.root.coordinator_boot==root.coordinator_boot&&e.root.root==root.root&&e.root.permit_id==root.permit_id){
        if(std::memcmp(e.root.exact,root.exact,GrantBytes)){e.state=GrantState::Ambiguous;return false;}
        if(e.state==GrantState::Ambiguous)return false;
        // Repetition changes neither immutable fields nor freshness knowledge.
        if(trusted_coarse_utc&&now_ns>=root.expiry_ns)e.state=GrantState::Expired;
        return e.state==GrantState::AuthenticatedFresh;
      }
    }
    if(!empty)return false;
    empty->root=root;empty->state=!trusted_coarse_utc?GrantState::ColdFreshnessUnknown:
      now_ns>=root.expiry_ns?GrantState::Expired:GrantState::AuthenticatedFresh;
    return empty->state==GrantState::AuthenticatedFresh;
  }
  const RootGrant* relay_parent(const RelayGrant& relay,bool trusted_coarse_utc,uint64_t now_ns){
    if(!trusted_coarse_utc||now_ns>=relay.expiry_ns)return nullptr;
    const RootGrant* found=nullptr;
    for(auto& e:entries){
      if(e.state!=GrantState::AuthenticatedFresh||!exact_parent(e.root,relay))continue;
      if(now_ns>=e.root.expiry_ns){e.state=GrantState::Expired;continue;}
      if(found)return nullptr;
      found=&e.root;
    }
    return found;
  }
};
}

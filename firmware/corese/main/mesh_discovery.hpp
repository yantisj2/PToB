// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstring>
#include "gm7_codec.h"

namespace corese {
// A locator selects an authentication context, never clock permission. The
// group ID/epoch come from the local radio; only the trusted PSK is provisioned.
inline void mesh_address(uint8_t out[6],const r2_group_config& group,uint64_t id){
 uint8_t key[16];r2_group_pair_key(key,&group,id,id);std::memcpy(out,key,6);out[5]|=0xc0;
}
inline bool mesh_locator(r2_group_config& selected,const r2_group_config& trusted,
                         const uint8_t* bytes,unsigned length,uint8_t sid,uint8_t props,
                         uint8_t address_type,const uint8_t address[6],uint64_t self){
 if(!bytes||address_type!=1)return false; // Canonical static random address.
 auto candidate=trusted;uint64_t sender=0;
 if(sid==14&&length>=28&&bytes[1]==0xff&&bytes[2]==0xff&&bytes[3]==0xff&&
    !std::memcmp(bytes+4,"G7V4",4)){
  candidate.id=0;candidate.epoch=0;
  for(unsigned i=16;i<24;++i)candidate.id=(candidate.id<<8)|bytes[i];
  for(unsigned i=24;i<28;++i)candidate.epoch=(candidate.epoch<<8)|bytes[i];
  gm7_packet packet{};
  if(gm7_ad_decode(&packet,bytes,length,&candidate)||packet.kind!=GM7_ANNOUNCE||
     !(packet.flags&GM7_ELIGIBLE))return false;
  const auto& c=packet.body.control;
  if(c.sender!=c.active||c.boot!=c.active_boot)return false;
  sender=c.sender;
 }else if(props&1){
  for(unsigned at=0;at<length;){const unsigned n=bytes[at];if(!n||at+n>=length)return false;
   const auto* p=bytes+at+2;
   if(n==24&&bytes[at+1]==0xff&&p[0]=='G'&&p[1]=='1'&&(p[2]&63)==1){
    candidate.id=r2_group_get(p+3,8);candidate.epoch=uint32_t(r2_group_get(p+11,4));
    sender=r2_group_get(p+15,8);break;
   }at+=n+1;
  }
 }else return false;
 if(!sender||sender==self||!r2_group_valid(&candidate))return false;
 uint8_t expected[6];mesh_address(expected,candidate,sender);
 if(std::memcmp(expected,address,6))return false;
 selected=candidate;return true;
}
}

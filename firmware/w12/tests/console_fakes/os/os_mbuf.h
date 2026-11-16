// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstring>
struct os_mbuf {const uint8_t* data=nullptr;unsigned length=0;os_mbuf* next=nullptr;};
inline unsigned OS_MBUF_PKTLEN(const os_mbuf* m){unsigned n=0;for(;m;m=m->next)n+=m->length;return n;}
inline int os_mbuf_copydata(const os_mbuf* m,int offset,int length,void* destination){
 auto* out=static_cast<uint8_t*>(destination);
 for(;m&&length;m=m->next){if(offset>=int(m->length)){offset-=m->length;continue;}
  const auto count=unsigned(length)<m->length-unsigned(offset)?unsigned(length):m->length-unsigned(offset);
  std::memcpy(out,m->data+offset,count);out+=count;length-=count;offset=0;}
 return length?1:0;
}

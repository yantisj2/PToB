// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "fine_receiver.hpp"
#include <cassert>
#include <cstdio>
#include <limits>
using namespace w12::fine;
static void same(const Edge& a,const Edge& b){
 assert(a.raw80==b.raw80&&a.tick20==b.tick20&&a.seen_us==b.seen_us&&
   a.number==b.number&&a.raw32==b.raw32&&a.incarnation==b.incarnation&&
   a.kind==b.kind&&a.polarity==b.polarity&&a.valid==b.valid);
}
int main(){
 static_assert(Receiver::Capacity==256&&Receiver::Fences==16);
 static_assert(sizeof(Receiver)<=7896);
 for(unsigned n=0;n<1024;++n){
  Receiver r;
  const uint64_t origin=(UINT64_C(1)<<42)+n*UINT64_C(0x100000001);
  const auto center=origin+800000;
  const auto incarnation=UINT32_MAX-n;
  r.begin(origin,origin+1,incarnation);
  // Tick and availability are retained verbatim, even at full-width extremes;
  // they are not inferred from the raw coordinate or narrowed for storage.
  const Edge rise{center-10000,UINT64_MAX-n,n,1,uint32_t(center-10000),incarnation,0,0,true};
  const Edge fall{center+125440,n,UINT64_MAX-n,2,uint32_t(center+125440),incarnation,0,1,true};
  r.observe(rise);r.observe(fall);
  Receiver::Match m;
  assert(r.claim(Counter::Base20+200000,0,0,191,0,1,1,center+248181,0,m)==Receiver::Result::Ready);
  same(m.rise,rise);same(m.fall,fall);
  uint64_t coordinate=0;
  assert(r.coordinate(m.fall.raw80,125440,coordinate)&&coordinate==Counter::Base20+200000);
  // Reset must remove both retained metadata and sealed interval ownership.
  r.begin(origin,origin+1,incarnation);
  assert(r.claim(Counter::Base20+200000,0,0,191,0,1,1,center+248181,0,m)==Receiver::Result::Missing);
 }
 std::printf("PASS exact full-width callback metadata, reset and fixed256 capacity; bytes=%zu\n",sizeof(Receiver));
}

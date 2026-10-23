// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#include "fine_receiver.hpp"
#include <cassert>
#include <cstdio>
using n30::fine::Receiver;using n30::fine::Edge;using n30::fine::Counter;
constexpr uint64_t Origin=4000,Center=Origin+800000,Upper=Center+248080;
static uint64_t descriptor(){return Counter::Base20+(Center-Origin)/4;}
static void add(Receiver& r,uint32_t n,uint64_t raw,uint8_t channel,uint8_t polarity){r.observe({raw,0,0,n,uint32_t(raw),1,channel,polarity,true});}
static Receiver base(){Receiver r;r.begin(Origin,Origin+20,1);add(r,1,Center-10000,0,0);add(r,2,Center+125440,0,1);return r;}
static auto claim(Receiver& r,Receiver::Match& out,uint32_t epoch=1,uint32_t seq=1,uint64_t now=Upper+100,uint32_t pending=0){return r.claim(descriptor(),0,0,191,0,epoch,seq,now,pending,out);}
int main(){
 Receiver::Match m{};auto r=base();assert(claim(r,m,1,1,Upper)==Receiver::Result::Pending);assert(claim(r,m,1,1,Upper+100,1)==Receiver::Result::Pending);
 assert(claim(r,m)==Receiver::Result::Ready&&m.fall.raw80==Center+125440);uint64_t tick=0;
 assert(r.coordinate(m.fall.raw80,125440,tick)&&tick==descriptor());
 assert(r.coordinate(Origin+5,0,tick)&&tick==Counter::Base20+1);assert(r.coordinate(Origin+6,0,tick)&&tick==Counter::Base20+2);
 assert(!r.coordinate(Origin+5,6,tick));assert(claim(r,m)==Receiver::Result::Unsupported);
 uint32_t guard=0;assert(Receiver::lifetime_guard(100,120,5000,guard)&&guard==6025);
 assert(Receiver::lifetime_guard(120,100,5000,guard)&&guard==6025);
 assert(!Receiver::lifetime_guard(0,UINT64_MAX,5000,guard));assert(!Receiver::lifetime_guard(100,100,UINT32_MAX-1,guard));
 // A callback serviced after sealing cannot silently alter an accepted map.
 add(r,3,Center+20000,1,0);assert(r.status().fault&&r.status().late==1);assert(!r.coordinate(Center,0,tick));
 r=base();add(r,3,Center-20000,1,0); // Cross-channel raw time need not follow sequence.
 assert(claim(r,m)==Receiver::Result::Ready);
 r=base();add(r,3,Center+130000,0,1);assert(claim(r,m)==Receiver::Result::Ambiguous);
 r=base();add(r,4,Upper+50,1,0);assert(claim(r,m)==Receiver::Result::Fault);
 r=base();add(r,3,Center+120000,0,0);assert(claim(r,m)==Receiver::Result::Fault); // same-channel reversal
 r=base();assert(r.claim(descriptor(),0,0,190,0,1,1,Upper+100,0,m)==Receiver::Result::Unsupported);
 assert(r.claim(descriptor(),0,1,191,0,1,1,Upper+100,0,m)==Receiver::Result::Unsupported);
 assert(r.claim(descriptor(),0,0,191,1,1,1,Upper+100,0,m)==Receiver::Result::Unsupported);
 assert(claim(r,m,0)==Receiver::Result::Unsupported);
 r.begin(Origin,Origin+20,1);add(r,1,Center+125440,0,1);assert(claim(r,m)==Receiver::Result::Missing);
 // Ring wrap cannot turn overwritten callback history into a clean window.
 r=base();for(unsigned n=3;n<270;++n)add(r,n,Center+130000+n,1,uint8_t(n&1));assert(claim(r,m)==Receiver::Result::Missing);
 // Stale incarnation and invalid hardware extension poison the whole owner.
 r=base();Edge e{Upper+50,0,0,3,uint32_t(Upper+50),2,1,0,true};r.observe(e);assert(r.status().fault);
 // Expired event age and pre-capture history do not acquire.
 r=base();assert(claim(r,m,1,1,Upper+Receiver::EventAge80+1)==Receiver::Result::Missing);
 r.begin(Origin,Center,1);add(r,1,Center+1,0,0);add(r,2,Center+125440,0,1);assert(claim(r,m)==Receiver::Result::Missing);
 std::printf("PASS continuous history, qualified class, causal pending, rounding, missing/ambiguous/replay/late faults; bytes=%zu\n",sizeof(Receiver));
}

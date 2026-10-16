#pragma once
#include "native_transport.hpp"
namespace v12 {
// Main-task discovery presentation only. All ingress/admission counters and
// scope/link/PA/physical-capture records are preserved. Repeated unsigned
// discovery bytes are represented by explicit totals and one sample/peer/s.
struct G1RecordBudget {
  struct Peer {uint64_t identity=0,last=0;};
  Peer peers[8]{};
  uint32_t discovery_total=0,discovery_sampled=0,discovery_aggregated=0;
  static void inc(uint32_t& n){if(n!=UINT32_MAX)++n;}
  bool emit(const Report& r){
    if(r.kind!=5)return true;
    inc(discovery_total);uint64_t id=0;
    for(unsigned at=0;at<r.length;){const unsigned n=r.bytes[at];
      if(!n||at+n>=r.length)break;
      if(n==24&&r.bytes[at+1]==0xff&&r.bytes[at+2]=='G'&&r.bytes[at+3]=='1'){
        for(unsigned i=0;i<8;i++)id|=uint64_t(r.bytes[at+17+i])<<(8*i);
        break;
      }at+=n+1;
    }
    if(!id){inc(discovery_sampled);return true;}
    unsigned slot=0;
    for(unsigned i=0;i<8;i++){
      if(peers[i].identity==id){
        if(r.callback_us<peers[i].last||r.callback_us-peers[i].last<1000000){inc(discovery_aggregated);return false;}
        slot=i;break;
      }
      if(peers[i].last<peers[slot].last)slot=i;
    }
    peers[slot]={id,r.callback_us};inc(discovery_sampled);return true;
  }
};
}

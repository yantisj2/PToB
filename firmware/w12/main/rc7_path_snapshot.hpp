#pragma once
#include <cstdint>
extern "C" {
#include "r2/rc7/protocol/rc_dynamic.h"
}
namespace v12::rc7_path_snapshot {
constexpr unsigned Bytes=89,HexChars=178,Capacity=179;
inline void clear(char (&out)[Capacity]){
  for(unsigned i=0;i<HexChars;++i)out[i]='0';
  out[HexChars]=0;
}
namespace detail {
inline char digit(unsigned n){return char(n<10?'0'+n:'a'+n-10);}
inline void put(char (&out)[Capacity],unsigned at,uint64_t value,unsigned bytes){
  for(unsigned i=0;i<bytes;++i){const auto b=unsigned((value>>(8*i))&255u);
    out[2*(at+i)]=digit(b>>4);out[2*(at+i)+1]=digit(b&15);}
}
}
// Worker snapshot serialization only: no allocation, crypto, RF or authority.
// A null/unbound/invalid path emits the canonical89 zero bytes. Serialize
// fields explicitly so host ABI padding never becomes diagnostic wire data.
inline void encode(char (&out)[Capacity],const rc7_path* bound_path){
  clear(out);
  if(!bound_path||!rc7_path_valid(bound_path))return;
  const auto& p=*bound_path;
  detail::put(out,0,p.source_epoch,4);detail::put(out,4,p.depth,1);
  for(unsigned i=0;i<3;++i){const auto& n=p.node[i];const auto at=5+28*i;
    detail::put(out,at,n.id,8);detail::put(out,at+8,n.boot,8);
    detail::put(out,at+16,n.train,4);detail::put(out,at+20,n.epoch,4);
    detail::put(out,at+24,n.slot,1);detail::put(out,at+25,n.role,1);
    // Canonical reserved bytes26/27 and unused nodes remain zero.
  }
}
static_assert(5+3*28==Bytes&&2*Bytes==HexChars&&HexChars+1==Capacity);
}

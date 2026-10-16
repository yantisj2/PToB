#include "diagnostic_codec.hpp"
#include <cassert>
#include <cstdio>
#include <initializer_list>
int main(){uint8_t out[190],mac[]={0x44,0xb1,0x76,0xae,0x20,0x44};
  const char* build="bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
  const char* source="aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
  for(unsigned n:{158u,182u,190u}){assert(v12::diagnostic_pack(out,n,n==190?0xf1:0xf0,5,mac,9,build,source,1));
    assert(out[0]==n-1&&out[13]==5&&!std::memcmp(out+14,mac,6)&&out[20]==9&&out[28]==0xbb&&out[60]==0xaa&&out[80]==1&&out[84]==n);}
  assert(!v12::diagnostic_pack(out,190,0xf1,5,mac,0,build,source,1));
  const char name[]="T096 V12 full PREPARATION";out[0]=sizeof(name);out[1]=9;std::memcpy(out+2,name,sizeof(name)-1);
  assert(v12::t096_initial_discovery(out,sizeof(name)+1,12));assert(!v12::t096_initial_discovery(out,sizeof(name)+1,11));
  out[5]^=1;assert(!v12::t096_initial_discovery(out,sizeof(name)+1,12));
  std::puts("PASS diagnostic codec: distinct kinds, full factory/build/source/boot/run attribution, exact finite T096 name+SID acquisition; no authority");}

#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
namespace v12 {
// Exact AD walk: reject truncation, permit R2's flags-only connection AD.
// Discovery is a route hint. It never authenticates or enables a source.
inline bool r2_connection_ad(const uint8_t* ad,size_t n){
  if(!ad||!n)return false;
  bool flags=false;
  for(size_t at=0;at<n;){const unsigned len=ad[at];
    if(!len||len>n-at-1)return false;
    if(ad[at+1]==1&&len==2&&(ad[at+2]&6)==6)flags=true;
    at+=len+1;
  }
  return flags;
}
inline bool r2_same_address(uint8_t type,const uint8_t addr[6],const uint8_t expected[6]){
  return type==1&&addr&&expected&&!std::memcmp(addr,expected,6);
}
}

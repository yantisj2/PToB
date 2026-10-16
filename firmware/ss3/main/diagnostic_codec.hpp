#pragma once
#include "client_lifecycle.hpp"
#include <cstring>
namespace v12 {
// INITIAL diagnostic bytes only. No grants, source flags or corrected time.
inline bool hex_bytes(const char* input,uint8_t* out,unsigned n){
  auto nibble=[](char c)->int{return c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:-1;};
  for(unsigned i=0;i<n;++i){const int a=nibble(input[2*i]),b=nibble(input[2*i+1]);if(a<0||b<0)return false;out[i]=uint8_t((a<<4)|b);}return true;
}
inline bool diagnostic_pack(uint8_t* out,unsigned n,uint8_t kind,uint8_t wire,const uint8_t mac[6],
                            uint64_t session,const char* build,const char* source,uint32_t run){
  if(!out||!mac||!build||!source||!session||!run||(n!=158&&n!=182&&n!=190)||(kind!=0xf0&&kind!=0xf1))return false;
  for(unsigned i=0;i<n;++i)out[i]=uint8_t(i^(kind==0xf1?0xa5:0x5a));
  out[0]=uint8_t(n-1);out[1]=out[2]=out[3]=0xff;out[4]=12;out[5]=kind;
  std::memcpy(out+6,"V12DIAG",7);out[13]=wire;std::memcpy(out+14,mac,6);
  for(unsigned i=0;i<8;++i)out[20+i]=uint8_t(session>>(8*i));
  if(!hex_bytes(build,out+28,32)||!hex_bytes(source,out+60,20))return false;
  for(unsigned i=0;i<4;++i){out[80+i]=uint8_t(run>>(8*i));}
  out[84]=uint8_t(n);out[85]=uint8_t(n>>8);return true;
}
inline bool t096_initial_discovery(const uint8_t* data,unsigned n,uint8_t sid){
  constexpr char name[]="T096 V12 full PREPARATION";
  return data&&sid==12&&n==sizeof(name)+1&&data[0]==sizeof(name)&&data[1]==9&&
    !std::memcmp(data+2,name,sizeof(name)-1);
}
}

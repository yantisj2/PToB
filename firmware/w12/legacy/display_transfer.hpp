#pragma once
#include <cstdint>
#include <cstring>

// Only changed page spans are sent. After any failed transfer the next attempt
// restores the entire framebuffer; the shadow never claims a partial success.
template<class Send> bool display_flush(const uint8_t* frame,uint8_t* shadow,bool& valid,Send send){
 for(unsigned page=0;page<8;++page){
  unsigned first=0,last=127;
  if(valid){
   while(first<128 && frame[page*128+first]==shadow[page*128+first])++first;
   if(first==128)continue;
   while(last>first && frame[page*128+last]==shadow[page*128+last])--last;
  }
  if(!send(page,first,frame+128*page+first,last-first+1)){valid=false;return false;}
 }
 std::memcpy(shadow,frame,1024);valid=true;return true;
}

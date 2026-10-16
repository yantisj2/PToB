#include "periodic_tx_chm_guard.hpp"
#include <cassert>
#include <cstdio>
namespace rom=v12::selected_tx::rom;
bool load(uintptr_t address,uint32_t& value){
 assert(!(address&3));for(const auto& s:rom::ChmSpans)
  if(address>=s.address&&address<s.address+4*s.words){value=rom::ChmWords[s.first+(address-s.address)/4];return true;}
 return false;
}
int main(){
 assert(rom::match_chm(load));unsigned cases=1,words=0;
 for(const auto& span:rom::ChmSpans)for(unsigned i=0;i<span.words;++i){
  const auto bad=span.address+4*i;
  auto mismatch=[&](uintptr_t a,uint32_t& v){if(!load(a,v))return false;if(a==bad)v^=1;return true;};
  auto unavailable=[&](uintptr_t a,uint32_t& v){return a!=bad&&load(a,v);};
  assert(!rom::match_chm(mismatch)&&!rom::match_chm(unavailable));cases+=2;++words;
 }
 assert(words==211);printf("{\"status\":\"PASSED\",\"additional_words\":%u,\"focused_guard_cases\":%u,\"actual_ROM_read\":false}\n",words,cases);
}

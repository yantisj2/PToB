#pragma once
#include <cstdint>
#include <cstddef>
namespace v12::selected_tx::rom {
// RF-off worker only; append to existing base/radio/capacity guards.
constexpr uintptr_t PackedMapAddress=0x40021dacu;
constexpr unsigned PackedMapWords=23;
inline constexpr uint32_t PackedMapExpected[]={0x57032200u,0x60cec661u,0x02671122u,0xb104680fu,0x2668fbefu,0xa235a6c2u,0x06e000a0u,0xa2046800u,0x266200a4u,0x0006e02fu,0x04880168u,0xc0905660u,0x56501165u,0x1155f0c0u,0x8226c552u,0x6a5a2f28u,0xe000a4a2u,0xaa5a0008u,0x50001a52u,0x25209450u,0xf4202020u,0x220020c0u,0x050c0056u};
template<class Load32> inline bool match_packed_map(Load32&& load){
 for(unsigned i=0;i<PackedMapWords;++i){uint32_t value=0;
  if(!load(PackedMapAddress+4*i,value)||value!=PackedMapExpected[i])return false;
 }return true;
}
}

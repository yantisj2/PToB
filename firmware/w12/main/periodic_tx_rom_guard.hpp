#pragma once
#include <cstdint>
#include <cstddef>

// Startup worker only. Never access this const flash table in a radio ISR.
// Caller supplies explicit aligned 32-bit ROM reads; no ROM byte reads.
// Original28 spans remain first; five cookie-handoff proof spans follow.
namespace v12::selected_tx::rom {
constexpr unsigned SpanCount = 33, WordCount = 208, ReadBytes = 832;
constexpr char BaseManifestSHA256[] = "edd7ef99904333b2866d91272ade5a081ccf9d0e49f8064874e398e3a5537660";
constexpr char ManifestSHA256[] = "40412e8ab933d971baf53981f1d5b6d80434826e2ee5bbf27e9026d5fa554a64";
struct Span { uintptr_t address; uint16_t first; uint8_t words; };
inline constexpr Span Spans[SpanCount] = {
  {0x40000570u, 0, 2},
  {0x40004574u, 2, 3},
  {0x40004580u, 5, 3},
  {0x40004568u, 8, 3},
  {0x40004508u, 11, 3},
  {0x4000452cu, 14, 3},
  {0x40004538u, 17, 3},
  {0x400045e0u, 20, 3},
  {0x4000455cu, 23, 3},
  {0x40003644u, 26, 3},
  {0x40021f9cu, 29, 12},
  {0x40021c78u, 41, 6},
  {0x40021d00u, 47, 14},
  {0x40022058u, 61, 8},
  {0x400220dcu, 69, 5},
  {0x400215b8u, 74, 8},
  {0x400215f4u, 82, 7},
  {0x40021188u, 89, 8},
  {0x4002142cu, 97, 9},
  {0x40021470u, 106, 13},
  {0x40021820u, 119, 5},
  {0x400228f0u, 124, 8},
  {0x40021ac0u, 132, 10},
  {0x4000a360u, 142, 9},
  {0x4000a418u, 151, 7},
  {0x4001abb4u, 158, 4},
  {0x400216ccu, 162, 4},
  {0x40021700u, 166, 8},
  {0x40021bd0u, 174, 16},
  {0x4002d8e8u, 190, 6},
  {0x4002d5f0u, 196, 6},
  {0x40004fa0u, 202, 3},
  {0x40004f88u, 205, 3},
};
inline constexpr uint32_t Expected[WordCount] = {
  0x00000009u, 0x00000000u, 0x40021f9cu, 0xa0ffff91u, 0x00000009u, 0x40021c78u,
  0xa0ffff91u, 0x00000009u, 0x40021158u, 0xa0ffff91u, 0x00000009u, 0x400215b8u,
  0xa0ffff91u, 0x00000009u, 0x400217dcu, 0xa0ffff91u, 0x00000009u, 0x400228f0u,
  0xa0ffff91u, 0x00000009u, 0x40022040u, 0xa0ffff91u, 0x00000009u, 0x40021ab0u,
  0xa0ffff91u, 0x00000009u, 0x4000a360u, 0xa0ffff91u, 0x00000009u, 0x40004136u,
  0x02bd7440u, 0x142603adu, 0x26748c0au, 0x44263b24u, 0x0008c616u, 0xcd93ca81u,
  0xa0088804u, 0x288274a0u, 0x0008e0bcu, 0x0000f01du, 0x3093c541u, 0x31006136u,
  0x2020fcefu, 0xa0323074u, 0x38744040u, 0x16114903u, 0x934124d3u, 0x6191e741u,
  0x0428cbc1u, 0x2220a660u, 0x55502f22u, 0x0002e0f0u, 0x581125f0u, 0x528a2a04u,
  0x06ad2f25u, 0xe0026182u, 0xa2aa0005u, 0x88001a52u, 0xe4505021u, 0x520020c0u,
  0x04580058u, 0xbd911371u, 0xa2073805u, 0x233260a0u, 0x0003e048u, 0x0a3d06a9u,
  0xbd7ada16u, 0x60a0c205u, 0x52214152u, 0xc2031922u, 0x532205a0u, 0x02192228u,
  0x2200a0b2u, 0xc3a22753u, 0x6100a136u, 0x2020fe9fu, 0x1132e074u, 0x363a8139u,
  0x42002372u, 0x34405207u, 0xa04440b0u, 0x401154c0u, 0x42271792u, 0x51922557u,
  0x28179209u, 0x47a2040cu, 0x11674257u, 0x92085192u, 0x055810c1u, 0x03387188u,
  0xf0f04880u, 0x23321144u, 0x4a41492fu, 0xe002ad4au, 0x41980003u, 0xaa746060u,
  0x001a22a9u, 0x5942531bu, 0x98585a00u, 0x745050b1u, 0x5580047cu, 0x0f094711u,
  0x4db10648u, 0xc22448feu, 0xa0a226a1u, 0x0004e000u, 0xc0192192u, 0x5a520020u,
  0x82045800u, 0x25520019u, 0x8906ad2fu, 0x0005e0c1u, 0xc1884198u, 0xa5aa594bu,
  0x820020c0u, 0x2182005au, 0x52819819u, 0x06ad0018u, 0x5852595au, 0x3295af21u,
  0x02382858u, 0x325208a2u, 0x5842b123u, 0x0003e027u, 0x51004136u, 0x20209131u,
  0x00258274u, 0x82743030u, 0x40400528u, 0x0008e0f4u, 0x80f9cc81u, 0x08a8a082u,
  0x9065502fu, 0x7200a4a2u, 0x03e02612u, 0x1136c000u, 0xf0c03360u, 0xc3621133u,
  0x61a6aa50u, 0x20c09500u, 0x005a7200u, 0xa2200678u, 0x20004136u, 0x3a20f420u,
  0x37243c41u, 0x4b4115b4u, 0xfff6c1f0u, 0xa0d20448u, 0x3034489fu, 0xa22020b3u,
  0x0004e020u, 0x88e0849au, 0x0020c011u, 0xa3310848u, 0x103430ffu, 0xe0ffa341u,
  0x33401133u, 0x1d232a20u, 0x5a1155f0u, 0x001a72aau, 0x460267f7u, 0x1342ff72u,
  0xc44206a8u, 0x2f2ad234u, 0xa1c991b9u, 0xe000a4a2u, 0xf0f05330u, 0xa4a21155u,
  0x0009e000u, 0x5014d552u, 0xa4aaf450u, 0x520020c0u, 0x090c005au, 0x00002446u,
  0x32005872u, 0x4152bb24u, 0x58415918u, 0x59013942u, 0x16025231u, 0x41521238u,
  0x52f50c14u, 0x11391741u, 0x030c150cu, 0x32154132u, 0x41521641u, 0x1c41321au,
  0x42194132u, 0x213906d4u, 0xad322432u, 0x0003e001u, 0x78fea481u, 0x1133c042u,
  0x1268383au, 0x02782379u, 0x13790369u, 0x4372170cu, 0x88947a5fu, 0x62489c19u,
  0x050cfcc6u, 0x29b81c0cu, 0xc56009a8u, 0x0008e093u, 0x4002d80cu, 0xa0ffff91u,
  0x00000009u, 0x4002d58cu, 0xa0ffff91u, 0x00000009u,
};
enum class Failure : uint8_t { None, Address, Read, Mismatch };
struct Result {
  uintptr_t address = 0;
  uint32_t expected = 0, actual = 0;
  uint16_t words_checked = 0;
  uint8_t span = 255, word = 255;
  Failure failure = Failure::None;
};
constexpr bool table_valid() {
  unsigned count = 0;
  for (const auto& s : Spans) {
    if ((s.address & 3) || s.address < 0x40000000 || !s.words ||
        s.address > 0x40060000 - 4 * unsigned(s.words) ||
        s.first != count || unsigned(s.first) + s.words > WordCount) return false;
    count += s.words;
  }
  return count == WordCount && count * 4 == ReadBytes;
}
static_assert(table_valid());
// load32(address,value) returns bool; it must load exactly one aligned word.
// Optional output is bounded failure metadata, never a ROM dump.
template<class Load32>
inline bool match(Load32&& load32, Result* result = nullptr) {
  Result r{};
  for (unsigned i = 0; i < SpanCount; ++i) {
    const Span s = Spans[i];
    for (unsigned j = 0; j < s.words; ++j) {
      r.address = s.address + 4 * j;
      r.expected = Expected[s.first + j];
      r.actual = 0;
      r.span = uint8_t(i); r.word = uint8_t(j);
      if ((r.address & 3) || r.address < 0x40000000 || r.address > 0x4005fffc)
        r.failure = Failure::Address;
      else if (!load32(r.address, r.actual)) r.failure = Failure::Read;
      else if (r.actual != r.expected) r.failure = Failure::Mismatch;
      if (r.failure != Failure::None) {
        if (result) *result = r;
        return false;
      }
      ++r.words_checked;
    }
  }
  if (result) *result = r;
  return true;
}
// Lab estimate startup adds these52 finite aligned words after match(), plus
// the separately pinned23-word packed CS+38 writer guard.
// The original33-span/208-word table and order above remain unchanged.
constexpr unsigned RadioWordCount=52, EstimatedWordCount=283, EstimatedReadBytes=1132;
constexpr char EstimatedManifestSHA256[]="231cdddf802392ac563bf41c69a14aca3c2c276bb9194539be5af73a6c83622e";
inline constexpr uint32_t RadioExpected[RadioWordCount]={
  0x30032200u, 0x802f0392u, 0x29201122u, 0x00279220u, 0x9200a4a2u, 0x09e02f29u,
  0x22c59200u, 0x20c0a9aau, 0x005a2200u, 0x92320322u, 0x22803103u, 0x20292011u,
  0xa2002792u, 0x299200a4u, 0x0009e02fu, 0xaa24c592u, 0x0020c0a9u, 0x22005a22u,
  0x03920027u, 0x2f222233u, 0x9200a4a2u, 0x02e01061u, 0x10219200u, 0xaa26c522u,
  0x0020c0a2u, 0x91005a92u, 0xb440924eu, 0x00292220u, 0x2228c3a2u, 0x02e08822u,
  0x92002100u, 0x22a20228u, 0x000ae022u, 0x92290322u, 0x22802803u, 0x20292011u,
  0x981653a2u, 0x75a0a007u, 0x922e43a2u, 0xa4a22f29u, 0x0009e000u, 0xa9aa95cbu,
  0x220020c0u, 0x0322005au, 0x2a03922bu, 0x20112280u, 0x07982029u, 0x9200a4a2u,
  0x09e02f29u, 0x0ec59200u, 0x20c0a9aau, 0x005a2200u,
};
template<class Load32>
inline bool match_radio(Load32&& load32){
  for(unsigned i=0;i<RadioWordCount;++i){
    uint32_t value=0;
    if(!load32(0x4002253cu+4*i,value)||value!=RadioExpected[i])return false;
  }
  return true;
}
// Config10 only: exact ten-pointer PA array clear, preserving all prior guards.
constexpr unsigned CapacityWordCount=9, ServerEstimatedWordCount=292, ServerEstimatedReadBytes=1168;
constexpr char CapacityManifestSHA256[]="cd38a70f592306d5989d41f10b06c1fa88f27d71c20c55a6afcf5804cc594e7e";
inline constexpr uint32_t CapacityExpected[CapacityWordCount]={
  0x08e04428u, 0x20220b00u, 0x82007420u, 0xfdf8d623u, 0x2cfc03a1u, 0x650b0c8cu, 0xf01d3509u, 0x3ff1a0a1u, 0x3fcefc58u
};
template<class Load32>
inline bool match_capacity10(Load32&& load32){
  for(unsigned i=0;i<CapacityWordCount;++i){
    const uintptr_t address=i<8?0x4002201cu+4*i:0x40021038u;
    uint32_t value=0;
    if(!load32(address,value)||value!=CapacityExpected[i])return false;
  }
  return true;
}
}

#pragma once
#include <cstdint>
#include "periodic_tx_selected_model.hpp"

// Explicitly untested lab estimates. No function in this file produces an
// actual RF timestamp, measured channel, precision bound or source authority.
namespace v12::tx_estimate {
constexpr uint32_t UnknownUncertainty=UINT32_MAX, ClockMask=0x0fffffff;
constexpr unsigned Banks=4;
enum class Provenance:uint8_t { None, ProgrammedHalfSlotAndCsa2=1, ProgrammedHalfSlotMean4AndCsa2=2 };
inline uint32_t le32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline uint64_t le64(const uint8_t* p){return le32(p)|(uint64_t(le32(p+4))<<32);}
inline bool validate(const uint8_t* p,unsigned n){
  if(!p||n!=190||p[0]!=189||p[1]!=255||p[2]!=255||p[3]!=255||
     p[4]!='R'||p[5]!='C'||p[6]!=7||p[7]>2||p[172]!=5||p[173]||
     !le32(p+8)||!le64(p+12)||!le64(p+20)||!le32(p+28)||
     !le64(p+88)||p[96]>2||p[97]>3||!le64(p+98)||
     !le32(p+106)||!le32(p+110)||!le32(p+114))return false;
  // Native relay owns cryptographic/path/source checks. This parser only
  // recognizes the exact immutable wire7/profile5 bytes prepared by that owner.
  return true;
}
inline bool same_scope(const uint8_t* a,const uint8_t* b){
  return le64(a+12)==le64(b+12)&&le64(a+20)==le64(b+20)&&
    le32(a+28)==le32(b+28)&&le64(a+88)==le64(b+88)&&
    a[96]==b[96]&&a[97]==b[97]&&le64(a+98)==le64(b+98)&&
    le32(a+106)==le32(b+106)&&le32(a+114)==le32(b+114);
}
inline bool equal190(const uint8_t* a,const uint8_t* b){
  for(unsigned i=0;i<190;++i){if(a[i]!=b[i])return false;}
  return true;
}
#define V12_EST_INLINE inline __attribute__((always_inline))
V12_EST_INLINE void publish_barrier(){
#ifdef __XTENSA__
  asm volatile("memw":::"memory");
#else
  asm volatile("":::"memory");
#endif
}
// CPU0 single-worker / CPU0 ISR ownership. Invalidate before inspecting refs:
// an interrupt may pin the old bytes before invalidation, never after it.
// Current and latest prepared banks cannot be reclaimed. One distinct update
// may await observation; BUSY preserves the caller's previous prepared bytes.
struct BankPool {
  uint8_t bytes[Banks][190]{};
  volatile uint8_t valid[Banks]{},refs[Banks]{};
  volatile uint8_t current=255,prepared=255;
  void reset(){for(unsigned j=0;j<Banks;++j){valid[j]=0;refs[j]=0;}current=255;prepared=255;}
  bool prepare(const uint8_t* p,unsigned n){
    if(!validate(p,n))return false;
    const auto previous=prepared;
    if(previous<Banks){
      if(equal190(bytes[previous],p))return true;
      if(previous!=current||!same_scope(bytes[previous],p)||
         le32(p+110)<=le32(bytes[previous]+110))return false;
    }
    for(unsigned j=0;j<Banks;++j){
      if(j==current||j==prepared)continue;
      valid[j]=0;publish_barrier();
      if(refs[j]){valid[j]=1;continue;}
      for(unsigned i=0;i<190;++i)bytes[j][i]=p[i];
      publish_barrier();valid[j]=1;publish_barrier();prepared=uint8_t(j);return true;
    }
    return false;
  }
  V12_EST_INLINE void expectations(selected_tx::KnownExpected* out)const{
    for(unsigned j=0;j<Banks;++j){out[j].bytes=bytes[j];out[j].length=190;out[j].validated=valid[j]!=0;}
  }
  V12_EST_INLINE bool pin(uint8_t slot){
    if(slot>=Banks||!valid[slot]||refs[slot]>=4)return false;
    refs[slot]=uint8_t(refs[slot]+1);current=slot;return true;
  }
  bool verify(uint8_t slot,const selected_tx::Selection& s)const{
    return slot<Banks&&valid[slot]&&refs[slot]&&selected_tx::payload_shape(s)&&
      validate(bytes[slot],190)&&equal190(s.data,bytes[slot]);
  }
  void release(uint8_t slot){if(slot<Banks&&refs[slot])refs[slot]=uint8_t(refs[slot]-1);}
};
#undef V12_EST_INLINE

// Core CSA#2, matching pinned NimBLE ble_ll_utils_dci_csa2. PA always uses
// CSA#2. Input CS map includes only bits0..36; refuse malformed/empty maps.
inline uint16_t perm(uint16_t p){
  p=uint16_t(((p&0xaaaa)>>1)|((p&0x5555)<<1));
  p=uint16_t(((p&0xcccc)>>2)|((p&0x3333)<<2));
  return uint16_t(((p&0xf0f0)>>4)|((p&0x0f0f)<<4));
}
inline bool channel(const selected_tx::Radio& r,uint16_t event,uint8_t& out){
  if(!r.access_address||(r.map[2]&~uint16_t(31)))return false;
  unsigned used=0;
  for(unsigned c=0;c<37;++c)if(r.map[c/16]&(1u<<(c%16)))++used;
  if(used<2)return false;
  const uint16_t id=uint16_t(r.access_address)^uint16_t(r.access_address>>16);
  uint16_t p=event^id;
  for(unsigned i=0;i<3;++i)p=uint16_t(17u*perm(p)+id);
  p^=id;const unsigned unmapped=p%37;
  if(r.map[unmapped/16]&(1u<<(unmapped%16))){out=uint8_t(unmapped);return true;}
  unsigned remap=(uint32_t(used)*p)>>16;
  for(unsigned c=0;c<37;++c)if(r.map[c/16]&(1u<<(c%16))){
    if(!remap){out=uint8_t(c);return true;}--remap;
  }
  return false;
}
struct ClockAnchor {uint64_t before=0,after=0;uint32_t half_slots=0,half_us=0;};
inline bool programmed_tick(const ClockAnchor& a,uint32_t cookie,uint64_t callback20,uint64_t& out){
  if(!a.before||a.after<a.before||a.after-a.before>400||a.half_slots>ClockMask||
     a.half_us>624||cookie>ClockMask||!callback20)return false;
  const uint64_t mid=a.before+(a.after-a.before)/2;
  // This is a finite association/freshness refusal, never an uncertainty bound.
  if(mid<callback20||mid-callback20>5000000)return false;
  const uint32_t wrapped=(cookie-a.half_slots)&ClockMask;
  const int64_t slots=wrapped&(1u<<27)?int64_t(wrapped)-(1LL<<28):wrapped;
  if(slots < -1600 || slots > 1600)return false;
  const int64_t delta=slots*6250-int64_t(a.half_us)*10;
  // The continuous counter is unsigned64; do not cast an arbitrary midpoint
  // to signed or overflow it when the28-bit Bluetooth clock wraps.
  if((delta<0&&mid<=uint64_t(-delta))||
     (delta>=0&&mid>UINT64_MAX-uint64_t(delta)))return false;
  const uint64_t mapped=delta<0?mid-uint64_t(-delta):mid+uint64_t(delta);
  if(!mapped||mapped>callback20||callback20-mapped>5000000)return false;
  out=mapped;return true;
}
// Same programmed packet, four independently validated clock projections.
// Average every projection; never choose by error or discard a wide residual.
// Missing or inconsistent observations refuse this estimate as a whole.
inline bool mean_programmed4(const uint64_t (&values)[4],uint64_t& out){
  out=0;uint64_t lo=UINT64_MAX,hi=0;
  for(const auto value:values){if(!value)return false;lo=value<lo?value:lo;hi=value>hi?value:hi;}
  if(hi-lo>40)return false; // Finite2us consistency screen, not an RF bound.
  uint64_t sum=0;for(const auto value:values)sum+=value-lo;
  out=lo+(sum+2)/4;return true;
}
}

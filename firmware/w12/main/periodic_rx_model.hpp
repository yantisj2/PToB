#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include "../legacy/reference/clock_s3_rx_descriptor.hpp"
namespace v12::periodic_rx {
constexpr unsigned Bytes=190,DirectBytes=158,MaxPdu=Bytes+5,Capacity=16,MaxAgeUs=250000;
struct Stamp {
  uint64_t tick20=0,available20=0;uint32_t epoch=0,sequence=0,coarse=0;
  uint16_t fine=0,raw_metadata=0,event=0;uint8_t activity=255,channel=255;
  uint32_t map_enclosure_ns=UINT32_MAX;
  bool actual:1=false,event_valid:1=false,channel_valid:1=false,edge_calibrated:1=false;
  bool association_observed:1=false; // Diagnostic candidate only; grants no fit eligibility.
  bool refinement_applied:1=false,radio_reference_valid:1=false,radio_reference_applied:1=false;
  int8_t refinement_ticks=0;
  uint8_t phy=255,radio_extra_us=0;
#ifdef PTOB_N30
  // Existing tail padding only. Already-loaded descriptor scalars; no new
  // controller reads or RF endpoint/CRC interpretation. Zero means unknown.
  uint8_t pdu_length=0,ext_header=0;uint16_t descriptor_status=0;
#endif
  uint64_t raw_tick20()const{return tick20+(radio_reference_applied?uint64_t(radio_extra_us)*20:0);}
};
static_assert(sizeof(Stamp)==48,"Periodic stamp must reuse existing padding");
struct Record {adv_rx::BleAnchor anchor{};Stamp stamp{};uint64_t observed_us=0;uint8_t bytes[Bytes]{};uint8_t length=DirectBytes;bool stable=false,single=false;};
inline __attribute__((always_inline)) bool complete_ad(const volatile uint8_t* p,unsigned n){
  return (n==DirectBytes||n==Bytes)&&p[0]==n-1&&p[1]==0xff&&p[2]==0xff&&p[3]==0xff;
}
struct Stats {uint32_t calls=0,candidates=0,matched=0,missed=0,ambiguous=0,overflow=0,fault=0,epoch=0,peak=0,hook_max_cycles=0;bool installed=false,armed=false;
  uint32_t aux_calls=0,aux_in_rx=0,aux_armed=0,reason=0,first=0,index=255,control=0,status=0,header=0,offset=0;};
// Single complete AUX_SYNC_IND only. Reject AuxPtr/chains, anonymous mode,
// unsupported extended header fields and trailing/partial AD bytes.
inline __attribute__((always_inline)) bool payload(const volatile uint8_t* p,unsigned n,unsigned& offset){
  offset=0;if(!p||!n||n>MaxPdu||(p[0]&0xc0))return false;
  const unsigned ext=p[0]&0x3f;if(ext+1>n)return false;
  if(ext){const uint8_t flags=p[1];if(flags&~0x48)return false;
    const unsigned expected=1+((flags&8)?2:0)+((flags&0x40)?1:0);
    if(ext!=expected)return false;}
  offset=ext+1;return complete_ad(p+offset,n-offset);
}
// S3 hardware moves AdvMode/ExtHdrLen into descriptor+16. EM payload begins
// with the extended flags (if any), not that first on-air byte. The pinned
// packet checker computes data length=PDU_length-ExtHdrLen-1 identically.
inline __attribute__((always_inline)) bool descriptor_payload(uint8_t ext_byte,const volatile uint8_t* p,unsigned pdu_bytes,unsigned& offset){
  offset=0;if(!p||!pdu_bytes||pdu_bytes>MaxPdu||(ext_byte&0xc0))return false;
  const unsigned ext=ext_byte&0x3f;if(ext+1>pdu_bytes)return false;
  if(ext){const uint8_t flags=p[0];if(flags&~0x48)return false;
    const unsigned expected=1+((flags&8)?2:0)+((flags&0x40)?1:0);
    if(ext!=expected)return false;}
  offset=ext;return complete_ad(p+offset,pdu_bytes-ext-1);
}
// Bounded consume-once matching. A duplicate physical payload is ambiguous;
// consuming a row never removes it from duplicate detection during its age.
inline bool select(const Record* rows,unsigned count,uint16_t handle,uint16_t active_handle,
                   uint32_t epoch,const uint8_t* bytes,size_t size,uint64_t now_us,
                   uint64_t callback20,Stamp& result,uint32_t& consumed, bool& ambiguous){
  result={};ambiguous=false;consumed=0;
  if(handle!=active_handle||handle==UINT16_MAX||!epoch||!bytes||(size!=DirectBytes&&size!=Bytes))return false;
  unsigned matches=0;const Record* selected=nullptr;
  for(unsigned i=0;i<count;i++){const auto& r=rows[i];
    if(r.length!=size||r.stamp.epoch!=epoch||!r.stamp.sequence||now_us<r.observed_us||now_us-r.observed_us>MaxAgeUs||std::memcmp(bytes,r.bytes,size))continue;
    ++matches;selected=&r;}
  ambiguous=matches>1;if(matches!=1||!selected->stable||!selected->single||!selected->stamp.actual)return false;
  uint64_t tick=0;uint32_t width=0;
  if(!adv_rx::descriptor_map(selected->anchor,selected->stamp.coarse,selected->stamp.fine,tick,width)||
     tick>callback20||callback20-tick>uint64_t(MaxAgeUs)*20)return false;
  result=selected->stamp;result.refinement_applied=false;result.radio_reference_applied=false;
  if(selected->stamp.refinement_applied&&std::abs(int(selected->stamp.refinement_ticks))<=10){
    const int64_t refined=int64_t(tick)+selected->stamp.refinement_ticks;
    if(refined>0&&uint64_t(refined)<=callback20&&callback20-uint64_t(refined)<=uint64_t(MaxAgeUs)*20){
      tick=uint64_t(refined);result.refinement_applied=true;
    }
  }
  // Normalize the captured1M RX once; no donor empirical output bias.
  // Refuse missing/other references instead of silently changing endpoints.
  if(!selected->stamp.radio_reference_valid||selected->stamp.phy!=0||selected->stamp.radio_extra_us>127)return false;
  const auto delta=uint64_t(selected->stamp.radio_extra_us)*20;
  if(tick<=delta||callback20-(tick-delta)>uint64_t(MaxAgeUs)*20)return false;
  tick-=delta;result.radio_reference_applied=true;
  result.tick20=tick;result.available20=callback20;
  result.map_enclosure_ns=width*1000+(result.refinement_applied?unsigned(std::abs(int(result.refinement_ticks)))*50:0);
  consumed=result.sequence;return true;
}
}

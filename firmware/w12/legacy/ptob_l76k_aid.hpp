#pragma once
#include <cstdint>
#include <cstring>
#include <cmath>
namespace ptob_l76k {
// L76K AGNSS Application Note V1.1 (2024), section3.2. Time-only AID-INI.
// GPS-UTC=18s is valid for this bounded table (IERS Bulletin C72).
struct TimeAid {
 bool authenticated=false;
 uint64_t utc_us=0,at_us=0,received_us=0,until_us=0,root=0,boot=0;
 uint32_t uncertainty_us=0,source_epoch=0,publication=0;
};
constexpr uint64_t AidTxBudgetUs=100000, AidFreshUs=250000;
inline void put32(uint8_t* p,uint32_t x){for(unsigned i=0;i<4;i++)p[i]=uint8_t(x>>(8*i));}
inline uint32_t get32(const uint8_t* p){return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
inline bool aid_frame(uint8_t (&out)[66],const TimeAid& a,uint64_t now,uint32_t& bound){
 if(!a.authenticated||!a.root||!a.boot||!a.uncertainty_us||a.uncertainty_us>=250000||
    now<a.at_us||a.at_us<a.received_us||now-a.received_us>=AidFreshUs-AidTxBudgetUs||
    a.until_us<=now||a.until_us-now<=AidTxBudgetUs)return false;
 const uint64_t age=now-a.at_us;
 if(a.utc_us>UINT64_MAX-age)return false;
 const uint64_t utc=a.utc_us+age;
 // Do not extrapolate leap-second knowledge past the next possible leap.
 if(utc<UINT64_C(1483228800000000)||utc>=UINT64_C(1814400000000000))return false;
 const uint64_t gps=utc-UINT64_C(315964800000000)+UINT64_C(18000000);
 const uint64_t week=gps/UINT64_C(604800000000);
 if(week>65535)return false;
 bound=uint32_t(uint64_t(a.uncertainty_us)+age/500+AidTxBudgetUs+1);
 std::memset(out,0,sizeof out);out[0]=0xba;out[1]=0xce;out[2]=56;out[4]=0x0b;out[5]=1;
 const double tow=double(gps%UINT64_C(604800000000))/1000000.0;
 uint64_t bits=0;static_assert(sizeof(double)==8);std::memcpy(&bits,&tow,8);
 for(unsigned i=0;i<8;i++)out[6+24+i]=uint8_t(bits>>(8*i));
 // L76K-specific tAcc is variance scaled by c^2, NOT seconds or zero.
 const double meters=299792458.0*double(bound)/1000000.0;
 const float variance=std::nextafter(float(meters*meters),INFINITY);
 uint32_t fbits=0;static_assert(sizeof(float)==4);std::memcpy(&fbits,&variance,4);put32(out+6+40,fbits);
 out[6+52]=uint8_t(week);out[6+53]=uint8_t(week>>8);
 out[6+54]=0; // GPS time source, as in the L76K AID-INI example.
 out[6+55]=0x42; // Time valid, height invalid; position/frequency remain invalid.
 uint32_t checksum=0;for(unsigned i=2;i<62;i+=4)checksum+=get32(out+i);put32(out+62,checksum);
 return true;
}
// Bounded sliding detector handles ACK split across UART chunks or mixed NMEA.
struct AidAck {
 uint8_t bytes[14]{};unsigned size=0;
 int feed(uint8_t b){
  if(size==14){std::memmove(bytes,bytes+1,13);size=13;}bytes[size++]=b;
  if(size!=14||bytes[0]!=0xba||bytes[1]!=0xce||bytes[2]!=4||bytes[3]||bytes[4]!=5||bytes[5]>1||bytes[6]!=0x0b||bytes[7]!=1||bytes[8]||bytes[9])return 0;
  if(get32(bytes+10)!=get32(bytes+2)+get32(bytes+6))return 0;
  return bytes[5]?1:-1;
 }
};
}

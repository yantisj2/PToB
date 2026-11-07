/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "fault_store.h"
#include <errno.h>
#include <string.h>
static unsigned ascii(const volatile uint32_t *p,unsigned word,unsigned byte)
{return(p[word+byte/4]>>(8*(byte%4)))&255u;}
static uint32_t capsule_crc(const volatile uint32_t *p)
{
 uint32_t crc=UINT32_MAX;
 for(unsigned i=1;i<33;i++)for(unsigned b=0;b<4;b++){
  crc^=(p[i]>>(8*b))&255u;
  for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^(UINT32_C(0xedb88320)&-(crc&1u));
 }
 return ~crc;
}
bool r2_fault_wd_valid(const volatile uint32_t *p,bool retained)
{
 const volatile uint32_t *raw=p+R2_FAULT_WD_WORD;
 bool observed=p[26]==R2_FAULT_CONTEXT_HARDWARE_RESET;
 if(p[0]!=R2_FAULT_WD_MAGIC||p[1]!=R2_FAULT_WD_SCHEMA||p[3]!=512||
    !(p[4]||p[5])||!(p[6]||p[7])||p[25]>R2_FAULT_WD_TERMINAL||
    (p[26]!=R2_FAULT_CONTEXT_SOFTWARE_WD&&!observed)||p[127]!=R2_CRASH_COMMIT||p[126]!=r2_crash_crc(p)||
    raw[0]!=(observed?R2_FAULT_RESET_MAGIC:UINT32_C(0x52325744))||raw[1]!=(observed?2u:1u)||raw[33]!=capsule_crc(raw)||
    p[4]!=raw[2]||p[5]!=raw[3]||p[24]!=raw[4])return false;
 bool terminal=!observed&&((p[24]>=2&&p[24]<=6)||(p[24]>=16&&p[24]<256));
 if(p[25]!=(terminal?R2_FAULT_WD_TERMINAL:0))return false;
 if(observed){if(p[24]||p[27]!=raw[5]||!p[27])return false;}
 else if(p[27])return false;
 for(unsigned i=8;i<16;i++)if(p[i])return false; /* original source UNKNOWN */
 for(unsigned i=0;i<8;i++){
  uint32_t binding=0;
  for(unsigned b=0;b<4;b++){
   unsigned hi=r2_crash_nibble((char)ascii(raw,12,i*8+b*2));
   unsigned lo=r2_crash_nibble((char)ascii(raw,12,i*8+b*2+1));
   if(hi>15||lo>15)return false;
   binding|=((hi<<4)|lo)<<(8*b);
  }
  if(p[16+i]!=binding)return false;
 }
 uint32_t factory[2]={0,0};
 for(unsigned i=0;i<16;i++){
  unsigned nibble=r2_crash_nibble((char)ascii(raw,28,i));if(nibble>15)return false;
  factory[i/8]=(factory[i/8]<<4)|nibble;
 }
 if(p[6]!=factory[1]||p[7]!=factory[0])return false;
 if(retained){if(p[2]!=UINT32_MAX||p[56]||p[57]||p[58])return false;}
 else if(p[2]>=16||!(p[56]||p[57]))return false;
 for(unsigned i=28;i<64;i++)if(i!=56&&i!=57&&i!=58&&p[i])return false;
 for(unsigned i=98;i<126;i++)if(p[i])return false;
 return true;
}
bool r2_fault_record_valid(const volatile uint32_t *p,bool retained)
{return r2_crash_valid(p,retained)||r2_fault_wd_valid(p,retained);}
static uint32_t crc_word(uint32_t crc,uint32_t word)
{
 for(unsigned b=0;b<4;b++){
  crc^=(word>>(8*b))&255u;
  for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^(UINT32_C(0xedb88320)&-(crc&1u));
 }
 return crc;
}
static bool expired(const struct r2_fault_io *io,uint64_t deadline)
{return io->now(io->context)>=deadline;}
static int read_bounded(const struct r2_fault_io *io,unsigned offset,void *out,size_t bytes,uint64_t deadline)
{
 if(offset>R2_FAULT_STORE_BYTES||bytes>R2_FAULT_STORE_BYTES-offset)return -EACCES;
 if(expired(io,deadline))return -ETIMEDOUT;
 int rc=io->read(io->context,offset,out,bytes);
 return rc?rc:expired(io,deadline)?-ETIMEDOUT:0;
}
static int write_bounded(const struct r2_fault_io *io,unsigned offset,const void *data,size_t bytes,uint64_t deadline)
{
 if(offset>R2_FAULT_STORE_BYTES||bytes>R2_FAULT_STORE_BYTES-offset)return -EACCES;
 if(expired(io,deadline))return -ETIMEDOUT;
 int rc=io->write(io->context,offset,data,bytes);
 return rc?rc:expired(io,deadline)?-ETIMEDOUT:0;
}
int r2_fault_append(const struct r2_fault_io *io,uint32_t p[128],uint64_t deadline,unsigned *stored_slot)
{
 if(!io||!io->read||!io->write||!io->now||!p||!stored_slot||!r2_fault_record_valid(p,true))return -EINVAL;
 unsigned free_slot=R2_FAULT_STORE_SLOTS;uint64_t generation=0;uint32_t check[16];
 uint32_t retained_crc=p[126];
 for(unsigned slot=0;slot<R2_FAULT_STORE_SLOTS;slot++){
  bool blank=true,same=true,shape=false;uint32_t crc=UINT32_MAX,wanted_crc=0;uint64_t seq=0;
  for(unsigned off=0;off<512;off+=64){
   int rc=read_bounded(io,slot*512+off,check,64,deadline);if(rc)return rc;
   if(!off){
    shape=check[2]==slot&&check[3]==512&&check[6]==p[6]&&check[7]==p[7]&&
     ((check[0]==R2_CRASH_MAGIC&&(check[1]==R2_CRASH_SCHEMA||check[1]==R2_CRASH_WITNESS_SCHEMA))||
      (check[0]==R2_FAULT_WD_MAGIC&&check[1]==R2_FAULT_WD_SCHEMA));
   }
   for(unsigned i=0;i<16;i++){
    unsigned word=off/4+i;uint32_t value=check[i];
    blank=blank&&value==UINT32_MAX;
    if(word<126)crc=crc_word(crc,value);
    if(word==2)same=same&&value==slot;
    else if(word==56)seq=value;
    else if(word==57)seq|=(uint64_t)value<<32;
    else if(word==58)same=same&&value==retained_crc;
    else if(word==126)wanted_crc=value;
    else same=same&&value==p[word];
   }
  }
  if(blank&&free_slot==R2_FAULT_STORE_SLOTS)free_slot=slot;
  if(shape&&~crc==wanted_crc&&check[15]==R2_CRASH_COMMIT){
   if(seq>generation)generation=seq;
   if(same&&seq){*stored_slot=slot;return 0;} /* every payload+commit byte matched */
  }
 }
 if(free_slot==R2_FAULT_STORE_SLOTS)return -ENOSPC;
 if(generation==UINT64_MAX)return -EOVERFLOW;
 p[2]=free_slot;p[56]=(uint32_t)(generation+1);p[57]=(uint32_t)((generation+1)>>32);p[58]=retained_crc;
 p[126]=r2_crash_crc(p);p[127]=R2_CRASH_COMMIT;
 if(!r2_fault_record_valid(p,false))return -EBADMSG;
 unsigned base=free_slot*512;
 int rc=write_bounded(io,base,p,508,deadline);if(rc)return rc;
 for(unsigned off=0;off<508;off+=64){
  unsigned n=508-off;if(n>64)n=64;
  rc=read_bounded(io,base+off,check,n,deadline);if(rc)return rc;
  if(memcmp(check,(const uint8_t *)p+off,n))return -EIO;
 }
 rc=write_bounded(io,base+508,p+127,4,deadline);if(rc)return rc;
 rc=read_bounded(io,base+508,check,4,deadline);if(rc)return rc;
 if(check[0]!=p[127])return -EIO;
 /* Payload+commit were fully read back; p was validated before write. */
 *stored_slot=free_slot;return 0;
}

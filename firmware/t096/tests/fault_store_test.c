/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
 * Actual append engine at a byte-addressed NOR/public driver boundary. */
#include "fault_store.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static uint8_t flash_bytes[8192],before[8192];
static uint64_t now;
static unsigned reads,writes,bytes_written,cost,fail_read,corrupt_read;
static int cut=-1;
static int read_flash(void *context,unsigned offset,void *out,size_t n)
{
 assert(context==flash_bytes&&offset<=8192&&n<=8192-offset&&n<=64);
 ++reads;now+=cost;if(reads==fail_read)return -EIO;
 memcpy(out,flash_bytes+offset,n);if(reads==corrupt_read)((uint8_t *)out)[0]^=1;return 0;
}
static int write_flash(void *context,unsigned offset,const void *data,size_t n)
{
 assert(context==flash_bytes&&offset<=8192&&n<=8192-offset);
 assert((offset%512==0&&n==508)||(offset%512==508&&n==4));
 ++writes;now+=cost;size_t amount=n;
 if(cut>=0&&bytes_written+amount>(unsigned)cut)amount=(unsigned)cut>bytes_written?(unsigned)cut-bytes_written:0;
 for(unsigned i=0;i<amount;i++){
  uint8_t value=((const uint8_t *)data)[i];assert((flash_bytes[offset+i]&value)==value);flash_bytes[offset+i]=value;
 }
 bytes_written+=(unsigned)amount;
 return cut>=0&&bytes_written>=(unsigned)cut?-EIO:0;
}
static uint64_t time_now(void *context){assert(context==flash_bytes);return now;}
static const struct r2_fault_io io={flash_bytes,read_flash,write_flash,time_now};
static void reset_io(void){now=0;reads=writes=bytes_written=cost=fail_read=corrupt_read=0;cut=-1;}
static void empty(void){memset(flash_bytes,255,sizeof flash_bytes);reset_io();}
static void cpu(uint32_t p[128],uint64_t boot)
{
 memset(p,0,512);p[0]=R2_CRASH_MAGIC;p[1]=R2_CRASH_SCHEMA;p[2]=UINT32_MAX;p[3]=512;
 p[4]=(uint32_t)boot;p[5]=(uint32_t)(boot>>32);p[6]=0x9abcdef0;p[7]=0x12345678;
 for(unsigned i=8;i<24;i++)p[i]=0x01020304+i;
 p[24]=4;p[25]=R2_CRASH_SCB_POST_CLEAR;p[126]=r2_crash_crc(p);p[127]=R2_CRASH_COMMIT;
 assert(r2_fault_record_valid(p,true));
}
static uint32_t wd_crc(const uint32_t *p)
{
 uint32_t c=UINT32_MAX;
 for(unsigned i=1;i<33;i++)for(unsigned b=0;b<4;b++){
  c^=(p[i]>>(8*b))&255;
  for(unsigned bit=0;bit<8;bit++)c=(c>>1)^(0xedb88320u&-(c&1));
 }
 return ~c;
}
static void wd(uint32_t p[128],unsigned reason)
{
 memset(p,0,512);p[0]=R2_FAULT_WD_MAGIC;p[1]=R2_FAULT_WD_SCHEMA;p[2]=UINT32_MAX;p[3]=512;
 p[4]=55;p[6]=0x9abcdef0;p[7]=0x12345678;p[24]=reason;
 p[25]=((reason>=2&&reason<=6)||(reason>=16&&reason<256))?1:0;p[26]=1;
 uint32_t *raw=p+64;raw[0]=0x52325744;raw[1]=1;raw[2]=55;raw[4]=reason;
 const char *binding="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
 memcpy(raw+12,binding,64);memcpy(raw+28,"123456789abcdef0",16);raw[33]=wd_crc(raw);
 for(unsigned i=0;i<8;i++)for(unsigned b=0;b<4;b++){
  unsigned k=i*8+b*2;p[16+i]|=((r2_crash_nibble(binding[k])<<4)|r2_crash_nibble(binding[k+1]))<<(8*b);
 }
 p[126]=r2_crash_crc(p);p[127]=R2_CRASH_COMMIT;assert(r2_fault_wd_valid(p,true));
}
int main(int argc,char **argv)
{
 uint32_t retained[128],p[128],stored[128];unsigned slot=99;
 empty();cpu(retained,1);memcpy(p,retained,512);
 assert(!r2_fault_append(&io,p,500,&slot)&&slot==0&&writes==2&&bytes_written==512);
 memcpy(stored,flash_bytes,512);assert(r2_crash_valid(stored,false)&&stored[58]==retained[126]);
 if(argc==2){FILE *f=fopen(argv[1],"wb");assert(f&&fwrite(flash_bytes,1,512,f)==512&&!fclose(f));}
 reset_io();memcpy(before,flash_bytes,8192);memcpy(p,retained,512);
 assert(!r2_fault_append(&io,p,500,&slot)&&slot==0&&!writes&&!memcmp(before,flash_bytes,8192));
 /* A matching retained-id alone must not deduplicate differing full evidence. */
 cpu(retained,2);((uint32_t *)flash_bytes)[58]=retained[126];
 ((uint32_t *)flash_bytes)[126]=r2_crash_crc((const uint32_t *)flash_bytes);memcpy(before,flash_bytes,512);
 memcpy(p,retained,512);assert(!r2_fault_append(&io,p,500,&slot)&&slot==1&&!memcmp(before,flash_bytes,512));
 /* Occupied/torn/unknown/foreign records are never cleared or selected. */
 empty();memset(flash_bytes,0xa5,512);memset(flash_bytes+512,0,512);memcpy(before,flash_bytes,1024);
 cpu(retained,9);memcpy(p,retained,512);assert(!r2_fault_append(&io,p,500,&slot)&&slot==2&&!memcmp(before,flash_bytes,1024));
 empty();memset(flash_bytes,0x3c,8192);memcpy(before,flash_bytes,8192);memcpy(p,retained,512);
 assert(r2_fault_append(&io,p,500,&slot)==-ENOSPC&&!writes&&!memcmp(before,flash_bytes,8192)&&!memcmp(p,retained,512));
 /* Every possible508+4 byte power-cut boundary retains torn data, never ACKs. */
 for(unsigned boundary=0;boundary<=512;boundary++){
  empty();cpu(retained,100+boundary);memcpy(p,retained,512);cut=(int)boundary;
  assert(r2_fault_append(&io,p,500,&slot)==-EIO);memcpy(before,flash_bytes,8192);
  reset_io();memcpy(p,retained,512);assert(!r2_fault_append(&io,p,500,&slot));
  if(boundary==512)assert(slot==0&&!writes&&!memcmp(before,flash_bytes,8192));
  else if(boundary){assert(slot==1&&!memcmp(before,flash_bytes,512));}
  else assert(slot==0);
 }
 /* Failure of any scan/payload/commit read never acknowledges the pending RAM. */
 for(unsigned call=1;call<=137;call++){
  empty();cpu(retained,3000+call);memcpy(p,retained,512);fail_read=call;
  assert(r2_fault_append(&io,p,500,&slot)==-EIO);
  assert(r2_crash_valid(retained,true));
 }
 empty();cpu(retained,4000);memcpy(p,retained,512);corrupt_read=129;
 assert(r2_fault_append(&io,p,500,&slot)==-EIO);
 empty();cpu(retained,4001);memcpy(p,retained,512);corrupt_read=137;
 assert(r2_fault_append(&io,p,500,&slot)==-EIO);
 /* Shared whole-boot500ms: CPU durable, later WD must remain pending on timeout. */
 empty();cost=1;cpu(retained,5000);memcpy(p,retained,512);
 assert(!r2_fault_append(&io,p,500,&slot)&&now<500);memcpy(before,flash_bytes,512);
 cost=3;wd(retained,5);memcpy(p,retained,512);unsigned old_writes=writes;
 assert(r2_fault_append(&io,p,500,&slot)==-ETIMEDOUT&&writes==old_writes&&
  r2_fault_wd_valid(retained,true)&&!memcmp(before,flash_bytes,512));
 /* Exactly500ms is outside the original budget, with no write or ACK. */
 empty();now=500;cpu(retained,5001);memcpy(p,retained,512);assert(r2_fault_append(&io,p,500,&slot)==-ETIMEDOUT&&!writes);
 /* Distinct watchdog context preserves raw reason1 as NONTERMINAL, not CPU. */
 const unsigned reasons[]={1,2,3,4,5,6,16,29,UINT32_MAX};
 for(unsigned i=0;i<sizeof reasons/sizeof *reasons;i++){
  empty();wd(retained,reasons[i]);assert(!r2_crash_valid(retained,true));memcpy(p,retained,512);
  assert(!r2_fault_append(&io,p,500,&slot));memcpy(stored,flash_bytes,512);assert(r2_fault_wd_valid(stored,false)&&!r2_crash_valid(stored,false));
  assert(stored[25]==(((reasons[i]>=2&&reasons[i]<=6)||(reasons[i]>=16&&reasons[i]<256))?1u:0u));
  reset_io();memcpy(p,retained,512);assert(!r2_fault_append(&io,p,500,&slot)&&!writes);
 }
 /* Torn/corrupt candidate, invalid callbacks, exhausted generation refuse. */
 empty();cpu(retained,6000);
 for(unsigned i=0;i<128;i++){
  memcpy(p,retained,512);p[i]^=1;assert(r2_fault_append(&io,p,500,&slot)==-EINVAL&&!writes);
 }
 memcpy(p,retained,512);assert(!r2_fault_append(&io,p,500,&slot));
 ((uint32_t *)flash_bytes)[56]=((uint32_t *)flash_bytes)[57]=UINT32_MAX;
 ((uint32_t *)flash_bytes)[126]=r2_crash_crc((const uint32_t *)flash_bytes);
 cpu(retained,6001);memcpy(p,retained,512);reset_io();assert(r2_fault_append(&io,p,500,&slot)==-EOVERFLOW&&!writes);
 puts("PASS actual boot-only append: full-byte dedup, preserve16 occupied/torn/foreign slots,513 power cuts,137 read failures, verification mismatch, shared500ms, distinct WD nonterminal context and exhausted generation");
 return 0;
}

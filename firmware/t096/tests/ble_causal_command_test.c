/* Copyright(c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "core.h"
#include "ble_causal_events.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static const char silicon[]="1000000000000001";
static const char source[]="ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff";
int64_t k_uptime_get(void){return 1000;}
int r2_console_note(const char *p,size_t n,uint64_t until){(void)p;(void)n;(void)until;return -EAGAIN;}
int main(void){
 unsigned maximum=
#ifdef R2_BLE_CAUSAL_EVENTS
 13;
#elif defined(R2_GM7_NATIVE)
 9;
#else
 4;
#endif
 struct r2_request r;char command[192],body[2976],frame[3072];
 for(unsigned page=0;page<256;page++){
  snprintf(command,sizeof command,"r2 00000002 %s %s 000000000000007b gm %02x",silicon,source,page);
  int rc=r2_parse(command,&r);
  if(page>maximum){assert(rc==-EINVAL);continue;}
  assert(!rc&&r.op==R2_GM&&r.nonce==page&&r2_bound(&r,silicon,source,123));
  assert(!r2_bound(&r,silicon,source,124)&&!r2_bound(&r,"1000000000000002",source,123));
#ifdef R2_BLE_CAUSAL_EVENTS
  if(page>=10){
   int n=r2_ble_causal_json(body,sizeof body,123,page);assert(n>0&&n<2976);
   int f=r2_frame(frame,sizeof frame,UINT32_MAX,UINT32_MAX,body);assert(f>n&&f<3072);
   unsigned id,seq,length,crc;assert(sscanf(frame,"R2 %x %x %u %x",&id,&seq,&length,&crc)==4);
   const char *payload=strchr(frame,'\n')+1;assert(length==(unsigned)n&&r2_crc32(payload,length)==crc);
   printf("PASS actualparse/binding/historyformat/CRC gm%02x bytes%d\n",page,f);
  }
#endif
 }
 snprintf(command,sizeof command,"r2 00000002 %s %s 000000000000007b gm 0",silicon,source);assert(r2_parse(command,&r)==-EINVAL);
 snprintf(command,sizeof command,"r2 00000002 %s %s 0000000000000000 gm 0a",silicon,source);
 if(maximum>=10){assert(!r2_parse(command,&r)&&!r2_bound(&r,silicon,source,123));}
 printf("PASS actual bounded GM command parser: cap%u, all256pages, malformedwidth, boot/factory binding, originalframe limits\n",maximum);
}

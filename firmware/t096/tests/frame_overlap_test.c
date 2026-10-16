/* Exact legacy wire compatibility with shared main-owner reply storage. */
#include "core.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
int main(void){
 char input[R2_REPLY_BYTES],expected[R2_REPLY_BYTES+128],actual[R2_REPLY_BYTES+128];
 const size_t lengths[]={0,1,2,255,R2_REPLY_BYTES-97};
 for(unsigned i=0;i<sizeof lengths/sizeof lengths[0];i++){
  size_t size=lengths[i];memset(input,'x',size);input[size]=0;
  int n=snprintf(expected,sizeof expected,"R2 %08x %08x %u %08x\n%s\nR2 END %08x %08x\n",
   UINT32_MAX,UINT32_MAX,(unsigned)size,r2_crc32(input,size),input,UINT32_MAX,UINT32_MAX);
  assert(n>0);
  /* Disjoint, exactly coincident, and partially overlapping source storage. */
  assert(r2_frame(actual,sizeof actual,UINT32_MAX,UINT32_MAX,input)==n&&!memcmp(actual,expected,(size_t)n+1));
  memcpy(actual,input,size+1);
  assert(r2_frame(actual,sizeof actual,UINT32_MAX,UINT32_MAX,actual)==n&&!memcmp(actual,expected,(size_t)n+1));
  memcpy(actual+48,input,size+1);
  assert(r2_frame(actual,sizeof actual,UINT32_MAX,UINT32_MAX,actual+48)==n&&!memcmp(actual,expected,(size_t)n+1));
  memcpy(actual,input,size+1);
  assert(r2_frame(actual+8,sizeof actual-8,UINT32_MAX,UINT32_MAX,actual)==n&&!memcmp(actual+8,expected,(size_t)n+1));
  memcpy(actual,input,size+1);
  assert(r2_frame(actual,(size_t)n,UINT32_MAX,UINT32_MAX,actual)==-ENOSPC&&!memcmp(actual,input,size+1));
  assert(r2_frame(actual,(size_t)n+1,UINT32_MAX,UINT32_MAX,actual)==n&&!memcmp(actual,expected,(size_t)n+1));
 }
 puts("PASS legacy framing, CRC, maximum payload, overlapping buffers and exact capacity refusal");
}

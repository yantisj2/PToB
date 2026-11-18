/* SPDX-License-Identifier: MIT
 * Copyright (c) 2026 Jonathan Yantis.
 * Ordinary user-space reads of an already-open stock debugfs view. No MMIO,
 * ioctls, writes, privilege changes or background worker. Caller owns guards.
 */
#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct ptob_runtime_read {uint64_t before,after;uint32_t mfindex,valid;};
struct ptob_realtime_pair {uint64_t before,realtime,after;};
static int raw_now(uint64_t *out){
 struct timespec t;
 if(clock_gettime(CLOCK_MONOTONIC_RAW,&t))return -1;
 *out=(uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;return 0;
}
int ptob_realtime_pair(struct ptob_realtime_pair *out){
 if(!out){errno=EINVAL;return -1;}memset(out,0,sizeof *out);
 struct timespec t;
 if(raw_now(&out->before))return -1;
 if(clock_gettime(CLOCK_REALTIME,&t))return -1;
 if(t.tv_sec<0){errno=ERANGE;return -1;}
 out->realtime=(uint64_t)t.tv_sec*UINT64_C(1000000000)+(uint64_t)t.tv_nsec;
 if(raw_now(&out->after))return -1;
 if(out->after<out->before){errno=EPROTO;return -1;}
 return 0;
}
static int frame(const char *s,size_t n,uint32_t *out){
 if(n<8||memcmp(s,"MFINDEX",7))return -1;
 size_t i=7;while(i<n&&(s[i]==' '||s[i]=='\t'))++i;
 if(i>=n||s[i++]!='=')return -1;
 while(i<n&&(s[i]==' '||s[i]=='\t'))++i;
 if(n-i<11||s[i++]!='0'||s[i++]!='x')return -1;
 uint32_t value=0;
 for(unsigned j=0;j<8;j++){
  unsigned char c=(unsigned char)s[i++];unsigned d;
  if(c>='0'&&c<='9')d=c-'0';else if(c>='a'&&c<='f')d=c-'a'+10;
  else if(c>='A'&&c<='F')d=c-'A'+10;else return -1;
  value=(value<<4)|d;
 }
 if(i>=n||s[i]!='\n'||value>=16384)return -1;
 *out=value;return 0;
}
int ptob_runtime_burst(int fd,uint32_t limit,uint64_t duration,
                       struct ptob_runtime_read *rows,uint32_t *count){
 if(!count){errno=EINVAL;return -1;}*count=0;
 if(!rows||!limit||limit>24||!duration||duration>500000){errno=EINVAL;return -1;}
 uint64_t start;if(raw_now(&start))return -1;
 unsigned transitions=0;
 for(uint32_t i=0;i<limit;i++){
  char text[1024];struct ptob_runtime_read *r=&rows[i];memset(r,0,sizeof *r);
  if(raw_now(&r->before))return -1;
  ssize_t n=pread(fd,text,sizeof text,0);int saved=errno;
  if(raw_now(&r->after))return -1;
  *count=i+1;
  if(n<0){errno=saved;return -1;}
  if(frame(text,(size_t)n,&r->mfindex)){errno=EPROTO;return -1;}
  r->valid=1;
  if(i&&((r->mfindex-rows[i-1].mfindex)&16383u)==1)++transitions;
  if(transitions>=2||r->after-start>=duration)break;
 }
 return 0;
}

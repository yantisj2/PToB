/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Production append/commit/recovery with a bounded NOR flash model. */
#include "lora_passive_store.h"
#include "core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define __aligned(n) __attribute__((aligned(n)))
struct device{int unused;};static const struct device fake;
#define DEVICE_DT_GET(n) (&fake)
#define DT_CHOSEN(n) 0
static bool device_is_ready(const struct device *d){return d==&fake;}
static uint8_t memory[LP_STORE_BYTES];static unsigned calls,fail_at,erases;
static int flash_read(const struct device *d,uint32_t at,void *out,size_t n){assert(d==&fake&&at>=LP_STORE_START&&at+n<=LP_STORE_START+sizeof memory);memcpy(out,memory+(at-LP_STORE_START),n);return 0;}
static int flash_write(const struct device *d,uint32_t at,const void *p,size_t n){assert(d==&fake&&at>=LP_STORE_START&&at+n<=LP_STORE_START+sizeof memory&&!(at%4)&&!(n%4));
 ++calls;size_t done=calls==fail_at?n/8*4:n;
 for(size_t i=0;i<done;i++){uint8_t x=((const uint8_t *)p)[i];assert((memory[at-LP_STORE_START+i]&x)==x);memory[at-LP_STORE_START+i]&=x;}
 return calls==fail_at?-1:0;
}
static int flash_erase(const struct device *d,uint32_t at,size_t n){assert(d==&fake&&at>=LP_STORE_START&&at+n<=LP_STORE_START+sizeof memory&&!(at%4096)&&n==4096);++erases;memset(memory+(at-LP_STORE_START),255,n);return 0;}
#include R2_PASSIVE_STORE_BODY
static void provision(uint64_t factory){
 uint32_t image[144]={MAGIC,1,1,sizeof(struct lp_checkpoint)};memcpy(image+4,&factory,8);
 struct lp_checkpoint s={.seed=0x51a7c003};memcpy(image+6,&s,sizeof s);image[142]=r2_crc32(image,568);image[143]=COMMIT;memcpy(memory,image,sizeof image);
}
int main(int argc,char **argv){assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);memset(memory,255,sizeof memory);struct lp_store s;struct lp_checkpoint state;
 if(mode==1){assert(!lp_store_load(&s,1,&state));return 0;}
 provision(1);assert(lp_store_load(&s,1,&state)&&s.sequence==1&&state.seed==0x51a7c003);
 if(mode==2){assert(!lp_store_load(&s,2,&state));return 0;}
 if(mode==3||mode==4){state.cursor=2;fail_at=mode==3?1:4;assert(!lp_store_save(&s,&state));assert(lp_store_load(&s,1,&state)&&s.sequence==1&&!state.cursor);assert(!lp_store_save(&s,&state));return 0;}
 if(mode==5){memory[24]^=1;assert(!lp_store_load(&s,1,&state));return 0;}
 if(mode==6){memcpy(memory+LP_STORE_RECORD_BYTES,memory,LP_STORE_RECORD_BYTES);assert(!lp_store_load(&s,1,&state));return 0;}
 for(unsigned i=1;i<200;i++){
  state.cursor=2*i;state.serial=i;if(i%3==1)assert(lp_store_prepare(&s));assert(lp_store_save(&s,&state));
  struct lp_store recovered;struct lp_checkpoint r;assert(lp_store_load(&recovered,1,&r)&&r.cursor==2*i&&r.serial==i&&recovered.sequence==s.sequence);recovered.prepared_page=s.prepared_page;s=recovered;
 }
 assert(erases>0);uint32_t at=address(s.next);(void)at;
 if(mode==8){s.sequence=LP_STORE_MAX_COMMITS;assert(!lp_store_save(&s,&state));}
 printf("PASS store %u commits=%u erases=%u\n",mode,s.sequence,erases);return 0;
}

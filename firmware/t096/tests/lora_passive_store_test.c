/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Production append/commit/recovery with a bounded NOR flash model. */
#include "lora_passive_store.h"
#include "core.h"
#include "lora_watchdog.h"
union r2_diag_scratch r2_diag_scratch;
static uint64_t fake_us;
static uint64_t k_uptime_ticks(void){return fake_us;}
static uint64_t k_ticks_to_us_floor64(uint64_t t){return t;}
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
static size_t cut_bytes=SIZE_MAX,programmed;static unsigned read_fault;
static uint64_t body_delay_us,commit_delay_us;
static int flash_read(const struct device *d,uint32_t at,void *out,size_t n){assert(d==&fake&&at>=LP_STORE_START&&at+n<=LP_STORE_START+sizeof memory);memcpy(out,memory+(at-LP_STORE_START),n);fake_us+=3;
 if(read_fault==1&&calls==1)return -1;
 if(read_fault==2&&calls==1)((uint8_t*)out)[0]^=1;
 if(read_fault==3&&calls==2&&n==4)return -1;
 return 0;}
static int flash_write(const struct device *d,uint32_t at,const void *p,size_t n){assert(d==&fake&&at>=LP_STORE_START&&at+n<=LP_STORE_START+sizeof memory&&!(at%4)&&!(n%4));
 ++calls;fake_us+=calls==1?body_delay_us:commit_delay_us;size_t done=calls==fail_at?n/8*4:n;
 for(size_t i=0;i<done;i++){if(programmed>=cut_bytes)return -1;uint8_t x=((const uint8_t *)p)[i];assert((memory[at-LP_STORE_START+i]&x)==x);memory[at-LP_STORE_START+i]&=x;++programmed;if(programmed==cut_bytes)return -1;}
 return calls==fail_at?-1:0;
}
static int flash_erase(const struct device *d,uint32_t at,size_t n){assert(d==&fake&&at>=LP_STORE_START&&at+n<=LP_STORE_START+sizeof memory&&!(at%4096)&&n==4096);++erases;memset(memory+(at-LP_STORE_START),255,n);return 0;}
#include R2_PASSIVE_STORE_BODY
static void provision(uint64_t factory){
 uint32_t image[144]={MAGIC,1,1,sizeof(struct lp_checkpoint)};memcpy(image+4,&factory,8);
 struct lp_checkpoint s={.seed=0x51a7c003};memcpy(image+6,&s,sizeof s);image[142]=r2_crc32(image,568);image[143]=COMMIT;memcpy(memory,image,sizeof image);
}
static void matrix_case(unsigned target,size_t cut,unsigned fault){
 memset(memory,255,sizeof memory);provision(1);unsigned old=(target+55)%56;
 uint8_t original[LP_STORE_RECORD_BYTES];memcpy(original,memory,sizeof original);
 memset(memory,255,LP_STORE_RECORD_BYTES);memcpy(memory+(address(old)-LP_STORE_START),original,sizeof original);
 struct lp_store s;struct lp_checkpoint state;calls=fail_at=0;cut_bytes=SIZE_MAX;programmed=0;read_fault=0;
 assert(lp_store_load(&s,1,&state)&&s.next==target&&s.sequence==1);
 state.cursor=2;state.serial=1;state.pending=1;state.buckets[0]=1643309;
 uint8_t before[LP_STORE_BYTES];memcpy(before,memory,sizeof before);
 r2_diag_scratch.lora.attempt.valid=1;cut_bytes=cut;read_fault=fault;bool ok=lp_store_save(&s,&state);
 bool complete=cut>=576&&(fault==0||fault==3); /* Marker may be programmed even if API reports failure. */
 if(cut!=SIZE_MAX||fault)assert(!ok&&!s.ready&&(r2_diag_scratch.lora.store.phase&128));else assert(ok&&calls==2);
 cut_bytes=SIZE_MAX;read_fault=0;struct lp_store recovered;struct lp_checkpoint actual;
 assert(lp_store_load(&recovered,1,&actual));
 assert(recovered.sequence==(complete?2u:1u));
 if(complete){assert(!memcmp(&actual,&state,sizeof state)&&actual.pending==1&&actual.buckets[0]==1643309);
  uint32_t image[144];memcpy(image,memory+(address(target)-LP_STORE_START),sizeof image);
  assert(image[0]==MAGIC&&image[1]==1&&image[2]==2&&image[3]==sizeof state&&image[140]==0&&image[141]==0&&image[143]==COMMIT&&image[142]==r2_crc32(image,568));
 }else assert(actual.cursor==0&&!actual.pending&&!actual.buckets[0]&&actual.seed==state.seed);
 assert(!memcmp(memory+(address(old)-LP_STORE_START),original,sizeof original));
 unsigned first=address(target)-LP_STORE_START;
 assert(!memcmp(memory,before,first)&&!memcmp(memory+first+576,before+first+576,sizeof memory-first-576));
}
static void cut_matrix(void){
 unsigned cases=0;
 for(unsigned slot=0;slot<56;slot++){
  for(unsigned cut=0;cut<=576;cut+=4){matrix_case(slot,cut,0);++cases;}
  for(unsigned n=1;n<=3;n++){matrix_case(slot,572+n,0);++cases;}
  matrix_case(slot,SIZE_MAX,0);++cases;
 }
 printf("PASS store word/commit-byte cut matrix cases=%u\n",cases);
}
static void readback_matrix(void){
 for(unsigned slot=0;slot<56;slot++)for(unsigned fault=1;fault<=3;fault++)matrix_case(slot,SIZE_MAX,fault);
 body_delay_us=700000;commit_delay_us=350000;matrix_case(0,SIZE_MAX,0);
 assert(r2_diag_scratch.lora.store.body_us==700000&&r2_diag_scratch.lora.store.verify_us==27&&r2_diag_scratch.lora.store.commit_us==350003&&r2_diag_scratch.lora.store.phase==4&&r2_diag_scratch.lora.store.sequence==2);
 struct r2_lora_store_timing first=r2_diag_scratch.lora.store;
 r2_diag_scratch.lora.attempt.valid=7;struct lp_store later;struct lp_checkpoint next;
 assert(lp_store_load(&later,1,&next)&&lp_store_save(&later,&next));assert(!memcmp(&first,&r2_diag_scratch.lora.store,sizeof first));
 /* A corrupted committed successor is ambiguous: never silently fall back. */
 memory[(address(0)-LP_STORE_START)+24]^=1;struct lp_store s;struct lp_checkpoint state;assert(!lp_store_load(&s,1,&state));
 printf("PASS store readback/CRC/phase matrix cases=170\n");
}
int main(int argc,char **argv){assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);if(mode==12){cut_matrix();return 0;}if(mode==13){readback_matrix();return 0;}memset(memory,255,sizeof memory);struct lp_store s;struct lp_checkpoint state;
 if(mode==1){assert(!lp_store_load(&s,1,&state));return 0;}
 provision(1);assert(lp_store_load(&s,1,&state)&&s.sequence==1&&state.seed==0x51a7c003);
 if(mode==2){assert(!lp_store_load(&s,2,&state));return 0;}
 if(mode==3||mode==4){state.cursor=2;fail_at=mode==3?1:2;assert(!lp_store_save(&s,&state));assert(lp_store_load(&s,1,&state)&&s.sequence==1&&!state.cursor);assert(!lp_store_save(&s,&state));return 0;}
 if(mode==5){memory[24]^=1;assert(!lp_store_load(&s,1,&state));return 0;}
 if(mode==6){memcpy(memory+LP_STORE_RECORD_BYTES,memory,LP_STORE_RECORD_BYTES);assert(!lp_store_load(&s,1,&state));return 0;}
 if(mode>=9&&mode<=11){
  uint32_t image[144];memcpy(image,memory,sizeof image);image[1]=2;image[2]=2;
  if(mode==10)image[6+520/4]=2; /* A cursor cannot be silently cleared. */
  if(mode==11)image[6+516/4]^=1; /* Original physical seed must match. */
  image[142]=r2_crc32(image,568);memcpy(memory+576,image,sizeof image);
  if(mode==10||mode==11){assert(!lp_store_load(&s,1,&state)&&!s.ready);return 0;}
  assert(lp_store_load(&s,1,&state)&&(s.ready&LP_STORE_INITIAL_QUIET)&&s.sequence==2&&state.seed==0x51a7c003);
  assert(lp_store_save(&s,&state)&&!(s.ready&LP_STORE_INITIAL_QUIET));
  assert(lp_store_load(&s,1,&state)&&s.sequence==3&&!(s.ready&LP_STORE_INITIAL_QUIET));return 0;
 }
 for(unsigned i=1;i<200;i++){
  state.cursor=2*i;state.serial=i;if(i%3==1)assert(lp_store_prepare(&s));assert(lp_store_save(&s,&state));
  struct lp_store recovered;struct lp_checkpoint r;assert(lp_store_load(&recovered,1,&r)&&r.cursor==2*i&&r.serial==i&&recovered.sequence==s.sequence);recovered.prepared_page=s.prepared_page;s=recovered;
 }
 assert(erases>0);uint32_t at=address(s.next);(void)at;
 if(mode==8){s.sequence=LP_STORE_MAX_COMMITS;assert(!lp_store_save(&s,&state));}
 printf("PASS store %u commits=%u erases=%u\n",mode,s.sequence,erases);return 0;
}

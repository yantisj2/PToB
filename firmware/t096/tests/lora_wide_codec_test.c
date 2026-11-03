/* Copyright (c)2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_passive_core.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static const struct r2_group_config group={.id=9,.epoch=1,.psk={1,2,3}};
static const struct lp_scope scope={.emitter=1,.boot=2,.root=1,.root_boot=2,.epoch=3,.stream=1,.seed=0x51a7c003,.kind=1};
static void print_hex(const uint8_t *p,size_t n){for(size_t i=0;i<n;i++)printf("%02x",p[i]);puts("");}
int main(int argc,char **argv){
 uint8_t context[16];assert(!lp_context(context,&scope,LP_SCHEDULE,&group));
 if(argc==1){
  print_hex(context,sizeof context);
  for(unsigned k=1;k<=4;k++){
   struct lp_packet p={.kind=k,.schedule=LP_SCHEDULE,.scope=scope,.cursor=0,.serial=1,
    .utc_ns=k==LP_META||k==LP_FOLLOW?UINT64_C(1000000000000):0,.tz={2,-300},
    .has_pair=k<3,.time_valid=k<3,.delay_10ms=k<3?85:0};
   uint8_t wire[LP_META_BYTES];assert(!lp_encode(wire,sizeof wire,&p,context,&group));
   struct lp_packet decoded;assert(!lp_decode(&decoded,wire,lp_size(k),context,&group));print_hex(wire,lp_size(k));
  }
  return 0;
 }
 assert(argc==2);FILE *f=fopen(argv[1],"r");assert(f);char line[LP_META_BYTES*2+2];
 assert(fgets(line,sizeof line,f));unsigned count=0;
 while(fgets(line,sizeof line,f)){
  size_t n=strcspn(line,"\r\n")/2;uint8_t wire[LP_META_BYTES];assert(n<=sizeof wire);
  for(size_t i=0;i<n;i++){unsigned value;assert(sscanf(line+i*2,"%2x",&value)==1);wire[i]=(uint8_t)value;}
  struct lp_packet p;assert(lp_decode(&p,wire,n,context,&group));++count;
 }
 assert(count==4);fclose(f);
 struct lp_rx r={0};struct lp_packet wrong={.kind=LP_META,.schedule=LP_SCHEDULE==3?4:3,.scope=scope,.serial=1,.utc_ns=1000000000000,.time_valid=true,.tz={2,-300}};
 assert(!lp_rx_meta(&r,&wrong,&group,NULL,1000000,1000000)&&!r.cached&&!r.have_pair);
 puts("PASS wrong authenticated schedule/context rejected before receiver admission");
}

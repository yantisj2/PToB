/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_rc8.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static const struct r2_group_config group={.id=UINT64_C(0x0102030405060708),.epoch=7,
 .psk={0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31}};
static bool zero(const void *data,size_t n){const uint8_t *p=data;for(size_t i=0;i<n;i++)if(p[i])return false;return true;}
static struct rc8_name named(void){
 struct rc8_scope s={.emitter=0x101,.emitter_boot=0x202,.stream=0x303,.root=0x404,.root_boot=0x505,
 .source_epoch=6,.scale=RC8_UTC,.kind=RC8_SELECTED_BLE};
 struct rc8_name p={.sequence=17,.name="Field-7"};assert(!rc8_context_id(p.context,&s,&group));return p;
}
static void reject(uint8_t *w){
 struct rc8_name p;memset(&p,0xa5,sizeof p);assert(!r2_group_mac(w+64,&group,"PTOBR8N5",w,64));
 assert(rc8_name_decode(&p,w,RC8_NAME_BYTES,&group));assert(zero(&p,sizeof p));
}
int main(int argc,char **argv){
 (void)argv;struct rc8_name p=named(),q;uint8_t w[RC8_NAME_BYTES],changed[RC8_NAME_BYTES];
 assert(!rc8_name_encode(w,&p,&group));
 if(argc>1){for(size_t i=0;i<sizeof w;i++)printf("%02x",w[i]);puts("");return 0;}
 assert(sizeof w==80&&w[24]==7&&!memcmp(w+25,"Field-7",7)&&zero(w+32,32));
 assert(!rc8_name_decode(&q,w,sizeof w,&group));assert(q.sequence==17&&!memcmp(q.context,p.context,16)&&!strcmp(q.name,p.name));
 for(size_t i=0;i<sizeof w;i++)for(unsigned b=0;b<8;b++){
  memcpy(changed,w,sizeof w);changed[i]^=(uint8_t)(1u<<b);memset(&q,0xa5,sizeof q);
  assert(rc8_name_decode(&q,changed,sizeof changed,&group)&&zero(&q,sizeof q));
 }
 for(size_t n=0;n<=sizeof w+2;n++)if(n!=sizeof w){memset(&q,0xa5,sizeof q);assert(rc8_name_decode(&q,w,n,&group)&&zero(&q,sizeof q));}
 struct r2_group_config bad=group;bad.id++;assert(rc8_name_decode(&q,w,sizeof w,&bad)==-EACCES&&zero(&q,sizeof q));
 bad=group;bad.epoch++;assert(rc8_name_decode(&q,w,sizeof w,&bad)==-EACCES);
 bad=group;bad.psk[0]++;assert(rc8_name_decode(&q,w,sizeof w,&bad)==-EACCES);
 assert(!r2_group_mac(changed+64,&group,"PTOBR8T5",w,64));memcpy(changed,w,64);
 assert(rc8_name_decode(&q,changed,sizeof changed,&group)==-EACCES);
 /* Authenticated malformed layouts must still fail; reserved bytes and name
  * padding have exactly one canonical representation. */
 for(unsigned field=0;field<10;field++){
  memcpy(changed,w,sizeof w);
  switch(field){case 0:changed[3]=2;break;case 1:changed[24]=0;break;case 2:changed[24]=25;break;
   case 3:memset(changed+4,0,16);break;case 4:memset(changed+20,0,4);break;
   case 5:changed[25]='\n';break;case 6:changed[26]=0;break;case 7:changed[32]=1;break;
   case 8:changed[49]=1;break;default:changed[63]=1;break;}
  reject(changed);
 }
 /* Neither old packet decoder can mistake descriptive names for time/scope. */
 struct rc8_packet timing;struct rc8_scope scope;uint8_t context[16];
 assert(rc8_decode(&timing,w,sizeof w,&group)&&!timing.sequence);
 assert(rc8_metadata_decode(&scope,context,w,sizeof w,&group)&&!scope.emitter);
 for(unsigned field=0;field<6;field++){
  p=named();switch(field){case 0:p.sequence=0;break;case 1:memset(p.context,0,16);break;
   case 2:p.name[0]=0;break;case 3:memset(p.name,'a',sizeof p.name);break;
   case 4:p.name[0]=0x7f;break;default:p.name[0]=(char)0xc0;break;}
  memset(changed,0xa5,sizeof changed);assert(rc8_name_encode(changed,&p,&group)&&zero(changed,sizeof changed));
 }
 p=named();memset(p.name,'x',RC8_NAME_MAX);p.name[RC8_NAME_MAX]=0;
 assert(!rc8_name_encode(w,&p,&group)&&w[24]==24&&!rc8_name_decode(&q,w,sizeof w,&group));assert(!strcmp(q.name,p.name));
 p=named();strcpy(p.name,"Caf\xc3\xa9");assert(!rc8_name_encode(w,&p,&group)&&!rc8_name_decode(&q,w,sizeof w,&group)&&!strcmp(q.name,p.name));
 p=named();assert(rc8_name_encode(w,&p,NULL)&&zero(w,sizeof w));
 assert(rc8_name_encode(w,NULL,&group)&&zero(w,sizeof w));
 memset(&q,0xa5,sizeof q);assert(rc8_name_decode(&q,NULL,80,&group)&&zero(&q,sizeof q));
 assert(rc8_name_encode(NULL,&p,&group)&&rc8_name_decode(NULL,w,80,&group));
 puts("RC8 name codec PASS:640 bit corruptions, malformed authenticated layouts, group/domain separation, canonical UTF-8 names, output clearing; no timing authority");
}

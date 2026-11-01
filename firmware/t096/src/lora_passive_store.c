/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "lora_passive_store.h"
#include <zephyr/drivers/flash.h>
#include <string.h>
#define MAGIC UINT32_C(0x3253504c)
#define COMMIT UINT32_C(0x54494d43)
#define RECORDS (8u*LP_STORE_RECORDS_PER_PAGE)
static const struct device *flash=DEVICE_DT_GET(DT_CHOSEN(zephyr_flash_controller));
/* One worker; bounded chunk verification avoids a duplicate checkpoint. */

_Static_assert(sizeof(struct lp_checkpoint)==536,"Persisted ARM checkpoint layout");
static uint32_t address(unsigned slot){return LP_STORE_START+(slot/7)*4096+(slot%7)*LP_STORE_RECORD_BYTES;}
static uint32_t crc_part(uint32_t crc,const void *p,size_t n){
 const uint8_t *b=p;for(size_t i=0;i<n;i++){crc^=b[i];for(unsigned bit=0;bit<8;bit++)crc=(crc>>1)^((crc&1)?UINT32_C(0xedb88320):0);}return crc;
}
static bool store_failure(struct lp_store *s){s->ready=0;return false;}
static bool erased(unsigned slot){
 uint32_t record[16];
 if(slot>=RECORDS)return false;
 for(unsigned off=0;off<LP_STORE_RECORD_BYTES;off+=sizeof record){unsigned n=LP_STORE_RECORD_BYTES-off;if(n>sizeof record)n=sizeof record;
  if(flash_read(flash,address(slot)+off,record,n))return false;
  for(unsigned i=0;i<n/4;i++)if(record[i]!=UINT32_MAX)return false;}
 return true;
}
/*0 absent/uncommitted,1 valid,-1 ambiguous committed record or read failure. */
static int valid(unsigned slot,uint64_t factory,uint32_t *sequence){
 uint32_t head[6],tail[2],record[16];uint64_t id;
 if(flash_read(flash,address(slot),head,sizeof head)||flash_read(flash,address(slot)+568,tail,sizeof tail))return -1;
 if(tail[1]!=COMMIT)return 0;
 memcpy(&id,head+4,8);
 if(head[0]!=MAGIC||(head[1]!=1&&head[1]!=2)||!head[2]||head[2]>LP_STORE_MAX_COMMITS||head[3]!=sizeof(struct lp_checkpoint)||id!=factory)return -1;
 uint32_t crc=UINT32_MAX;
 for(unsigned off=0;off<568;off+=sizeof record){unsigned n=568-off;if(n>sizeof record)n=sizeof record;
  if(flash_read(flash,address(slot)+off,record,n))return -1;
  crc=crc_part(crc,record,n);}
 if((crc^UINT32_MAX)!=tail[0])return -1;
 if(head[1]==2&&head[2]!=2)return -1;
 *sequence=head[2];return head[1]==2?2:1;
}
static bool verify_bytes(uint32_t,const void *,unsigned);
bool lp_store_load(struct lp_store *s,uint64_t factory,struct lp_checkpoint *out){
 if(!s||!factory||!out||!device_is_ready(flash))return false;
 memset(s,0,sizeof *s);s->factory=factory;unsigned ready=0;
 for(unsigned n=0;n<RECORDS;n++){
  uint32_t sequence=0;int rc=valid(n,factory,&sequence);if(rc<0)return false;
  if(rc){if(sequence==s->sequence)return false;
   if(sequence>s->sequence){s->sequence=sequence;s->latest=(uint8_t)n;ready=LP_STORE_ACTIVE|(rc==2?LP_STORE_INITIAL_QUIET:0);
    if(flash_read(flash,address(n)+24,out,sizeof *out))return false;}}
 }
 if(!s->sequence||s->sequence>=LP_STORE_MAX_COMMITS)return false;
 if(ready&LP_STORE_INITIAL_QUIET){
  /* Installer-only record2 certifies an actual hour of prior RF silence.
   * It must preserve the exact original seed and otherwise-empty record1.
   * Firmware never creates schema2, and consumes it before any admission. */
  uint32_t first=0;if(s->latest!=1||valid(0,factory,&first)!=1||first!=1||!out->seed||
   !verify_bytes(address(0)+24,out,sizeof *out))return false;
  const uint8_t *bytes=(const uint8_t *)out;
  for(unsigned i=0;i<sizeof *out;i++)if((i<offsetof(struct lp_checkpoint,seed)||i>=offsetof(struct lp_checkpoint,seed)+4)&&bytes[i])return false;
 }
 s->next=(uint8_t)((s->latest+1)%RECORDS);s->prepared_page=UINT8_MAX;s->ready=(uint8_t)ready;return true;
}
static bool verify_bytes(uint32_t at,const void *p,unsigned bytes){
 uint32_t record[16];
 for(unsigned off=0;off<bytes;off+=sizeof record){unsigned n=bytes-off;if(n>sizeof record)n=sizeof record;
  if(flash_read(flash,at+off,record,n)||memcmp(record,(const uint8_t *)p+off,n))return false;}
 return true;
}
bool lp_store_save(void *arg,const struct lp_checkpoint *state){
 struct lp_store *s=arg;
 if(!s||!s->ready||!state||s->sequence>=LP_STORE_MAX_COMMITS||!erased(s->next))return false;
 uint32_t head[6]={MAGIC,1,s->sequence+1,sizeof *state,0,0};memcpy(head+4,&s->factory,8);
 uint32_t tail[4]={0,0,0,COMMIT};uint32_t crc=crc_part(UINT32_MAX,head,sizeof head);
 crc=crc_part(crc,state,sizeof *state);tail[2]=crc_part(crc,tail,8)^UINT32_MAX;
 uint32_t at=address(s->next);
 if(flash_write(flash,at,head,sizeof head)||flash_write(flash,at+24,state,sizeof *state)||flash_write(flash,at+560,tail,12)||
 !verify_bytes(at,head,sizeof head)||!verify_bytes(at+24,state,sizeof *state)||!verify_bytes(at+560,tail,12))return store_failure(s);
 uint32_t committed=0;
 if(flash_write(flash,at+572,&tail[3],4)||flash_read(flash,at+572,&committed,4)||committed!=COMMIT)return store_failure(s);
 if(s->next/7==s->prepared_page)s->prepared_page=UINT8_MAX;
 s->ready=LP_STORE_ACTIVE;
 ++s->sequence;s->latest=s->next;s->next=(uint8_t)((s->next+1)%RECORDS);return true;
}
bool lp_store_prepare(struct lp_store *s){
 if(!s||!s->ready)return false;
 if(!erased(s->next)){
  if(s->next%7||s->next/7==s->latest/7)return false;
  if(flash_erase(flash,LP_STORE_START+(s->next/7)*4096,4096))return store_failure(s);
  for(unsigned i=0;i<7;i++)if(!erased((s->next/7)*7+i))return store_failure(s);
 }
 /* Up to three commits are needed for the next complete pair. Pre-erase
  * the following page while quiescent, before a reservation can cross it. */
 unsigned page=s->next/7;
 if(s->next%7<5)return true;
 page=(page+1)%8;
 if(s->prepared_page==page)return true;
 if(page==s->latest/7)return false;
 bool blank=true;for(unsigned i=0;i<7;i++)if(!erased(page*7+i))blank=false;
 if(!blank){uint32_t at=LP_STORE_START+page*4096;
  if(at<LP_STORE_START||at+4096>LP_STORE_START+LP_STORE_BYTES||flash_erase(flash,at,4096))return store_failure(s);
  for(unsigned i=0;i<7;i++)if(!erased(page*7+i))return store_failure(s);
 }
 s->prepared_page=(uint8_t)page;return true;
}

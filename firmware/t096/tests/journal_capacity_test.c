/* Copyright(c)2026 Jonathan Yantis. MIT. Saved-bank input, actual journal body;
 * deterministic flash interruptions, no device IO or timing qualification. */
#define main historical_journal_main
#include "crash_journal_test.c"
#undef main
#include <setjmp.h>
static uint8_t bank[8192];
static jmp_buf interruption;
static unsigned boundary,cut_at,ack_reads;
static bool in_store,allow_cut;
static struct r2_identity physical;
static void unchanged_crash_page(void){assert(!memcmp(storage+4096,bank+4096,4096));}
static void observed(unsigned op,unsigned after,unsigned off,size_t bytes){
 unchanged_crash_page();
 if(op==3&&!after){assert(off==R2_DIAG_START&&bytes==4096);in_store=true;}
 if(!in_store)return;
 assert(!handoffs&&pending);
 unsigned point=boundary++;if(allow_cut&&point==cut_at)longjmp(interruption,1);
}
static void acknowledged(uint32_t id){
 unchanged_crash_page();assert(pending&&!handoffs&&id==retained[126]);
 bool found=false;
 for(unsigned i=0;i<16;i++){
  uint32_t *p=(uint32_t *)(storage+512*i);
  if(valid_crash(p,i)&&p[58]==id){
   for(unsigned n=4;n<24;n++)assert(p[n]==retained[n]);found=true;
  }
 }
 assert(found);ack_reads=read_calls;
}
static void setup_bank(void){
 memcpy(storage,bank,8192);memset(storage+8192,0x53,8192);
 mode=fail_write=read_failure=read_mismatch=write_delay=0;
 writes=erases=read_calls=handoffs=ack_reads=boundary=0;
 pending=true;in_store=allow_cut=false;reset_boot();
 flash_observer=observed;saved_observer=acknowledged;
}
static void restart(void){
 flash_observer=NULL;allow_cut=false;mode=fail_write=read_failure=read_mismatch=write_delay=0;
 read_calls=0;reset_boot();assert(!r2_diag_init(&physical));
 unchanged_crash_page();assert(!pending&&handoffs==1);
 assert(generation>=81&&generation<=89);
}
static void crc_slot(unsigned i){uint32_t *p=(uint32_t *)(storage+512*i);p[126]=r2_crc32(p,504);}
int main(int argc,char **argv){
 assert(argc==3);FILE *f=fopen(argv[2],"rb");assert(f&&fread(retained,1,512,f)==512&&!fclose(f)&&r2_crash_valid(retained,true));
 /* Physical identity comes from captured pending data, never a display alias. */
 snprintf(physical.silicon,sizeof physical.silicon,"%08x%08x",retained[7],retained[6]);physical.boot=99;
 if(!strcmp(argv[1],"--synthetic")){
  memset(storage,255,8192);
  for(unsigned i=0;i<15;i++){
   seed5(i,i<8?81+i:73+i-8,&physical);
   uint32_t *q=(uint32_t *)(storage+i*512);memset(&q[10],'a',64);crc_slot(i);
  }
  seed_crash(15,80);memcpy(bank,storage,8192);
 }else{f=fopen(argv[1],"rb");assert(f&&fread(bank,1,8192,f)==8192&&!fclose(f));}
 setup_bank();assert(!r2_diag_init(&physical));assert(!pending&&handoffs==1&&erases==1&&writes==2&&generation==89&&crash_slot==0);
 assert(ack_reads==read_calls&&read_calls>=16+8+1);unchanged_crash_page();
 uint32_t *p=(uint32_t *)storage;assert(valid_crash(p,0)&&p[56]==89&&p[57]==0&&p[58]==retained[126]);
 unsigned boundaries=boundary;
 for(unsigned i=8192;i<sizeof storage;i++)assert(storage[i]==0x53);
 puts("PASS captured16slots: startupforeignordinarypage0reclaimed, committedpage1byteidentical, max88→89, exactpayload+commitreadbackbeforeoneack");
 for(unsigned negative=0;negative<13;negative++){
  setup_bank();p=(uint32_t *)storage;
  switch(negative){
   case 0:memcpy(&p[10],R2_SOURCE_SHA,64);crc_slot(0);break;
   case 1:p[44]|=0x400;p[80]=0x4c525731;crc_slot(0);break;
   case 2:p[1]=99;crc_slot(0);break;
   case 3:p[126]^=1;break;
   case 4:p[6]^=1;crc_slot(0);break;
   case 5:p[42]=p[43]=0;crc_slot(0);break;
   case 6:p=(uint32_t *)(storage+7*512);p[42]=p[43]=UINT32_MAX;crc_slot(7);break;
   case 7:pending=false;break;
   case 8:p[44]|=0x80000000u;crc_slot(0);break;
   case 9:p[1]=6;p[39]=1;p[44]=0x400;p[80]=0x4c525731;crc_slot(0);break;
   case 10:p[127]=0;break;
   case 11:p[2]=3;crc_slot(0);break;
   case 12:memcpy(p,retained,512);p[2]=0;p[56]=70;p[57]=0;p[58]=retained[126]^1;crc_slot(0);break;
  }
  assert(!r2_diag_init(&physical));assert(!writes&&!erases&&!handoffs);
  assert(negative==7?!pending:pending);
  assert(negative==7?r2_diag_crash_result()==0:r2_diag_crash_result()==-ENOSPC);
  unchanged_crash_page();
 }
 puts("PASS13negativepages/current/fault/unknown/torn/CRC/factory/slot/zerogen/counterfull/nopending/committedcrash cannottriggerreclamation");
 /* Every before/after erase/write/readback boundary uses the actual startup
  * scanner/store. RAM survives these simulated resets; true power loss may
  * lose pendingRAM. The prior committed crash remains safe in either case. */
 for(cut_at=0;cut_at<boundaries;cut_at++){
  setup_bank();allow_cut=true;
  if(!setjmp(interruption)){(void)r2_diag_init(&physical);assert(!"Expectedinterruption");}
  allow_cut=false;flash_observer=NULL;unchanged_crash_page();assert(!handoffs&&pending);
  restart();
 }
 printf("PASS%uactualerase/payload/commit/readbackcutpoints: priorcommittedcrashsurvives, noearlyack, freshscanrecoversonlythroughverifieddurabledata\n",boundaries);
 setup_bank();mode=13;assert(!r2_diag_init(&physical)&&r2_diag_crash_result()==-EIO&&pending&&!handoffs);unchanged_crash_page();restart();
 setup_bank();fail_write=1;mode=8;assert(!r2_diag_init(&physical)&&r2_diag_crash_result()==-EIO&&pending&&!handoffs);unchanged_crash_page();restart();
 puts("PASS partialpageerase/tornpayload leavepriorcommittedcrashintact; restartusesonlyactualblankspace andneverrelaxesvalidation");
 setup_bank();memset(storage,255,4096);assert(!r2_diag_init(&physical)&&!pending&&handoffs==1&&generation==81);unchanged_crash_page();
 puts("PASS resetafterforeignpageerase sequences81above survivingcrash80; retiredordinary88isnothistoricalpersistentauthority");
 /* A fully occupied torn page cannot be classified as foreign ordinary. */
 setup_bank();memset(storage,0,7*512);assert(!r2_diag_init(&physical)&&pending&&!handoffs&&!writes&&!erases&&r2_diag_crash_result()==-ENOSPC);unchanged_crash_page();
 setup_bank();memset(storage,255,4096);pending=false;assert(!r2_diag_init(&physical)&&!pending&&!handoffs&&!writes&&!erases);unchanged_crash_page();
 puts("PASS occupiedmalformedprotectedpageconservativestop; missingpendingRAMafterpowerloss isnotrecreated");
 return 0;
}

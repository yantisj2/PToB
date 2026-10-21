#include "core.h"
#include "ui_pages.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static const char silicon[]="1000000000000001";
static const char source[]="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
static void parse_tests(void)
{
  struct r2_request r; char command[256];
  snprintf(command,sizeof(command),"r2 00000001 %s %s 0000000000000000 status",silicon,source);
  assert(!r2_parse(command,&r) && r.id==1 && r.op==R2_STATUS);
  assert(r2_bound(&r,silicon,source,123));
  assert(!r2_bound(&r,"1000000000000002",source,123));
  r.op=R2_PREPARE; assert(!r2_bound(&r,silicon,source,123));
  r.boot=122; assert(!r2_bound(&r,silicon,source,123));
  r.boot=123; assert(r2_bound(&r,silicon,source,123));
  snprintf(command,sizeof(command),"r2 00000002 %s %s 000000000000007b prepare 00000000000000ab",silicon,source);
  assert(!r2_parse(command,&r) && r.nonce==171 && r.boot==123);
  for(unsigned page=0;page<6;page++){
   snprintf(command,sizeof command,"r2 00000002 %s %s 000000000000007b gm %02x",silicon,source,page);
   if(page<=4)assert(!r2_parse(command,&r)&&r.op==R2_GM&&r.nonce==page&&r2_bound(&r,silicon,source,123));
   else assert(r2_parse(command,&r));
  }
  snprintf(command,sizeof command,"r2 00000002 %s %s 000000000000007b gm 0",silicon,source);assert(r2_parse(command,&r));
  snprintf(command,sizeof command,"r2 00000002 %s %s 0000000000000000 gm 00",silicon,source);assert(!r2_parse(command,&r)&&!r2_bound(&r,silicon,source,123));
  const char *bad[]={"", "r2", "r2 00000000", " r2", "r2  1", "r2 1 ", "r2\t1"};
  for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) assert(r2_parse(bad[i],&r));
  command[strlen(command)-1]='G'; assert(r2_parse(command,&r));
  memset(command,'x',sizeof(command));command[sizeof(command)-1]=0;assert(r2_parse(command,&r));
  snprintf(command,sizeof(command),"r2 00000002 %s %s 0000000000000000 clock",silicon,source);
  assert(!r2_parse(command,&r)&&r.op==R2_CLOCK&&!r2_bound(&r,silicon,source,123));
  r.boot=123;assert(r2_bound(&r,silicon,source,123));
  snprintf(command,sizeof command,"r2 00000003 %s %s 0000000000000000 events 00000000",silicon,source);
  assert(!r2_parse(command,&r)&&r.op==R2_EVENTS&&!r.nonce&&!r2_bound(&r,silicon,source,123));
  r.boot=123;assert(r2_bound(&r,silicon,source,123));
  snprintf(command,sizeof command,"r2 00000003 %s %s 000000000000007b events ffffffff",silicon,source);
  assert(!r2_parse(command,&r)&&r.nonce==UINT32_MAX);
  command[strlen(command)-1]='g';assert(r2_parse(command,&r));
  snprintf(command,sizeof(command),"r2 00000003 %s %s 0000000000000000 mark",silicon,source);
  assert(!r2_parse(command,&r)&&r.op==R2_MARK&&!r2_bound(&r,silicon,source,123));
  r.boot=123;assert(r2_bound(&r,silicon,source,123));
  snprintf(command,sizeof(command),"r2 00000004 %s %s 000000000000007b ui",silicon,source);
  assert(!r2_parse(command,&r)&&r.op==R2_UI&&r.trace_page==R2_UI_PAGES);
  for(unsigned page=0;page<=R2_UI_PAGES;page++){
   snprintf(command,sizeof(command),"r2 00000004 %s %s 000000000000007b ui %02x",silicon,source,page);
   if(page<R2_UI_PAGES)assert(!r2_parse(command,&r)&&r.trace_page==page);
   else assert(r2_parse(command,&r));
  }
  for(unsigned len=0;len<R2_COMMAND_BYTES;len++) { memset(command,'a',len);command[len]=0;assert(r2_parse(command,&r)); }
  snprintf(command,sizeof(command),"r2 00000005 %s %s 000000000000007b relay 0123456789abcdef 00000000",silicon,source);
  assert(!r2_parse(command,&r)&&r.op==R2_RELAY&&r.nonce==UINT64_C(0x0123456789abcdef)&&!r.seconds&&r2_bound(&r,silicon,source,123));
  snprintf(command,sizeof(command),"r2 00000006 %s %s 000000000000007b relay 0123456789abcdef 00000078",silicon,source);
  assert(!r2_parse(command,&r)&&r.seconds==120);
  command[strlen(command)-1]='9';assert(r2_parse(command,&r));
  snprintf(command,sizeof(command),"r2 00000007 %s %s 000000000000007b relay 0000000000000000 0000003c",silicon,source);assert(r2_parse(command,&r));
  snprintf(command,sizeof(command),"r2 00000008 %s %s 0000000000000000 route",silicon,source);
  assert(!r2_parse(command,&r)&&r.op==R2_ROUTE&&!r2_bound(&r,silicon,source,123));
  snprintf(command,sizeof(command),"r2 00000009 %s %s 000000000000007b name 436c6f636b2035",silicon,source);
  assert(!r2_parse(command,&r)&&r.op==R2_NAME&&!strcmp(r.name,"Clock 5")&&r2_bound(&r,silicon,source,123));
  const char *bad_names[]={"", "0", "00", "gg", "436c00", "4141414141414141414141414141414141414141414141414141"};
  for(unsigned i=0;i<sizeof bad_names/sizeof bad_names[0];i++){
    snprintf(command,sizeof(command),"r2 00000009 %s %s 000000000000007b name %s",silicon,source,bad_names[i]);
    assert(r2_parse(command,&r));
  }
  snprintf(command,sizeof(command),"r2 0000000a %s %s 0000000000000000 join",silicon,source);
  assert(!r2_parse(command,&r)&&r.op==R2_JOIN&&!r2_bound(&r,silicon,source,123));
}
static void maintenance_tests(void)
{
  struct r2_maint m={0};
  assert(r2_prepare(&m,1,10,11,false)==-EACCES && m.phase==R2_IDLE);
  assert(r2_prepare(&m,1,11,10,true)==-ETIMEDOUT);
  assert(r2_prepare(&m,1,10,10+R2_MAINTENANCE_MS,true)==-ETIMEDOUT);
  assert(!r2_prepare(&m,1,10,20,true) && m.deadline==10+R2_MAINTENANCE_MS);
  assert(r2_prepare(&m,2,20,20,true)==-EACCES && m.nonce==1);
  assert(r2_commit(&m,1,20,21)==-EACCES);
  assert(!r2_closed(&m,100,0) && m.deadline==100+R2_MAINTENANCE_MS && m.phase==R2_READY);
  assert(r2_commit(&m,2,100,101)==-EACCES);
  assert(!r2_commit(&m,1,100,101) && m.phase==R2_RESET);
  assert(r2_commit(&m,1,100,101)==-EACCES);
  m=(struct r2_maint){0}; assert(!r2_prepare(&m,4,0,0,true));
  assert(r2_closed(&m,R2_MAINTENANCE_MS,0)==-EACCES && m.error==-ETIMEDOUT);
  assert(r2_prepare(&m,5,2001,2001,true)==-EACCES);
  m=(struct r2_maint){0}; assert(!r2_prepare(&m,4,0,0,true));
  assert(r2_closed(&m,1999,-EIO)==-EIO && m.phase==R2_FAULT);
  m=(struct r2_maint){0}; assert(!r2_prepare(&m,4,0,0,true));
  assert(!r2_closed(&m,1999,0)); r2_expire(&m,m.deadline);
  assert(r2_commit(&m,4,m.deadline-1,m.deadline)==-EACCES && m.phase==R2_FAULT);
}
int main(void)
{
  parse_tests(); maintenance_tests();
  assert(r2_crc32("123456789",9)==0xcbf43926);
  char frame[128];assert(r2_frame(frame,8,1,1,"{}") == -ENOSPC);
  int n=r2_frame(frame,sizeof(frame),1,2,"{}");assert(n>0 && (size_t)n==strlen(frame));
  assert(!strcmp(frame,"R2 00000001 00000002 2 a3a6bf43\n{}\nR2 END 00000001 00000002\n"));
  puts("R2 core: malformed input, identity/session, maintenance deadlines and framing PASS");
}

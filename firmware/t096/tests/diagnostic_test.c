#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "time_service.h"
#include "diagnostic.h"
#define __aligned(n) __attribute__((aligned(n)))
#define DT_CHOSEN(x) 0
#define K_FOREVER -1
#define K_SEM_DEFINE(name,a,b) static int name
static int k_sem_take(int *s,int timeout){return 0;}
static void k_sem_give(int *s){}
static int64_t now;static unsigned writes,erases,mode,fail_write;
static void diagnostic_sleep(int ms){now+=ms;}
#define k_sleep diagnostic_sleep
static uint8_t storage[16384] __aligned(4);static bool closed;
static struct r2_service_status radio_status;
void r2_service_snapshot(struct r2_service_status *s){*s=radio_status;}
void r2_ui_snapshot(struct r2_ui_status *s){memset(s,0,sizeof(*s));s->max_frame_ms=17;}
uint32_t r2_ui_stack_unused(void){return 1024;}
int64_t k_uptime_get(void){return now;}
void r2_platform_snapshot(struct r2_platform *p){memset(p,0,sizeof(*p));p->close_done=closed;}
int r2_ui_close_result(void){return closed?0:-EAGAIN;}
int r2_time_close_result(void){return closed?0:-EAGAIN;}
static int flash_read(const struct device *d,unsigned off,void *out,size_t n){assert(off>=R2_DIAG_START&&off+n<=R2_DIAG_START+sizeof(storage));memcpy(out,storage+(off-R2_DIAG_START),n);return 0;}
static int flash_write(const struct device *d,unsigned off,const void *in,size_t n){
 writes++;assert(off>=R2_DIAG_START&&off+n<=R2_DIAG_START+8192);assert(n==508||n==4);
 if(mode==3)return -EIO;
 bool failing=fail_write&&writes==fail_write;size_t bytes=failing&&mode==8?n/2:n;
 if(failing&&mode!=8)return -EIO;
 for(size_t i=0;i<bytes;i++)assert((storage[off-R2_DIAG_START+i]&((const uint8_t*)in)[i])==((const uint8_t*)in)[i]);
 memcpy(storage+(off-R2_DIAG_START),in,bytes);return failing?-EIO:0;
}
static int flash_erase(const struct device *d,unsigned off,size_t n){
 assert((n==8192&&off==R2_DIAG_START)||(n==4096&&(off==R2_DIAG_START||off==R2_DIAG_START+4096)));erases++;
 bool partial=mode==1||(n==4096&&(mode==13||mode==17||mode==19));
 memset(storage+(off-R2_DIAG_START),255,partial?n/2:n);
 if(mode==2||(mode==18&&n==4096))now+=600;
 if(mode==14&&n==4096)now+=2100;
 return n==4096&&(mode==13||mode==17)?-EIO:0;
}
#include "../src/diagnostic.c"
static void seed5(unsigned slot,uint64_t seq,const struct r2_identity *id){
 uint32_t p[128]={0};p[0]=0x44325231;p[1]=5;p[2]=slot;p[3]=512;p[127]=0x434f4d31;
 memcpy(&p[4],&id->boot,8);memcpy(&p[6],id->silicon,16);memcpy(&p[10],R2_SOURCE_SHA,64);memcpy(&p[42],&seq,8);p[126]=r2_crc32(p,504);memcpy(storage+slot*512,p,512);
}
static void finish_write(void){now+=100;r2_diag_poll();assert(!pending_post);store_once();}
static void reboot(const struct r2_identity *id){phase=0;writer_active=pending_post=quiescing=finish_requested=false;assert(!r2_diag_init(id));}
int main(int argc,char **argv)
{
 mode=argc>1?atoi(argv[1]):0;memset(storage,0x53,sizeof(storage));
 struct r2_identity id={.boot=123};strcpy(id.silicon,"123456789abcdef0");
 if(mode==16||mode==18||mode==19)seed5(15,99,&id);
 if(mode==4){uint32_t old[1024]={0};old[0]=0x44325231;old[1]=4;old[3]=4096;old[960]=old[961]=64;old[42]=1;memcpy(&old[6],id.silicon,16);memcpy(&old[10],R2_SOURCE_SHA,64);old[1023]=0x434f4d31;old[1022]=r2_crc32(old,4088);memcpy(storage,old,4096);}
 int rc=r2_diag_init(&id);
 if(mode==1||mode==2||mode==18||mode==19){assert(rc==((mode==1||mode==19)?-EIO:-ETIMEDOUT)&&r2_diag_maintenance_safe());assert(r2_diag_arm(now,0));if(mode>=18)assert(valid5((uint32_t*)(storage+15*512),15)&&!writes);return 0;}
 assert(!rc&&erases==1);assert(!r2_diag_arm(now,0));
 struct r2_service_status s={.active=true,.root=2,.coordinator=3,.standby=3,.root_boot=9,.standby_boot=10,.source_epoch=11,.hot_refusal=32};struct r2_clock_view c={0};s.precision.rms_ns=123;s.precision.rms_valid=true;
 for(unsigned i=0;i<15;i++){now=i*1000;s.at=now*1000;r2_diag_sample(&s,&c);r2_diag_command(i,R2_UI,0,now);}
 assert(phase==5&&command_count==3&&!writes&&!r2_diag_busy());
 r2_diag_command(999,R2_NETWORK,0,now);assert(command_count==3);
 now=20000;s.at=now*1000;r2_diag_sample(&s,&c);
 if(mode==20){
  s.precision.map_valid=true;s.relay.selected=s.relay.active=s.relay.map_valid=s.relay.phase_admitted=true;
  s.relay.role=1;s.relay.parent_identity=111;s.direct_path[0]=1;
  r2_diag_sample(&s,&c);assert(!r2_diag_busy()&&!writes);
  assert((current[2]&0x7d00)==0x7d00);
  now+=10001;s.at=now*1000;s.precision.map_valid=false;r2_diag_sample(&s,&c);
  assert(!r2_diag_busy()&&!writes);
  for(unsigned i=0;i<1000;i++){now++;s.at=now*1000;r2_diag_sample(&s,&c);}
  assert(!writes&&!r2_diag_busy());
  puts("PASS field map/route changes retain RAM flags without initiating flash writes");return 0;
 }
 if(mode>=12&&mode<=14){storage[4096]=0;occupied[8]=true;}
 if(mode==15||mode==17){memset(storage,0x53,8192);seed5(15,99,&id);for(unsigned i=0;i<16;i++)occupied[i]=true;latest_slot=15;generation=99;select_target();assert(erase_needed&&next_slot==0);}
 assert(!r2_diag_mark());
 assert(r2_diag_busy()&&pending_post&&due==22000&&post_until==20100);
 assert(package[80+13]==123&&package[59]==32&&package[112+1]==12&&package[120+1]==14);
 if(mode==5){now=due;r2_diag_poll();assert(phase==-ETIMEDOUT&&!r2_diag_busy()&&!writes);return 0;}
 if(mode==6){
  r2_diag_quiesce();assert(r2_diag_mark()==-ESHUTDOWN);r2_diag_poll();assert(package[33]&8);store_once();
  r2_diag_finish(0);assert(!r2_diag_busy()&&!finish_requested);closed=true;r2_diag_finish(0);assert(r2_diag_busy());r2_diag_poll();store_once();assert(r2_diag_shutdown_ready());return 0;
 }
 radio_status=(struct r2_service_status){.active=true,.follower=true,.at=(uint64_t)now*1000,.service_until=(uint64_t)now*1000+500000};
 bool erasing=erase_needed;erase_needed=false;assert(storage_window());erase_needed=true;assert(!storage_window());radio_status.storage_window=true;assert(storage_window());
 radio_status.backlog=0x100;assert(!storage_window());radio_status.backlog=1;assert(!storage_window());radio_status.backlog=0;assert(storage_window());
 erase_needed=erasing;radio_status.active=false;
 if(mode==12||mode==15)radio_status=(struct r2_service_status){.active=true,.follower=true,.at=((uint64_t)now+100)*1000,.service_until=((uint64_t)now+5000)*1000};
 finish_write();
 if(mode==3){assert(phase==-EIO&&r2_diag_maintenance_safe());return 0;}
 if(mode==13||mode==14){
  assert(phase==(mode==13?-EIO:-ETIMEDOUT)&&!r2_diag_saved()&&r2_diag_maintenance_safe());
  assert(totals.saved==1&&latest_slot==0&&valid5((uint32_t*)storage,0));assert(r2_diag_mark());
  mode=0;reboot(&id);assert(latest_slot==0&&generation==1);assert(!r2_diag_arm(now,0));now+=60000;assert(!r2_diag_mark());finish_write();assert(r2_diag_saved());return 0;
 }
 if(mode==15||mode==17){
  assert(phase==(mode==15?-ETIMEDOUT:-EIO)&&!writes&&!totals.saved&&!r2_diag_saved());
  assert(latest_slot==15&&valid5((uint32_t*)(storage+15*512),15)&&r2_diag_maintenance_safe());
  mode=0;radio_status.active=false;reboot(&id);assert(generation==99&&latest_slot==15&&!erase_needed);assert(!r2_diag_arm(now,0));now+=60000;assert(!r2_diag_mark());finish_write();assert(r2_diag_saved()&&generation==100);return 0;
 }
 assert(r2_diag_saved()&&package[44]==0x100&&valid5((uint32_t*)(storage+latest_slot*512),latest_slot));
 assert(totals.write_bytes==512&&totals.saved==1&&writes==2);
 if(mode==12){
  assert((uint64_t)now<due&&totals.erase_deferred==1&&!totals.page_erases&&storage[4096]==0);
  radio_status.active=false;now+=60000;assert(!r2_diag_mark());finish_write();assert(r2_diag_saved()&&totals.saved==2&&totals.page_erases==1&&latest_slot==1);return 0;
 }
 if(mode==16){assert(generation==100&&latest_slot==0&&totals.page_erases==1&&storage[15*512]==255);return 0;}
 if(mode==10){
  for(unsigned i=0;i<9;i++){
   now+=10001;s.at=now*1000;s.source_epoch++;s.hot_ready=true;s.hot_withdrawals++;s.hot_withdrawal_at=s.at-1000;s.hot_withdrawal_reason=32;
   r2_diag_sample(&s,&c);
   if(i<8){assert(r2_diag_busy()&&(package[44]&64)&&package[59]==32&&due==(uint64_t)now+1999);finish_write();}
   else assert(!r2_diag_busy());
  }
  assert(automatic_count==8&&totals.saved==9&&totals.coalesced);
  now+=3600000;s.at=now*1000;s.hot_withdrawals++;s.hot_withdrawal_at=s.at;r2_diag_sample(&s,&c);assert(r2_diag_busy());finish_write();assert(automatic_count==1);return 0;
 }
 if(mode==11){now+=60000;s.at=now*1000;s.hot_withdrawals++;s.hot_withdrawal_at=s.at-2100000;s.hot_withdrawal_reason=32;r2_diag_sample(&s,&c);assert(phase==-ETIMEDOUT&&!r2_diag_busy()&&totals.saved==1);return 0;}
 if(mode==7||mode==8){
  unsigned saved=latest_slot;uint64_t gen=generation;now+=60000;assert(!r2_diag_mark());fail_write=writes+(mode==7?2:1);finish_write();assert(phase==-EIO);
  reboot(&id);assert(latest_slot==saved&&generation==gen&&next_slot!=saved+1);assert(!r2_diag_arm(now,0));mode=0;fail_write=0;now+=60000;assert(!r2_diag_mark());finish_write();assert(generation==gen+1);return 0;
 }
 if(mode==9){
  unsigned saved=latest_slot;now+=60000;assert(!r2_diag_mark());next_slot=saved/8*8;erase_needed=true;finish_write();assert(phase==-EFAULT);assert(valid5((uint32_t*)(storage+saved*512),saved));return 0;
 }
 /*32 records consume16KiB programming; three event-coupled spare preparations
  * for a fresh bank, with no mandatory erase before those appends. */
 for(unsigned i=1;i<32;i++){now+=60000;assert(!r2_diag_mark());finish_write();assert(r2_diag_saved());}
 assert(totals.saved==32&&totals.write_bytes==16384&&totals.page_erases==(mode==4?4u:3u));
 for(unsigned i=8192;i<sizeof(storage);i++)assert(storage[i]==0x53);
 unsigned saved=latest_slot,old_erases=erases;uint64_t seq=generation;reboot(&id);assert(latest_slot==saved&&generation==seq&&erases==old_erases);
 storage[saved*512+508]=0;reboot(&id);assert(generation==seq-1&&erases==old_erases);
 /* New identity/source cannot inherit a record, cleanup remains startup-only. */
 strcpy(id.silicon,"fedcba9876543210");reboot(&id);assert(generation==0&&erases==old_erases+1);
 puts("Compact journal:512B append,16slots,CRC/commit/tornskip,legacy recovery,full rotation,identity binding,bank1 protection PASS");
}

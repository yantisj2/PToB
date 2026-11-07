/* MIT. Copyright(c)2026 Jonathan Yantis. Actual check/snapshot/configure gates
 * at public synthetic kernel boundaries. Atomics are real C11/pthreads. */
#include "ble_service.h"
#include "rc_check_site.h"
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef atomic_int atomic_t;typedef int atomic_val_t;
static atomic_t fault,first_check_site,started;
static atomic_bool pause_claim,claimed,release_claim,pause_snapshot,snapshot_paused,release_snapshot;
static atomic_uint events,event_crc,site_reads;
static uint64_t self_identity=17;
static int atomic_get(const atomic_t *p){
 if(p==&first_check_site)atomic_fetch_add(&site_reads,1);
 int value=atomic_load(p);
 if(p==&fault&&atomic_exchange(&pause_snapshot,false)){
  atomic_store(&snapshot_paused,true);while(!atomic_load(&release_snapshot))sched_yield();
 }
 return value;
}
static void atomic_clear(atomic_t *p){atomic_store(p,0);}
static int atomic_set(atomic_t *p,int n){return atomic_exchange(p,n);}
static bool atomic_cas(atomic_t *p,int old,int value){
 bool ok=atomic_compare_exchange_strong(p,&old,value);
 if(ok&&(p==&first_check_site||p==&fault)&&atomic_load(&pause_claim)){
  atomic_store(&pause_claim,false);atomic_store(&claimed,true);while(!atomic_load(&release_claim))sched_yield();
 }
 return ok;
}
uint32_t r2_crc32(const void *v,size_t n){const uint8_t *p=v;uint32_t c=UINT32_MAX;
 while(n--){c^=*p++;for(unsigned b=0;b<8;b++)c=(c>>1)^((c&1)?UINT32_C(0xedb88320):0);}return ~c;}
static void r2_timing_event(unsigned kind,int result,uint64_t peer,uint32_t detail,uint32_t a,uint32_t b){
 assert(kind==15&&result==-EIO&&peer==self_identity&&!a&&!b);atomic_store(&event_crc,detail);atomic_fetch_add(&events,1);
}
#define R2_TIMING_OWNER_FAULT 15u
#include R2_CHECK_BODY
#include R2_CHECK_MACRO
static struct r2_service_status status,paused_snapshot;
static struct r2_group_config group;
struct k_mutex{pthread_mutex_t mutex;};
static struct k_mutex state_lock={PTHREAD_MUTEX_INITIALIZER};
#define K_FOREVER (-1)
#define K_NO_WAIT 0
static int k_mutex_lock(struct k_mutex *m,int wait){assert(wait==K_FOREVER);return pthread_mutex_lock(&m->mutex);}
static int k_mutex_unlock(struct k_mutex *m){return pthread_mutex_unlock(&m->mutex);}
#include R2_SNAPSHOT_BODY
static void *snapshot_reader(void *unused){(void)unused;r2_service_snapshot(&paused_snapshot);return 0;}
struct k_thread{int unused;};static struct k_thread owner_thread;
static bool group_ok=true,reset_ok=true;static int join_error;
bool r2_group_valid(const struct r2_group_config *g){(void)g;return group_ok;}
static int k_thread_join(struct k_thread *t,int wait){assert(t==&owner_thread&&!wait);return join_error;}
static bool lt_reset(void){return reset_ok;}
#include R2_CONFIGURE_BOUNDARY
static void reset(void){
 atomic_store(&fault,0);atomic_store(&first_check_site,0);atomic_store(&started,0);
 atomic_store(&events,0);atomic_store(&event_crc,0);atomic_store(&site_reads,0);atomic_store(&pause_claim,false);
 atomic_store(&claimed,false);atomic_store(&release_claim,false);status=(struct r2_service_status){0};
 group_ok=reset_ok=true;join_error=0;atomic_store(&pause_snapshot,false);atomic_store(&snapshot_paused,false);atomic_store(&release_snapshot,false);
}
static void *first(void *arg){unsigned n=(unsigned)(uintptr_t)arg;check_at(false,n==100?"FIRST":"OTHER",n);return 0;}
int main(int argc,char **argv){assert(argc==2);unsigned mode=(unsigned)atoi(argv[1]);reset();
 if(mode==1){
  for(unsigned iteration=0;iteration<200;iteration++){
   reset();pthread_t a,b;assert(!pthread_create(&a,0,first,(void*)100)&&!pthread_create(&b,0,first,(void*)200));
   assert(!pthread_join(a,0)&&!pthread_join(b,0));unsigned site=r2_rc_check_site(atomic_get(&fault),(uint16_t)atomic_get(&first_check_site));
   assert(site==100||site==200);assert(atomic_load(&events)==1);
   const char *why=site==100?"FIRST":"OTHER";assert(atomic_load(&event_crc)==r2_crc32(why,strlen(why)));
   struct r2_service_status s;r2_service_snapshot(&s);assert(s.error==-EIO&&s.transport_fault.operation==R2_RC_CHECK_OPERATION&&s.transport_fault.peer==site&&s.transport_fault.at==0);
  }
  puts("PASS competing actual check/snapshot functions: one coherent firstsite/EIO/event, explicit marker, unknown timestamp");
 }else if(mode==2||mode==7){
  atomic_store(&pause_claim,true);pthread_t t;assert(!pthread_create(&t,0,first,(void*)100));while(!atomic_load(&claimed))sched_yield();
  /* EIO must already be visible while the winner's metadata is preempted.
   * The old313 site-first implementation fails this exact policy assertion. */
  assert(atomic_get(&fault)==-EIO);
  if(mode==2){
   assert(!r2_rc_check_recorded((uint32_t)atomic_get(&first_check_site)));
   check_at(false,"OTHER",200);struct r2_service_status s;r2_service_snapshot(&s);
   assert(s.error==-EIO&&!s.transport_fault.operation&&!atomic_load(&events));
   atomic_store(&release_claim,true);assert(!pthread_join(t,0));r2_service_snapshot(&s);
   assert(s.error==-EIO&&s.transport_fault.peer==100&&atomic_load(&events)==1);
   puts("PASS fault visible before preempted metadata; UNKNOWN until original winner records its site");
  }else{
   uint32_t old=(uint32_t)atomic_get(&first_check_site);
   struct r2_identity id={.silicon="1000000000000001",.boot=22};struct r2_enrollment e={.rc_dynamic=true};
   atomic_store(&started,1);status.closed=true;assert(!configure_boundary(&id,&e));assert(((uint32_t)atomic_get(&first_check_site)&R2_RC_CHECK_GENERATION_MASK)!=(old&R2_RC_CHECK_GENERATION_MASK));
   check_at(false,"OTHER",200);struct r2_service_status s;r2_service_snapshot(&s);assert(s.transport_fault.peer==200);
   assert(atomic_load(&events)==1&&atomic_load(&event_crc)==r2_crc32("OTHER",5));
   atomic_store(&release_claim,true);assert(!pthread_join(t,0));r2_service_snapshot(&s);
   assert(s.error==-EIO&&s.transport_fault.peer==200&&atomic_load(&events)==2);
   puts("PASS paused old winner cannot write metadata across configure generation; current fault/site preserved");
  }
 }else if(mode==3){
  atomic_store(&fault,-ETIMEDOUT);check_at(false,"OTHER",200);
  struct r2_service_status s;r2_service_snapshot(&s);assert(s.error==-ETIMEDOUT&&!s.transport_fault.operation&&!atomic_load(&events));
  puts("PASS existing non-EIO fault remains original winner and gains no RC metadata");
 }else if(mode==4){
  check_at(true,"healthy",100);assert(!atomic_load(&site_reads));assert(!atomic_get(&fault)&&!atomic_get(&first_check_site)&&!atomic_load(&events));
  unsigned evaluated=0;check(++evaluated==0,"once");assert(evaluated==1&&atomic_get(&fault)==-EIO);
  reset();check_at(false,"unknown",5000);assert(atomic_get(&fault)==-EIO&&atomic_get(&first_check_site)==0);
  struct r2_service_status s;r2_service_snapshot(&s);assert(s.error==-EIO&&!s.transport_fault.operation);
  puts("PASS healthy expression evaluated once; invalid metadata never suppresses original EIO, unknown remains unknown");
 }else if(mode==5){
  struct r2_identity id={.silicon="1000000000000001",.boot=22};struct r2_enrollment e={.rc_dynamic=true};
  check_at(false,"FIRST",100);int original=atomic_get(&first_check_site);
  id.result=-1;assert(configure_boundary(&id,&e)==-EINVAL);id.result=0;
  group_ok=false;assert(configure_boundary(&id,&e)==-EINVAL);group_ok=true;
  atomic_store(&started,1);assert(configure_boundary(&id,&e)==-EBUSY);status.closed=true;join_error=-EAGAIN;
  assert(configure_boundary(&id,&e)==-EBUSY);join_error=0;reset_ok=false;
  assert(configure_boundary(&id,&e)==-EIO);reset_ok=true;
  memcpy(id.silicon,"0000000000000000",17);assert(configure_boundary(&id,&e)==-EACCES);
  assert(atomic_get(&first_check_site)==original&&atomic_get(&fault)==-EIO);
  memcpy(id.silicon,"1000000000000001",17);assert(!configure_boundary(&id,&e));
  assert(!r2_rc_check_recorded((uint32_t)atomic_get(&first_check_site))&&!atomic_get(&fault));check_at(false,"OTHER",200);
  assert(r2_rc_check_site(atomic_get(&fault),(uint16_t)atomic_get(&first_check_site))==200);
  puts("PASS original configure admission/join/reset/identity gates preserve failure; exact guarded clear allows next configured owner");
 }else if(mode==6){
  for(unsigned v=0;v<=65535;v++)assert(!r2_rc_check_site(-ETIMEDOUT,(uint16_t)v));
  for(unsigned mask=0;mask<8;mask++)assert(!r2_rc_check_site(-EIO,(uint16_t)mask));
  for(unsigned site=1;site<=4095;site++)assert(r2_rc_check_site(-EIO,r2_rc_check_pack(site))==site);
  assert(!r2_rc_check_pack(0)&&!r2_rc_check_pack(4096));
  puts("PASS every legacyGM low16/ETIMEDOUT value and legacyRC predicate reject typed RC decoding; full12bit range exact");
 }else if(mode==8){
  atomic_store(&first_check_site,(atomic_val_t)UINT32_C(0xfffe0000));
  struct r2_identity id={.silicon="1000000000000001",.boot=22};struct r2_enrollment e={.rc_dynamic=true};
  assert(!configure_boundary(&id,&e));assert((uint32_t)atomic_get(&first_check_site)==R2_RC_CHECK_GENERATION_MASK);
  check_at(false,"EXHAUSTED",100);struct r2_service_status s;r2_service_snapshot(&s);
  assert(s.error==-EIO&&!s.transport_fault.operation&&atomic_load(&events)==1);
  assert(!configure_boundary(&id,&e));assert((uint32_t)atomic_get(&first_check_site)==R2_RC_CHECK_GENERATION_MASK);
  check_at(false,"EXHAUSTED_AGAIN",200);r2_service_snapshot(&s);
  assert(s.error==-EIO&&!s.transport_fault.operation&&atomic_load(&events)==2);
  puts("PASS generation never wraps: exhaustion preserves EIO/event/reset eligibility and keeps metadata UNKNOWN");
 }else if(mode==9){
  struct r2_identity id={.silicon="1000000000000001",.boot=22};struct r2_enrollment e={.rc_dynamic=true};
  assert(!configure_boundary(&id,&e));check_at(false,"FIRST",100);uint16_t old=status.rc_check_generation;
  atomic_store(&pause_snapshot,true);pthread_t t;assert(!pthread_create(&t,0,snapshot_reader,0));while(!atomic_load(&snapshot_paused))sched_yield();
  assert(!configure_boundary(&id,&e));assert(status.rc_check_generation!=old);check_at(false,"OTHER",200);
  atomic_store(&release_snapshot,true);assert(!pthread_join(t,0));
  assert(paused_snapshot.error==-EIO&&paused_snapshot.rc_check_generation==old&&!paused_snapshot.transport_fault.operation);
  struct r2_service_status fresh;r2_service_snapshot(&fresh);assert(fresh.rc_check_generation==status.rc_check_generation&&fresh.transport_fault.peer==200);
  puts("PASS old copied status/new fault generation yields UNKNOWN; fresh current snapshot resolves new site");
 }else if(mode==10){
  memset(&status.transport_fault,0xa5,sizeof status.transport_fault);
  check_at(false,"OTHER",200);struct r2_service_status s;r2_service_snapshot(&s);
  struct r2_transport_fault expected;memset(&expected,0,sizeof expected);
  expected.error=-EIO;expected.operation=R2_RC_CHECK_OPERATION;expected.peer=200;expected.host_detail=r2_rc_check_pack(200);
  assert(!memcmp(&s.transport_fault,&expected,sizeof expected));
  puts("PASS full typed fault structure/padding zeroed before assignment; no stale or future fields retained");
 }else assert(false);
 return 0;
}

/* Production supervisor/worker bookkeeping with injected timer preemption.
 * Kernel calls are fake; exact ARM frame inspection remains separate. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
typedef unsigned k_spinlock_key_t;
void r2_service_supervise(void);
static unsigned lock,held,stops,submits,timer_stops;
static bool started,inject_timer;
static int busy,stop_requested,submit_rc;
static uint64_t fake_now,close_deadline;
struct r2_supervision {
 uint64_t submitted,started,fault_at,fault_submitted,fault_started;
 uint32_t queue_max_us,late_queues;uint8_t state,fault_site,fault_state;
 uint64_t stage_at,fault_stage_at;uint32_t stage_max_us[7],owner_max_us;uint8_t stage,fault_stage;
};
static struct { uint64_t deadline;bool closed;int error;uint32_t worker_max_us;struct r2_supervision supervision; } status;
struct k_timer {int unused;};
static int service_work,owner_queue;
static uint64_t now_us(void){return fake_now;}
static unsigned k_spin_lock(unsigned *p){
 assert(p==&lock&&!held);
 /* Timer preempts immediately before the outer thread disables interrupts. */
 if(inject_timer){inject_timer=false;fake_now++;r2_service_supervise();}
 held=1;return 0;
}
static void k_spin_unlock(unsigned *p,unsigned k){assert(p==&lock&&held);held=0;}
static int atomic_get(int *p){return *p;}
static void atomic_clear(int *p){assert(held);*p=0;}
static bool atomic_cas(int *p,int old,int value){assert(held);if(*p!=old)return false;*p=value;return true;}
static void r2_service_stop(void){assert(!held);stops++;if(!stop_requested){stop_requested=1;close_deadline=fake_now+2000000;}}
static int k_work_submit_to_queue(int *q,int *p){assert(q==&owner_queue);assert(!held&&p==&service_work);assert(busy&&status.supervision.state==1&&status.supervision.submitted==fake_now);submits++;return submit_rc;}
static void k_timer_stop(struct k_timer *p){assert(!held);timer_stops++;}
#include "supervisor_under_test.h"
static void reset(void){lock=held=stops=submits=timer_stops=0;started=true;inject_timer=false;busy=stop_requested=submit_rc=0;fake_now=100000;close_deadline=0;memset(&status,0,sizeof status);}
static void queued(uint64_t age){busy=1;status.supervision.state=1;status.supervision.submitted=fake_now-age;}
int main(void){
 reset();started=false;r2_service_supervise();assert(!submits&&!stops);
 reset();r2_service_supervise();assert(submits==1&&busy&&status.supervision.submitted==fake_now&&!stops);
 reset();queued(49999);r2_service_supervise();assert(!submits&&!stops&&!status.error);
 reset();queued(50000);r2_service_supervise();assert(!submits&&stops==1&&status.error==-ETIMEDOUT&&close_deadline==fake_now+2000000);assert(status.supervision.fault_site==1&&status.supervision.fault_state==1&&status.supervision.fault_submitted==50000&&status.supervision.fault_at==fake_now);
 reset();status.deadline=fake_now;r2_service_supervise();assert(stops==1&&submits==1&&stop_requested);
 reset();status.closed=true;r2_service_supervise();assert(!submits&&!stops);
 reset();queued(0);stop_requested=1;close_deadline=fake_now;r2_service_supervise();assert(status.error==-ETIMEDOUT&&!submits&&close_deadline==fake_now&&status.supervision.fault_site==2);
 reset();queued(0);stop_requested=1;close_deadline=fake_now+1;r2_service_supervise();assert(!status.error&&!stops);
 reset();queued(50000);status.error=-EIO;r2_service_supervise();assert(status.error==-EIO&&stops==1);
 reset();submit_rc=-EIO;r2_service_supervise();assert(status.error==-EIO&&!busy&&stops==1&&status.supervision.fault_site==3&&status.supervision.fault_state==1&&!status.supervision.state);
 reset();struct k_timer timer;supervise_timer(&timer);assert(submits==1&&!timer_stops&&!held);
 reset();status.closed=true;supervise_timer(&timer);assert(!submits&&timer_stops==1&&!held);
 /* Regression: old body falsely times out a0us-old item after this preemption. */
 reset();inject_timer=true;r2_service_supervise();assert(submits==1&&status.supervision.submitted==100001&&!status.error&&!stops);
 /* Defensive backwards time cannot unsigned-underflow into a timeout. */
 reset();queued(0);status.supervision.submitted++;r2_service_supervise();assert(!status.error&&!stops);
 reset();queued(60000);worker_begin();assert(status.supervision.state==2&&status.supervision.started==fake_now&&status.supervision.queue_max_us==60000&&status.supervision.late_queues==1);
 r2_service_supervise();assert(status.supervision.fault_state==2&&status.supervision.fault_started==fake_now&&status.supervision.fault_submitted==40000);
 uint64_t first=status.supervision.fault_at;fake_now+=2000000;r2_service_supervise();assert(status.supervision.fault_at==first&&status.supervision.fault_site==1);
 worker_end();assert(!busy&&!status.supervision.state&&!held);
 reset();queued(122);worker_begin();fake_now+=40000;worker_stage(6);fake_now+=10000;r2_service_supervise();
 assert(status.supervision.fault_stage==6&&status.supervision.fault_stage_at==140000);
 fake_now+=232;worker_stage(7);fake_now+=100;worker_end();
 assert(status.worker_max_us==50332&&status.supervision.owner_max_us==50454&&status.supervision.stage_max_us[5]==10232&&status.supervision.stage_max_us[6]==100);
 reset();queued(10);worker_begin();fake_now+=49990;r2_service_supervise();assert(status.error==-ETIMEDOUT&&status.supervision.fault_state==2&&status.supervision.queue_max_us==10);
 reset();fake_now=UINT64_C(1)<<33;queued(UINT64_C(1)<<32);status.supervision.late_queues=UINT32_MAX;worker_begin();assert(status.supervision.queue_max_us==UINT32_MAX&&status.supervision.late_queues==UINT32_MAX);
 puts("Production supervisor18 branches PASS; coherent preemption/queue/running diagnostics; original50ms submitted-age and2s drain preserved; fake kernel is not ARM liveness evidence");
}

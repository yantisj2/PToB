/* MIT. Copyright (c) 2026 Jonathan Yantis. Production supervisor boundaries and unchanged watchdog composition. */
#include "gm7_service_fake.h"
#include "ble_service.h"
#include "cleanup_budget.h"
#include "gm7_resume_progress.h"
#include <stdio.h>
static struct r2_service_status status;
static bool started, resuming;
static uint64_t close_until;
static atomic_t stopping, busy;
static struct { bool pending; int16_t result; } yield_request;
K_MUTEX_DEFINE(lock);
static struct k_work_q owner_queue;
static struct k_work service_work;
static int submit_result=1;
static uint64_t kernel_now(void) { return k_ticks_to_us_floor64(k_uptime_ticks()); }
void r2_radio_service_withdraw(void) {}
void r2_gm7_controller_cancel(uint64_t now) { (void)now; }
static int submit_boundary(struct k_work_q *q,struct k_work *w) {(void)q;(void)w;return submit_result;}
#define k_work_submit_to_queue submit_boundary
#include "production-supervision.inc"
#undef k_work_submit_to_queue
#include "production-worker-boundaries.inc"
static struct gm7_progress_record records[GM7_PROGRESS_RECORDS];
static unsigned count;
static void record(unsigned expected) {
 assert(count<GM7_PROGRESS_RECORDS);
 records[count++]=(struct gm7_progress_record){fake_kernel,status,expected};
}
static void setup(void) {
 status=(struct r2_service_status){.configured=true,.active=true,.normal=true,.gm_cold=true,.at=100000000};
 fake_kernel=100000000;started=true;resuming=false;close_until=0;stopping=busy=0;submit_result=1;
}
int main(int argc,char **argv) {
 assert(argc==2);
 /* Exact reported extrema form an explicit synthetic schedule, not a claim
  * that the two measured maxima belonged to the same physical cycle. */
 setup();old_supervise();fake_kernel+=12054;uint64_t began=worker_begin();
 fake_kernel=100100000;old_supervise();assert(status.error==-ETIMEDOUT&&stopping&&status.supervision.fault_state==2);
 puts("PASS frozen178 supervisor reproduces terminal busy-age at100000us");
 setup();current_supervise();fake_kernel+=12054;began=worker_begin();
 fake_kernel=100116180;current_supervise();assert(!status.error&&!stopping&&busy&&status.at==100000000);
 worker_finish(began);assert(!busy&&status.worker_max_us==104126&&status.supervision.owner_max_us==116180&&status.at==100000000);record(0);
 puts("PASS current accepts116180us submitted/104126us execution wall schedule and completes without publication renewal");
 setup();current_supervise();fake_kernel+=116180;current_supervise();
 assert(busy&&status.supervision.state==1&&!status.error&&status.at==100000000);record(0);
 puts("PASS queued delay does not become terminal or renew accepted CONTROL");
 setup();current_supervise();fake_kernel=129999000;current_supervise();assert(status.at==100000000&&!status.error);record(0);
 fake_kernel=130000000;current_supervise();assert(status.at==100000000&&!status.error);record(1);
 puts("PASS supervisor-only wedge preserves last actual CONTROL sample for exact30s watchdog");
 setup();status.deadline=100100000;fake_kernel=status.deadline-1;current_supervise();assert(!stopping);
 fake_kernel++;current_supervise();assert(stopping&&status.closing&&!status.error&&!yield_request.pending&&yield_request.result==-ECANCELED);
 uint64_t original=close_until;assert(original==fake_kernel+R2_CLEANUP_US);
 fake_kernel=original-1;current_supervise();assert(!status.error&&close_until==original);
 fake_kernel=original;current_supervise();assert(status.error==-ETIMEDOUT&&close_until==original);record(7);
 puts("PASS original service expiry and unrenewed40s cleanup expiry remain exact");
 setup();submit_result=-EIO;current_supervise();assert(!busy&&status.error==-EIO&&stopping);record(2);
 puts("PASS submission error remains terminal and drains");
 FILE *f=fopen(argv[1],"wb");assert(f&&fwrite(&count,sizeof count,1,f)==1&&fwrite(records,sizeof records[0],count,f)==count&&!fclose(f));
 return 0;
}

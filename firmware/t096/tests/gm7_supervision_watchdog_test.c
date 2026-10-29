/* MIT. Copyright (c) 2026 Jonathan Yantis. Actual unchanged watchdog consumer. */
#include "gm7_watchdog_fixture.inc"
#include "gm7_resume_progress.h"
int main(int argc,char **argv) {
 assert(argc==2);unsigned count;struct gm7_progress_record records[GM7_PROGRESS_RECORDS];
 FILE *f=fopen(argv[1],"rb");assert(f&&fread(&count,sizeof count,1,f)==1&&count<=GM7_PROGRESS_RECORDS&&fread(records,sizeof records[0],count,f)==count&&!fclose(f));
 for(unsigned i=0;i<count;i++) {
  struct gm7_progress_record *r=&records[i];clear_runtime();memset((void *)r2_watchdog_capsule,0,sizeof r2_watchdog_capsule);
  id.reset_cause=0;assert(!r2_watchdog_boot(&id));advance((uint32_t)(r->now/1000));r2_watchdog_enable(true);
  r2_watchdog_observe(&r->status,true);unsigned mask=(unsigned)report_worker_stalled&7u;
  assert(mask==r->expected_stall);bool feeding=watchdog_step();
  assert(feeding==!r->expected_stall);
  if(r->expected_stall){assert(reset_reason==WD_RC_WORKER_STALL);advance((uint32_t)(r->now/1000)+1);struct r2_service_status healthy={.active=true,.gm_cold=true,.at=fake_ms*1000};r2_watchdog_observe(&healthy,true);assert(!watchdog_step());}
 }
 puts("PASS actual unchanged watchdog consumes current snapshots:116ms completion/queued delay feed; exact30s stale, original close expiry and errors remain terminal");return 0;
}

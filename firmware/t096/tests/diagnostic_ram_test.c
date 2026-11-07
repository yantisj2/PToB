/* MIT. Jonathan Yantis, 2026. Actual RAM backend; no NV implementation linked. */
#include <assert.h>
#include <stdio.h>
#include "time_service.h"
#include "diagnostic.h"
#define __aligned(n) __attribute__((aligned(n)))
static int64_t now;
void r2_ui_diag_snapshot(int *error,uint32_t *max_frame_ms,uint32_t *missed){*error=0;*max_frame_ms=17;*missed=0;}
uint32_t r2_ui_stack_unused(void){return 1024;}
int64_t k_uptime_get(void){return now;}
#include "../src/diagnostic.c"
int main(void){
 struct r2_identity id={.boot=123};strcpy(id.silicon,"123456789abcdef0");
 memset(&r2_diag_scratch,0xa5,sizeof r2_diag_scratch);assert(!r2_diag_init(&id)&&!r2_diag_arm(now,0));
 assert(!r2_diag_scratch.lora.retained.boot&&r2_diag_crash_result()==-ENOTSUP);
 struct r2_service_status s={.active=true,.root=2,.coordinator=3,.standby=3,.root_boot=9,.standby_boot=10,.source_epoch=11,.hot_refusal=32,.service_until=5000000};
 struct r2_clock_view c={0};s.precision.rms_ns=123;s.precision.rms_valid=true;
 now=1000;s.at=now*1000;r2_diag_sample(&s,&c);assert(!r2_diag_mark());r2_diag_poll();
 assert(package[1]==8&&package[56]==11&&package[60]==4000&&package[80+13]==123);
 assert(package[30]==1024&&package[41]==UINT32_MAX&&package[34]==1000);
 uint8_t out[64];assert(!r2_diag_read(0,0,out)&&r2_diag_read(1,0,out)==-ENOENT&&r2_diag_read(0,8,out)==-ENOENT);
 uint32_t fault[16]={0x4c525731,(uint32_t)-ETIMEDOUT};uint64_t token=0;
 assert(!r2_diag_lora(fault,true,&token)&&r2_diag_record_result(token)==-EINPROGRESS);r2_diag_poll();
 assert(!r2_diag_record_result(token)&&r2_diag_scratch.lora.retained.terminal&&!memcmp(&package[80],fault,64));
 for(unsigned i=0;i<64;i++){now+=60001;assert(!r2_diag_mark());r2_diag_poll();assert(!memcmp(&package[80],fault,64));}
 struct r2_diag_stats stats;r2_diag_stats(&stats);assert(stats.saved==66&&!stats.write_bytes&&!stats.page_erases&&!stats.erase_deferred);
 id.boot=456;assert(!r2_diag_init(&id));assert(!r2_diag_scratch.lora.retained.boot&&!generation&&!previous_root_boot&&!last_service.root_boot);
 assert(r2_diag_read(0,0,out)==-ENOENT);assert(!r2_diag_arm(now,0));r2_diag_finish(0);r2_diag_poll();assert(r2_diag_shutdown_ready());
 puts("PASS RAM diagnostic metadata, terminal fault,64 events, USB bounds, zero NV backend, reboot clearing");
}

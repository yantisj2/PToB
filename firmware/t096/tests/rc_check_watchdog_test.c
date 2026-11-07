/* MIT. Copyright(c)2026 Jonathan Yantis. Actual accepted WD mailbox/capsule. */
#define main original_watchdog_main
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wreturn-type"
#include "watchdog_test.c"
#pragma clang diagnostic pop
#undef main
static unsigned profile_reads;
static void flip_capsule_profile(const atomic_t *p){
 if(p==&report_rc&&++profile_reads==2){on_atomic_get=NULL;atomic_set(&report_rc,0);}
}
static struct r2_service_status sample(int error,unsigned operation,uint16_t detail){
 return(struct r2_service_status){.at=100000,.active=true,.profile_rc=true,.error=error,
  .transport_fault={.error=error,.operation=operation,.host_detail=detail}};
}
int main(void){
 clear_runtime();fake_ms=100;main_at=100;intent=1;profile_reads=0;
 struct r2_service_status s=sample(-EIO,R2_RC_CHECK_OPERATION,r2_rc_check_pack(3831));
 r2_watchdog_observe(&s,true);on_atomic_get=flip_capsule_profile;assert(!watchdog_step());
 assert(reset_reason==5&&r2_watchdog_capsule[4]==5&&r2_watchdog_capsule[5]==0);
 uint32_t w=r2_watchdog_capsule[32];assert((int16_t)(w>>16)==-EIO&&(w&7)==2);
 assert(r2_rc_check_site((int16_t)(w>>16),(uint16_t)w)==3831);assert(crc(r2_watchdog_capsule)==r2_watchdog_capsule[33]);
 /* A typed descriptive value alone is never a reset predicate. */
 clear_runtime();fake_ms=100;main_at=100;intent=1;s=sample(0,R2_RC_CHECK_OPERATION,r2_rc_check_pack(100));
 r2_watchdog_observe(&s,true);assert(watchdog_step()&&!reset_reason);
 /* Legacy GM ETIMEDOUT uses all low diagnostic bits unchanged. */
 clear_runtime();fake_ms=100;main_at=100;intent=1;s=sample(-ETIMEDOUT,3,65528);s.profile_rc=false;
 r2_watchdog_observe(&s,true);assert(!watchdog_step());w=r2_watchdog_capsule[32];
 assert((int16_t)(w>>16)==-ETIMEDOUT&&(w&65528)==65528&&!r2_rc_check_site((int16_t)(w>>16),(uint16_t)w));
 /* Unmarked EIO cannot acquire an RC tag from another host-detail layout. */
 clear_runtime();fake_ms=100;main_at=100;intent=1;s=sample(-EIO,3,r2_rc_check_pack(200));
 r2_watchdog_observe(&s,true);assert(!watchdog_step()&&(r2_watchdog_capsule[32]&65535)==2);
 puts("PASS actual WD predicate/detail/CRC: typedsite survives capsule profile race; metadata no-reset; legacyGM unchanged");
 return 0;
}

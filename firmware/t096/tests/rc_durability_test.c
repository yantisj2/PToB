/* Actual parent reset/rebind functions, with fake public controller boundaries. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rc_dynamic.h"
#include "rc_gm_adapter.h"
#include "rc_stream_quality.h"
#include "rc_transport_hold.h"
static struct rc_transport_hold downstream_hold;
static struct rc7_receiver receiver;
static struct rc5_gm_context gm_context;
static struct r2_link_quality delivery;
static struct rc5_rate_fit fit;
static struct rc5_clock local_clock;
static struct rc7_path upstream_path,publication_path;
static struct rc5_origin current_origin;
static uint8_t current_proof[16];
static uint64_t lt_renew_at,lt_budget_until,lt_attempt_ready,lt_next,discipline_at,usable_tx_from,phase_rx,phase_step_at,phase_log_at,requested,restart_at,start_delay,upstream_retry_at;
static uint64_t rebind_parent_boot,rebind_root_boot,fit_parent_boot,fit_root_boot;
static uint32_t rebind_root_epoch,rebind_capture_epoch,fit_parent_epoch,fit_root_epoch,fit_rms;
static unsigned sent_count,lt_attempts,holdover_expiries,upstream_recoveries,train=1;
static bool lt_acquiring,lt_paused,phase_admitted,start_prepared,awaiting_first_tx,holdover_expired,fit_rms_valid;
static bool parent_known,parent_synced,creating,running,stop_fails,faulted;
static bool parent_ineligible;
static void *lt_conn;
static bool upstream_usable,admission_live=true,drain_ok=true;
static unsigned parent_admission=2,cancel_calls;
static uint64_t last_context_progress,fake_now,sync_started,cancel_until;
static uint64_t mono_us(void){return fake_now;}
static bool upstream_admitted(uint64_t tick){(void)tick;return admission_live;}
static void check(bool ok,const char *reason){(void)reason;if(!ok)faulted=true;}
static bool sync_close_until(uint64_t until){
 ++cancel_calls;cancel_until=until;
 if(!drain_ok)return false;
 creating=parent_synced=false;sync_started=0;return true;
}
#define RC_FIELD_HOLD_TICKS UINT64_C(8000000)
static void lt_quiesce(bool pause){lt_paused=pause;}
static void r2_shadow_normal_withdraw(void){}
#include "rc_durability_under_test.h"
int main(void){
 parent_ineligible=lt_acquiring=lt_paused=phase_admitted=start_prepared=awaiting_first_tx=holdover_expired=fit_rms_valid=true;
 discipline_at=phase_rx=requested=restart_at=start_delay=upstream_retry_at=123;
 receiver.clock.bound=true;local_clock.count=2;sent_count=2;lt_attempts=3;current_origin.expires=100;
 memset(current_proof,1,sizeof current_proof);
 downstream_hold=(struct rc_transport_hold){.deadline=100,.recovered=50,.last=60,.armed=true};
 parent_handoff_reset();
 assert(!downstream_hold.armed&&!downstream_hold.deadline&&!downstream_hold.recovered&&!downstream_hold.last);
 assert(!parent_ineligible);
 assert(!lt_acquiring&&!lt_paused&&!phase_admitted&&!start_prepared&&!awaiting_first_tx&&!holdover_expired&&!fit_rms_valid);
 assert(!discipline_at&&!phase_rx&&!requested&&!restart_at&&!start_delay&&!upstream_retry_at&&!receiver.clock.bound&&!local_clock.count&&!sent_count&&!lt_attempts&&!current_origin.expires);
 for(unsigned i=0;i<sizeof current_proof;i++)assert(!current_proof[i]);
 parent_known=parent_synced=running=true;lt_acquiring=lt_paused=true;
 parent_rebind_request(32000000);
 assert(parent_synced&&running&&!lt_acquiring&&!lt_paused&&train==1&&upstream_recoveries==1&&upstream_retry_at==192000000);
 parent_rebind_request(32000001);assert(upstream_recoveries==1);
 creating=true;parent_rebind_request(192000000);assert(upstream_recoveries==1);
 creating=false;lt_conn=&train;parent_rebind_request(192000000);assert(upstream_recoveries==1);
 lt_conn=NULL;stop_fails=true;lt_acquiring=true;
 local_clock.count=2;fit.valid=true;discipline_at=123;sent_count=2;
 parent_rebind_request(192000000);
 assert(upstream_recoveries==2&&!lt_acquiring&&parent_synced&&running&&train==1);
 assert(local_clock.count==2&&fit.valid&&discipline_at==123&&sent_count==2);
 // PA establishment can fail repeatedly while the selected binding stays live.
 // Neither cancellation nor the old two-second correction watchdog may force
 // a GATT renewal into the next PA acquisition opportunity.
 parent_synced=false;upstream_retry_at=0;lt_acquiring=false;
 lt_renew_at=UINT64_C(4800000000);last_context_progress=16000000;
 uint64_t renew=lt_renew_at;unsigned recoveries=upstream_recoveries;
 for(unsigned attempt=0;attempt<4;attempt++) {
  creating=true;sync_started=1000000+attempt*2000000;fake_now=sync_started+1499999;
  assert(sync_acquisition_poll()&&creating&&cancel_calls==attempt);
  fake_now++;uint64_t until=sync_started+2000000;
  assert(sync_acquisition_poll()&&!creating&&cancel_until==until&&cancel_calls==attempt+1);
  assert(lt_renew_at==renew);
  parent_recovery_poll(fake_now*16);
  assert(lt_renew_at==renew&&upstream_recoveries==recoveries+attempt+1);
 }
 // Lost established PA also retries within the binding; actual permission
 // expiry must still request a fresh authenticated challenge.
 parent_recovery_poll(UINT64_C(1600000000));assert(lt_renew_at==renew);
 admission_live=false;parent_recovery_poll(UINT64_C(1600000000));
 assert(!lt_renew_at&&upstream_recoveries==recoveries+5);
 // Established PA with no corrected-time progress still uses bounded rebind.
 admission_live=parent_synced=true;lt_renew_at=renew;upstream_retry_at=0;
 parent_recovery_poll(last_context_progress+31999999);assert(lt_renew_at==renew);
 parent_recovery_poll(last_context_progress+32000000);assert(!lt_renew_at);
 // Useful time, an active ACL and an in-progress PA attempt preserve ownership.
 lt_renew_at=renew;upstream_retry_at=0;upstream_usable=true;
 parent_recovery_poll(UINT64_C(1600000000));assert(lt_renew_at==renew);
 upstream_usable=false;lt_conn=&train;parent_recovery_poll(UINT64_C(1600000000));assert(lt_renew_at==renew);
 lt_conn=NULL;creating=true;parent_recovery_poll(UINT64_C(1600000000));assert(lt_renew_at==renew);
 // A failed cancellation fails closed at the original deadline, with no renew.
 sync_started=10000000;fake_now=11500000;drain_ok=false;
 assert(!sync_acquisition_poll()&&faulted&&creating&&lt_renew_at==renew&&cancel_until==12000000);
 puts("PASS actual RC handoff/rebind and PA retry: live binding keeps renewal deadline, expired binding and stalled established stream rebind, original cancellation deadline fails closed");
 return 0;
}

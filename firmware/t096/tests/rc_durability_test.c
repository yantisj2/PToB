/* Actual parent reset/rebind functions, with fake public controller boundaries. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "rc_dynamic.h"
#include "rc_gm_adapter.h"
#include "rc_stream_quality.h"
static struct rc7_receiver receiver;
static struct rc5_gm_context gm_context;
static struct r2_link_quality delivery;
static struct rc5_rate_fit fit;
static struct rc5_clock local_clock;
static struct rc7_path upstream_path,publication_path;
static struct rc5_origin current_origin;
static uint8_t current_proof[16];
static uint64_t lt_renew_at,lt_budget_until,lt_attempt_ready,lt_next,discipline_at,usable_tx_from,phase_rx,requested,restart_at,start_delay,upstream_retry_at;
static uint64_t rebind_parent_boot,rebind_root_boot,fit_parent_boot,fit_root_boot;
static uint32_t rebind_root_epoch,rebind_capture_epoch,fit_parent_epoch,fit_root_epoch,fit_rms;
static unsigned sent_count,lt_attempts,holdover_expiries,upstream_recoveries,train=1;
static bool lt_acquiring,lt_paused,phase_admitted,start_prepared,awaiting_first_tx,holdover_expired,fit_rms_valid;
static bool parent_known,parent_synced,creating,running,stop_fails,faulted;
static void *lt_conn;
#define RC_FIELD_HOLD_TICKS UINT64_C(8000000)
static bool stop(void){if(stop_fails)return false;running=false;start_prepared=false;return true;}
static void check(bool ok,const char *reason){(void)reason;if(!ok)faulted=true;}
static void lt_quiesce(bool pause){lt_paused=pause;}
static void r2_shadow_normal_withdraw(void){}
#include "rc_durability_under_test.h"
int main(void){
 lt_acquiring=lt_paused=phase_admitted=start_prepared=awaiting_first_tx=holdover_expired=fit_rms_valid=true;
 discipline_at=phase_rx=requested=restart_at=start_delay=upstream_retry_at=123;
 receiver.clock.bound=true;local_clock.count=2;sent_count=2;lt_attempts=3;current_origin.expires=100;
 memset(current_proof,1,sizeof current_proof);
 parent_handoff_reset();
 assert(!lt_acquiring&&!lt_paused&&!phase_admitted&&!start_prepared&&!awaiting_first_tx&&!holdover_expired&&!fit_rms_valid);
 assert(!discipline_at&&!phase_rx&&!requested&&!restart_at&&!start_delay&&!upstream_retry_at&&!receiver.clock.bound&&!local_clock.count&&!sent_count&&!lt_attempts&&!current_origin.expires);
 for(unsigned i=0;i<sizeof current_proof;i++)assert(!current_proof[i]);
 parent_known=parent_synced=running=true;lt_acquiring=lt_paused=true;
 parent_rebind_request(32000000);
 assert(parent_synced&&!running&&!lt_acquiring&&!lt_paused&&train==2&&upstream_recoveries==1&&upstream_retry_at==192000000);
 parent_rebind_request(32000001);assert(upstream_recoveries==1);
 creating=true;parent_rebind_request(192000000);assert(upstream_recoveries==1);
 creating=false;lt_conn=&train;parent_rebind_request(192000000);assert(upstream_recoveries==1);
 lt_conn=NULL;stop_fails=true;lt_acquiring=true;parent_rebind_request(192000000);assert(upstream_recoveries==1&&lt_acquiring);
 stop_fails=false;parent_rebind_request(192000000);assert(upstream_recoveries==2&&!lt_acquiring&&parent_synced);
 running=true;train=UINT32_MAX;parent_rebind_request(352000000);assert(faulted&&upstream_recoveries==2);
 puts("PASS actual RC handoff clears phase/admission state; same-parent rebind preserves RX sync, drains publisher, fences train, throttles retries and respects ACL/create/drain ownership");
 return 0;
}

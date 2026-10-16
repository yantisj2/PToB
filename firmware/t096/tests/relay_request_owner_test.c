/* Exact production request API, with no controller resource accessible. */
#include "relay_admission.h"
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#define R2_REPLY_MS 1000
typedef int k_spinlock_key_t;
static struct {bool configured,closing,closed,relay_pending;int error;
 uint64_t deadline,relay_session;enum r2_relay_pending_reason relay_pending_reason;} status;
static struct {uint64_t identity[4];} roster;
static struct r2_relay_request relay_request;
static bool started,relay_requested,relay_transition,relay_selected,relay_cancelled;
static uint64_t relay_session,current;
static int lock;
static uint64_t now_us(void){return current;}
static int k_spin_lock(int *x){return 0;}
static void k_spin_unlock(int *x,int k){}
static int r2_group_slot(uint64_t *ids,uint64_t id){return 0;}
#include "relay_request_under_test.h"
int main(void){
 status.configured=started=true;status.deadline=123456789;current=1000000;
 assert(!r2_service_relay(1,10,1000));assert(relay_request.pending&&relay_request.expires==6000000&&relay_request.activation_until==11000000);
 current=1100000;assert(!r2_service_relay(1,12,1100));assert(relay_request.expires==6000000&&relay_request.activation_until==11000000);
 assert(!r2_service_relay(2,1,1100));assert(relay_request.session==2&&relay_request.expires==2100000);
 assert(r2_service_relay_cancel(1)==-ESTALE);assert(!r2_service_relay_cancel(2)&&!relay_request.pending);
 assert(!r2_service_relay(3,0,1100));assert(relay_request.pending&&relay_request.expires==6100000&&!relay_request.activation_until);
 relay_requested=true;assert(r2_service_relay_cancel(3)==-EBUSY&&!relay_cancelled);
 relay_requested=false;relay_transition=true;assert(r2_service_relay_cancel(3)==-EBUSY);
 relay_transition=false;relay_selected=true;assert(r2_service_relay_cancel(3)==-EBUSY);
 assert(status.deadline==123456789);return 0;
}

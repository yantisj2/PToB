/* Optional activation policy, no controller resources or authority. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
enum r2_relay_pending_reason {R2_RELAY_NONE,R2_RELAY_PENDING_GM,
 R2_RELAY_PENDING_CONTROL,R2_RELAY_ADMIT,R2_RELAY_EXPIRED,R2_RELAY_CANCELLED,R2_RELAY_PENDING_MAP};
struct r2_relay_request {uint64_t session,expires,activation_until;bool pending;};
static inline enum r2_relay_pending_reason r2_relay_request_poll(
 struct r2_relay_request *r,uint64_t now,bool gm_required,bool control_busy){
 if(!r->pending)return R2_RELAY_NONE;
 if(now>=r->expires){r->pending=false;return R2_RELAY_EXPIRED;}
 if(gm_required)return R2_RELAY_PENDING_GM;
 if(control_busy)return R2_RELAY_PENDING_CONTROL;
 r->pending=false;return R2_RELAY_ADMIT;
}

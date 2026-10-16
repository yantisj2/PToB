/* SPDX-License-Identifier: Apache-2.0
 * V14 rev1: pure conservative arithmetic, independent of fitted residuals.
 * All elapsed inputs are microseconds; all bounds are nanoseconds. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <limits.h>
#define R2_V14_MEMBERSHIP_US UINT64_C(10000000)
#define R2_V14_SUSPICION_US UINT64_C(2000000)
#define R2_V14_RECOVERY_US UINT64_C(5000000)
#define R2_V14_UNKNOWN UINT32_MAX
/* FIXTURE is explicitly synthetic; PHYSICAL requires external qualification.
 * Zero-initialized native configuration is UNKNOWN and cannot grant holdover. */
enum r2_bound_provenance { R2_BOUND_UNKNOWN, R2_BOUND_FIXTURE, R2_BOUND_PHYSICAL };
struct r2_hold_bound {
 uint32_t reference_ns,transfer_ns,quantization_ns,rate_ppb,wander_ppb_per_s;
 enum r2_bound_provenance provenance;
};
static inline uint64_t r2_sat_add(uint64_t a,uint64_t b){return UINT64_MAX-a<b?UINT64_MAX:a+b;}
static inline uint64_t r2_sat_mul(uint64_t a,uint64_t b){return b&&a>UINT64_MAX/b?UINT64_MAX:a*b;}
static inline uint64_t r2_ceil_div(uint64_t a,uint64_t b){return a/b+(a%b!=0);}
/* Linear bound on rate deviation + conservative quadratic wander envelope.
 * Every intermediate saturates upwards; overflow can only refuse quality. */
static inline uint32_t r2_hold_error(const struct r2_hold_bound *b,uint64_t elapsed_us,uint32_t upstream_ns){
 if(!b||b->provenance==R2_BOUND_UNKNOWN||b->provenance>R2_BOUND_PHYSICAL||upstream_ns==R2_V14_UNKNOWN)return R2_V14_UNKNOWN;
 uint64_t e=r2_sat_add(upstream_ns,b->reference_ns);
 e=r2_sat_add(e,b->transfer_ns);e=r2_sat_add(e,b->quantization_ns);
 e=r2_sat_add(e,r2_ceil_div(r2_sat_mul(elapsed_us,b->rate_ppb),1000000));
 /* Round elapsed upwards before multiplying again, deliberately conservative. */
 uint64_t wander=r2_sat_mul(elapsed_us,b->wander_ppb_per_s);
 wander=r2_ceil_div(wander,1000000);
 wander=r2_ceil_div(r2_sat_mul(wander,elapsed_us),1000000);
 e=r2_sat_add(e,wander);
 return e>=R2_V14_UNKNOWN?R2_V14_UNKNOWN:(uint32_t)e;
}
static inline bool r2_hold_valid(const struct r2_hold_bound *b,uint64_t now_us,uint64_t reference_us,uint64_t permission_until,uint32_t upstream_ns,uint32_t budget_ns){
 if(now_us<reference_us||now_us>=permission_until||!budget_ns||budget_ns==R2_V14_UNKNOWN)return false;
 uint32_t error=r2_hold_error(b,now_us-reference_us,upstream_ns);
 return error!=R2_V14_UNKNOWN&&error<=budget_ns;
}
struct r2_recovery {uint64_t lost_at;bool losing;};
static inline void r2_recovery_update(struct r2_recovery *s,bool usable,uint64_t now){
 if(usable){s->losing=false;s->lost_at=0;}
 else if(!s->losing){s->losing=true;s->lost_at=now;}
}
static inline bool r2_recovery_retained(const struct r2_recovery *s,bool time_valid,uint64_t now){
 return time_valid||!s->losing||(now>=s->lost_at&&now-s->lost_at<R2_V14_RECOVERY_US);
}

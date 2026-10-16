/* Operator-authorized temporary empirical BLE1M receive phase correction.
 * Trained on direct TC01->TC03 independent PPS, excluding invalid reception.
 * Applied once at each T096 follower/RC delivered map; no raw tick rewriting,
 * no running witness feedback, no calibrated hardware/UTC claim. Extension to
 * other receivers/hops is the operator's provisional assumption, to be tested.
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#define RC_DIRECT_PPS_CORRECTION_NS 10543
#define RC_DIRECT_PPS_CALIBRATION_RUN "rc-offset-calibration-10min-20261012-01"
#define RC_DIRECT_PPS_CALIBRATION_SHA256 "d521620bcb29c88e934f64d1ac746cf2cf9a84ba7f489578aec13acbf3c14e66"
_Static_assert(RC_DIRECT_PPS_CORRECTION_NS>=-100000 && RC_DIRECT_PPS_CORRECTION_NS<=100000,
               "Empirical correction outside existing discipline domain");
static inline int32_t rc_direct_pps_correction(unsigned profile){
 return profile>=1&&profile<=5?RC_DIRECT_PPS_CORRECTION_NS:0;
}
static inline bool r2_empirical_phase_ns(int64_t raw,int64_t *out){
 if(!out || (RC_DIRECT_PPS_CORRECTION_NS>0&&raw>INT64_MAX-RC_DIRECT_PPS_CORRECTION_NS) ||
    (RC_DIRECT_PPS_CORRECTION_NS<0&&raw<INT64_MIN-RC_DIRECT_PPS_CORRECTION_NS))return false;
 *out=raw+RC_DIRECT_PPS_CORRECTION_NS;return true;
}

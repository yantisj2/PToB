/* Default-BMCA dataset order for distinct clocks; not PTP wire conformance. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#define R2_ELECTION_STABLE_US UINT64_C(60000000)
#define R2_ELECTION_NONE 255u
struct r2_clock_quality {
 uint8_t priority1,clock_class,accuracy,priority2;
 uint16_t variance;
 uint64_t identity;
};
struct r2_source_admission {
 uint64_t boot_at,now,stable_at,last_expiry;
 uint32_t source_epoch;
 bool observing,eligible,failed;
};
struct r2_ranked_clock {
 struct r2_clock_quality quality;
 uint64_t boot;
 uint8_t wire;
 bool gm_capable,gnss_capable,independent_ready,authenticated;
};
struct r2_elected_roles {uint8_t gm,coordinator,standby;};
/* Negative means a is preferred. Uptime/RMS are deliberately not fields. */
int r2_quality_compare(const struct r2_clock_quality *a,const struct r2_clock_quality *b);
void r2_admission_init(struct r2_source_admission *s,uint64_t now);
bool r2_admission_update(struct r2_source_admission *s,bool gm_capable,bool gnss_capable,
 bool independent_ready,uint32_t source_epoch,uint64_t original_expiry,uint64_t now);
struct r2_elected_roles r2_election_rank(const struct r2_ranked_clock *clocks,unsigned count);

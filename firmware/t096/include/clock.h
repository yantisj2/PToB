/* SPDX-License-Identifier: Apache-2.0 */
#ifndef R2_CLOCK_H
#define R2_CLOCK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Software UTC estimate only. Does not steer Zephyr uptime or CLOCK hardware. */
#define R2_GPS_FRESH_US INT64_C(2500000)
#define R2_PPS_FRESH_US INT64_C(1500000)
#define R2_HOLDOVER_US INT64_C(60000000)
#define R2_INPUT_MAX_AGE_US INT64_C(250000)
#define R2_SLEW_PPB 5000000 /* 0.5%; output always advances, including negative corrections */
#define R2_RATE_LIMIT_PPB 2000000
#define R2_CLOCK_MAX_US (INT64_MAX/4)
#define R2_GGA_HDOP_UNKNOWN UINT16_MAX
enum r2_clock_state { R2_TIME_UNSET, R2_TIME_ACQUIRING, R2_TIME_COARSE,
  R2_TIME_DISCIPLINING, R2_TIME_SYNCED, R2_TIME_HOLDOVER, R2_TIME_EXPIRED, R2_TIME_FAULT };
enum r2_association { R2_ASSOC_UNKNOWN, R2_ASSOC_PRECEDING_UTC_SECOND };
enum r2_clock_fault { R2_INPUT_OK, R2_INPUT_CHECKSUM, R2_INPUT_SYNTAX, R2_INPUT_VOID,
  R2_INPUT_STALE, R2_INPUT_OVERFLOW, R2_INPUT_DRIVER, R2_INPUT_RESTART,
  R2_INPUT_JUMP, R2_INPUT_TIMEBASE, R2_INPUT_STOP };
struct r2_clock_view {
  int64_t utc_us, mono_us, gps_age_us, pps_age_us, phase_us, rx_age_us, gga_age_us;
  int32_t rate_ppb, input_error;
  uint32_t generation, source_epoch, pairs, accepted, rejected, faults;
  uint32_t pps_edges, pps_rejected, gga_count;
  unsigned state, reason, association, sats_used;
  bool valid, source_available, rate_valid, pps_present, fix_active;
  uint8_t rmc_status,rmc_mode; /* Last checksum-valid RMC fields, ASCII codes. */
  uint16_t gga_hdop_centi; /* Reported GGA HDOP only; never source/servo input. */
};
struct r2_clock {
  struct r2_clock_view view;
  /* Source-owner translation snapshot; raw servo/eligibility stay separate. */
  int64_t translated_utc_us, translated_at_us;
  uint32_t translated_epoch;
  int64_t last_tick, wall, rate_remainder, slew_remainder, phase;
  int64_t last_rmc, last_rmc_utc, last_pps, last_pair_edge, last_pair_utc;
  int64_t fit_edge, fit_utc, last_good, line_start, last_gga, last_rx;
  uint32_t run_rmc, run_pair, fit_count;
  bool mapped, paired_once, fatal, stopped, line_drop;
  char line[128]; unsigned used;
};
void r2_clock_init(struct r2_clock *c,int64_t now,enum r2_association association);
void r2_clock_source_fence(struct r2_clock *c);
void r2_clock_advance(struct r2_clock *c,int64_t now);
void r2_clock_fault(struct r2_clock *c,enum r2_clock_fault reason,int64_t now);
void r2_clock_pps(struct r2_clock *c,int64_t edge,int64_t now);
void r2_clock_bytes(struct r2_clock *c,const uint8_t *bytes,size_t size,int64_t received,int64_t now);
/* Parser returns 1 active RMC, 2 void/invalid mode RMC, 3 GGA, 0 unrelated,
 * negative for malformed/checksum-invalid input. UTC is POSIX, not TAI. */
int r2_nmea(const char *line,int64_t *utc_us,unsigned *satellites);
/* Diagnostic-only checksum-valid GGA field7; exact unsigned centi units.
 * Blank, malformed, negative, >2 decimal places or overflow returns UNKNOWN.
 * Does not change ordinary GGA/RMC acceptance or any clock qualification. */
uint16_t r2_nmea_gga_hdop(const char *line);
#endif

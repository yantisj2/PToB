/* SPDX-License-Identifier: Apache-2.0
 * PPS fit/rate port from legacy src/time_core.c; no UTC/PPS association guess,
 * hardware access or persistence. Only the portable estimator is shared.
 */
#include "precision.h"
#include <stdio.h>
#define Q UINT64_C(65536)
#define NOMINAL (R2_CAPTURE_HZ * Q)
static int32_t ppb(int64_t delta) {
  return (int32_t)(delta * INT64_C(1000000000) / (int64_t)NOMINAL);
}
void r2_pps_precision_init(struct r2_pps_precision *s) {
  memset(s, 0, sizeof(*s));
  s->rate_q16 = NOMINAL;
}
static void fit(struct r2_pps_precision *s) {
  if (s->count < R2_PPS_FIT_POINTS)
    return;
  const int64_t n = R2_PPS_FIT_POINTS;
  uint64_t origin = s->ticks[s->head];
  int64_t sy = 0, sxy = 0, sx = 0, sxx = 0;
  for (int64_t x = 0; x < n; x++) {
    int64_t y = (int64_t)(s->ticks[(s->head + x) % n] - origin) -
                x * (int64_t)R2_CAPTURE_HZ;
    sx += x;
    sxx += x * x;
    sy += y;
    sxy += x * y;
  }
  int64_t slope = (n * sxy - sx * sy) * (int64_t)Q / (n * sxx - sx * sx);
  int64_t intercept = (sy * (int64_t)Q - slope * sx) / n;
  uint64_t squares = 0;
  for (int64_t x = 0; x < n; x++) {
    int64_t y = (int64_t)(s->ticks[(s->head + x) % n] - origin) -
                x * (int64_t)R2_CAPTURE_HZ;
    int64_t residual = y - (intercept + slope * x) / (int64_t)Q;
    uint64_t square = (uint64_t)(residual * residual);
    if (square > UINT64_MAX - squares) {
      s->fit_valid = false;
      return;
    }
    squares += square;
  }
  /* Keep the old16MHz scatter convention. Overflow is unavailable, not a
   * dropped tail or saturated passing result. No detrending in causal RMS. */
  if (squares > UINT64_MAX / 390625) {
    s->fit_valid = false;
    return;
  }
  s->fit_rms_ns = r2_servo_rms_floor(squares * 390625, 100 * n);
  s->drift_ppb = ppb(slope);
  s->fit_valid = true;
}
bool r2_pps_precision_add(struct r2_pps_precision *s, struct r2_capture edge) {
  if (!edge.epoch || edge.kind != R2_CAPTURE_PPS) {
    r2_math_inc(&s->rejected);
    return false;
  }
  if (s->epoch != edge.epoch) {
    uint32_t rejected = s->rejected;
    r2_pps_precision_init(s);
    s->rejected = rejected;
    s->epoch = edge.epoch;
  }
  if (s->seen) {
    uint64_t interval = edge.tick - s->last;
    if (edge.tick <= s->last || interval <= 15990000 || interval >= 16010000) {
      uint32_t rejected = s->rejected;
      r2_pps_precision_init(s);
      s->rejected = rejected;
      r2_math_inc(&s->rejected);
      s->epoch = edge.epoch;
      s->last = edge.tick;
      s->seen = true;
      return false;
    }
    int64_t error = (int64_t)(interval * Q) - (int64_t)s->rate_q16;
    if (s->rate_valid) {
      s->last_error_ns = (int32_t)(error * 125 / (2 * (int64_t)Q));
      s->errors[s->error_head] = s->last_error_ns;
      s->error_head = (s->error_head + 1) % 32;
      if (s->error_count < 32)
        ++s->error_count;
      uint64_t squares = 0;
      s->prediction_max_ns = 0;
      for (unsigned i = 0; i < s->error_count; i++) {
        int64_t e = s->errors[i];
        uint32_t magnitude = (uint32_t)(e < 0 ? -e : e);
        squares += (uint64_t)(e * e);
        if (magnitude > s->prediction_max_ns)
          s->prediction_max_ns = magnitude;
      }
      s->prediction_rms_ns = r2_servo_rms_floor(squares, s->error_count);
      s->rate_q16 = (uint64_t)((int64_t)s->rate_q16 + error / 8);
    } else {
      s->rate_q16 = interval * Q;
      s->rate_valid = true;
    }
  }
  s->last = edge.tick;
  s->seen = true;
  s->raw_rate_ppb = ppb((int64_t)s->rate_q16 - (int64_t)NOMINAL);
  s->ticks[s->head] = edge.tick;
  s->head = (s->head + 1) % R2_PPS_FIT_POINTS;
  if (s->count < R2_PPS_FIT_POINTS)
    ++s->count;
  fit(s);
  return true;
}
int32_t r2_pps_mapping_trim_ppb(const struct r2_pps_precision *s) {
  if (!s->rate_valid)
    return 0;
  /* Inverse of measured ticks/second; fast oscillator needs negative trim. */
  return (int32_t)(((int64_t)NOMINAL - (int64_t)s->rate_q16) *
                   INT64_C(1000000000) / (int64_t)s->rate_q16);
}
void r2_precision_format(char *out, size_t capacity, uint32_t ns, bool valid) {
  if (!valid || ns == UINT32_MAX)
    (void)snprintf(out, capacity, "--");
  else if (ns < 1000)
    (void)snprintf(out, capacity, "%u ns", ns);
  else
    (void)snprintf(out, capacity, "%u.%03u us", ns / 1000, ns % 1000);
}

void r2_source_clock_init(struct r2_source_clock *s) {
  memset(s, 0, sizeof(*s));
}

/* Bounded conversion without floating point or 128-bit arithmetic on Cortex-M.
 * The intermediate remainder times 1e6 fits for the admitted oscillator range.
 */
static bool source_elapsed(uint64_t ticks, uint64_t rate, uint64_t *ns) {
  if (ticks > 24000000 || rate < UINT64_C(15990000) * Q ||
      rate > UINT64_C(16010000) * Q)
    return false;
  uint64_t scaled = ticks * Q;
  uint64_t micro = (scaled % rate) * UINT64_C(1000000);
  *ns = (scaled / rate) * UINT64_C(1000000000) + (micro / rate) * 1000 +
        (micro % rate) * 1000 / rate;
  return true;
}

/* Preserve continuity at publication while removing the accumulated phase of
 * the causal map at our own physical PPS. This is a slew, not a fitted-mean
 * subtraction or a UTC association. A5ms/s bound keeps every admitted map
 * advancing (>0.994 of nominal) even during initial/reacquired phase errors. */
#define R2_SOURCE_PHASE_SLEW_PPB INT32_C(5000000)
static bool source_map_elapsed(const struct r2_source_map *m,uint64_t ticks,uint64_t *ns){
 if(!source_elapsed(ticks,m->rate_q16,ns)||m->phase_ppb>R2_SOURCE_PHASE_SLEW_PPB||m->phase_ppb<-R2_SOURCE_PHASE_SLEW_PPB)return false;
 int64_t correction=(int64_t)*ns*m->phase_ppb/INT64_C(1000000000);
 if(correction>=0){if(*ns<(uint64_t)correction)return false;*ns-=(uint64_t)correction;}
 else{uint64_t add=(uint64_t)-correction;if(*ns>UINT64_MAX-add)return false;*ns+=add;}
 return true;
}

bool r2_source_clock_read(const struct r2_source_clock *s,
                          struct r2_capture event, uint64_t *ns,
                          uint64_t *generation) {
  if (!event.epoch || event.epoch != s->epoch ||
      (event.kind != R2_CAPTURE_PPS && event.kind != R2_CAPTURE_RADIO &&
       event.kind != R2_CAPTURE_NOW))
    return false;
  for (unsigned i = 0; i < s->count; ++i) {
    unsigned index =
        (s->head + R2_SOURCE_MAP_HISTORY - 1 - i) % R2_SOURCE_MAP_HISTORY;
    const struct r2_source_map *m = &s->maps[index];
    if (event.tick < m->from)
      continue;
    uint64_t delta;
    if (event.tick >= m->until ||
        !source_map_elapsed(m,event.tick - m->from, &delta) ||
        delta > UINT64_MAX - m->anchor_ns)
      return false;
    *ns = m->anchor_ns + delta;
    *generation = m->generation;
    return true;
  }
  return false;
}

void r2_source_clock_invalidate(struct r2_source_clock *s) {
  s->utc_bound = false;
  s->count = s->head = 0;
  s->epoch = s->last_edge = 0;
}

bool r2_source_clock_publish(struct r2_source_clock *s,
                             const struct r2_pps_precision *pps,
                             struct r2_capture available) {
  if (!pps->rate_valid || !pps->seen || !pps->epoch ||
      available.epoch != pps->epoch || available.kind != R2_CAPTURE_NOW ||
      available.tick < pps->last || available.tick - pps->last > 4000000 ||
      pps->last > UINT64_MAX - 24000000) {
    r2_source_clock_invalidate(s);
    return false;
  }
  if (s->epoch != pps->epoch)
    r2_source_clock_invalidate(s);
  uint64_t anchor, ignored;int32_t phase_ppb=0;
  if (s->count) {
    const struct r2_source_map *previous =
        &s->maps[(s->head + R2_SOURCE_MAP_HISTORY - 1) % R2_SOURCE_MAP_HISTORY];
    if (pps->last <= s->last_edge || available.tick <= previous->from)
      return false;
    uint64_t at_pps;
    if(!r2_source_clock_read(s,(struct r2_capture){pps->last,pps->epoch,R2_CAPTURE_PPS},&at_pps,&ignored)){
      r2_source_clock_invalidate(s);return false;
    }
    uint64_t fraction=at_pps%UINT64_C(1000000000);
    int64_t phase=fraction>=500000000?(int64_t)fraction-INT64_C(1000000000):(int64_t)fraction;
    phase_ppb=phase>R2_SOURCE_PHASE_SLEW_PPB?R2_SOURCE_PHASE_SLEW_PPB:phase<-R2_SOURCE_PHASE_SLEW_PPB?-R2_SOURCE_PHASE_SLEW_PPB:(int32_t)phase;
    /* Expired delivery cannot be silently bridged as continuous precision. */
    if (!r2_source_clock_read(s, available, &anchor, &ignored)) {
      r2_source_clock_invalidate(s);
      return false;
    }
  } else if (!source_elapsed(available.tick - pps->last, pps->rate_q16,
                             &anchor)) {
    return false;
  }
  if (s->generation == UINT64_MAX) {
    r2_source_clock_invalidate(s);
    return false;
  }
  s->epoch = pps->epoch;
  s->last_edge = pps->last;
  s->maps[s->head] =
      (struct r2_source_map){available.tick, pps->last + 24000000, anchor,
                             pps->rate_q16, ++s->generation,phase_ppb};
  s->head = (s->head + 1) % R2_SOURCE_MAP_HISTORY;
  if (s->count < R2_SOURCE_MAP_HISTORY)
    ++s->count;
  return true;
}

bool r2_source_clock_expiry(const struct r2_source_clock *s,struct r2_capture event,uint64_t *expires_ns) {
 if(!event.epoch||event.epoch!=s->epoch)return false;
 for(unsigned i=0;i<s->count;i++){
  const struct r2_source_map *m=&s->maps[(s->head+R2_SOURCE_MAP_HISTORY-1-i)%R2_SOURCE_MAP_HISTORY];
  if(event.tick<m->from)continue;
  uint64_t delta;
  if(event.tick>=m->until||!source_map_elapsed(m,m->until-m->from,&delta)||delta>UINT64_MAX-m->anchor_ns)return false;
  *expires_ns=m->anchor_ns+delta;return true;
 }
 return false;
}

/* The coarse observation and captured coordinate describe one bounded owner
 * sample. Ordinary UART jitter must never rewrite this offset. The existing raw-clock two-second
 * jump magnitude is the contradiction threshold, not a UTC accuracy/association bound. */
int r2_source_utc_bind(struct r2_source_clock *s,struct r2_capture event,
                       int64_t coarse_ns,uint32_t source_epoch,unsigned association){
 uint64_t ns,generation;
 if(!source_epoch||coarse_ns<0||!r2_source_clock_read(s,event,&ns,&generation)||ns>INT64_MAX)return 0;
 int64_t offset=coarse_ns-(int64_t)ns;
 if(s->utc_ever_bound&&s->utc_source_epoch==source_epoch){
  if(!s->utc_bound||s->utc_association!=association){s->utc_bound=false;return -1;}
  /* Unsigned difference also handles opposite-sign offsets without signed overflow. */
  uint64_t difference=offset>=s->utc_offset_ns?(uint64_t)offset-(uint64_t)s->utc_offset_ns:
    (uint64_t)s->utc_offset_ns-(uint64_t)offset;
  if(difference>UINT64_C(2000000000)){s->utc_bound=false;return -1;}
  return 1;
 }
 s->utc_offset_ns=offset;s->utc_source_epoch=source_epoch;s->utc_association=association;
 s->utc_bound=s->utc_ever_bound=true;return 1;
}
bool r2_source_utc_read(const struct r2_source_clock *s,struct r2_capture event,
                        uint32_t source_epoch,int64_t *utc_ns){
 uint64_t ns,generation;
 if(!s->utc_bound||source_epoch!=s->utc_source_epoch||!r2_source_clock_read(s,event,&ns,&generation)||ns>INT64_MAX)return false;
 if(s->utc_offset_ns>0&&(int64_t)ns>INT64_MAX-s->utc_offset_ns)return false;
 int64_t value=(int64_t)ns+s->utc_offset_ns;if(value<0)return false;
 *utc_ns=value;return true;
}

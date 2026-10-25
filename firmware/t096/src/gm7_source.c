/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "gm7_source.h"
#include <errno.h>
#include <limits.h>
static bool project(uint64_t utc, uint64_t at, uint64_t now, uint64_t *out) {
  if (now < at || utc > UINT64_MAX - (now - at))
    return false;
  *out = utc + now - at;
  return gm7_utc_valid(*out);
}
int gm7_source_snapshot(struct gm7_source *out, const struct r2_clock *c,
                        const struct r2_source_clock *map,
                        struct r2_capture capture, uint64_t sampled) {
  if (!out || !c || !map || capture.epoch > UINT32_MAX ||
      map->utc_association >= UINT32_MAX || sampled > INT64_MAX)
    return -EINVAL;
  struct gm7_source s = {.sampled_us = sampled,
                         .source_gen = c->view.source_epoch,
                         .capture_gen = (uint32_t)capture.epoch,
                         /* Configuration identity only:UNKNOWN association0
                          * maps to stable token1. This does not commission
                          * PPS/UTC association or confer an accuracy bound. */
                         .association_gen = (uint32_t)map->utc_association + 1,
                         .quality = {128, 2, 255, 128, 65535}};
  if (c->last_pps >= 0 && sampled >= (uint64_t)c->last_pps &&
      sampled - (uint64_t)c->last_pps < R2_PPS_FRESH_US &&
      c->view.pps_present) {
    s.pps_healthy = true;
    s.pps_at = (uint64_t)c->last_pps;
  }
  bool gps = c->last_rmc >= 0 && sampled >= (uint64_t)c->last_rmc &&
             sampled - (uint64_t)c->last_rmc < R2_GPS_FRESH_US &&
             c->view.fix_active;
  if (gps && s.pps_healthy && c->view.valid && !c->stopped && !c->fatal &&
      c->view.source_epoch && capture.epoch) {
    uint64_t gps_until = (uint64_t)c->last_rmc + R2_GPS_FRESH_US,
             pps_until = (uint64_t)c->last_pps + R2_PPS_FRESH_US;
    s.until_us = gps_until < pps_until ? gps_until : pps_until;
    s.gnss_healthy = true;
    /* Preserve the established fractional functional UTC interpretation. Fine
     * PPS-relative translation is preferred only when the actual map owns it.
     */
    int64_t translated;
    uint64_t relative, generation, expires;
    bool physical =
        capture.kind == R2_CAPTURE_NOW &&
        r2_source_clock_read(map, capture, &relative, &generation) &&
        r2_source_clock_expiry(map, capture, &expires) && expires > relative &&
        r2_source_utc_read(map, capture, c->view.source_epoch, &translated) &&
        translated >= 0;
    if (physical) {
      s.utc_us = (uint64_t)translated / 1000;
      s.utc_available = gm7_utc_valid(s.utc_us);
      s.physical_ready = s.utc_available;
    } else if (c->translated_epoch == c->view.source_epoch &&
               c->translated_at_us >= 0 && c->translated_utc_us >= 0)
      s.utc_available =
          project((uint64_t)c->translated_utc_us, (uint64_t)c->translated_at_us,
                  sampled, &s.utc_us);
    else if (c->last_pair_edge >= 0 && c->last_pair_utc > 0)
      s.utc_available =
          project((uint64_t)c->last_pair_utc, (uint64_t)c->last_pair_edge,
                  sampled, &s.utc_us);
    else if (c->last_rmc_utc > 0)
      s.utc_available = project((uint64_t)c->last_rmc_utc,
                                (uint64_t)c->last_rmc, sampled, &s.utc_us);
  }
  *out = s;
  return 0;
}
uint32_t gm7_duration_ms(uint64_t now, uint64_t since) {
  if (now < since)
    return UINT32_MAX;
  uint64_t ms = (now - since) / 1000;
  return ms >= UINT32_MAX ? UINT32_MAX - 1 : (uint32_t)ms;
}
static bool current_lineage(const struct gm7_owner *o, uint64_t now) {
  return o->scope.birth && (o->serving || o->armed ||
                            (o->scope.active != o->identity &&
                             now >= o->selected_original &&
                             now - o->selected_original < GM7_ARM_RETAIN_US));
}
static uint16_t wire_blockers(const struct gm7_owner *o, uint64_t now) {
  uint16_t b = 0;
  if (o->gm_capable && !o->source.gnss_healthy)
    b |= GM7_W_NO_GNSS;
  if (o->gm_capable && !o->source.pps_healthy)
    b |= GM7_W_NO_PPS;
  if (o->gm_capable && (o->blockers & GM7_B_DWELL))
    b |= GM7_W_DWELL;
  if (o->blockers & (GM7_B_MAP | GM7_B_LOCK | GM7_B_CORRECTION | GM7_B_PREPARE |
                     GM7_B_UNCERTAINTY))
    b |= GM7_W_SHADOW_UNREADY;
  if (o->blockers & GM7_B_NOMINATION)
    b |= GM7_W_WRONG_NOMINEE;
  if (o->blockers & GM7_B_SCOPE)
    b |= GM7_W_WRONG_BOOT_TERM;
  if ((o->blockers & GM7_B_CORRECTION) ||
      (o->source.until_us && now >= o->source.until_us) ||
      (o->selected.active && now >= o->selected_source_until))
    b |= GM7_W_STALE_EVIDENCE;
  if (o->withdrawn || (o->event.valid && o->event.negative))
    b |= GM7_W_WITHDRAWN;
  if (o->reason == GM7_R_MERGE)
    b |= GM7_W_MERGE_LOST;
  if (o->blockers & GM7_B_UTC)
    b |= GM7_W_UTC_UNAVAILABLE;
  if (o->blockers & (GM7_B_PHASE | GM7_B_UNCERTAINTY))
    b |= GM7_W_ALIGNMENT_UNTRUSTED;
  if (o->blockers & GM7_B_RESOURCE)
    b |= GM7_W_RESOURCE_LIMIT;
  /* Calendar inhibition has no wire blocker assignment. Its distinct reason
   * belongs to the versioned JSON snapshot, never an invented wire meaning. */
  return b;
}
int gm7_role_snapshot(struct gm7_owner *o, struct gm7_packet *p, uint64_t now) {
  if (!o || !p || o->failed || o->stopped || now < o->now ||
      o->role_sequence == UINT64_MAX)
    return -ESTALE;
  *p = (struct gm7_packet){.group = o->group.id,
                           .key_epoch = o->group.epoch,
                           .kind = GM7_ROLE};
  struct gm7_role *r = &p->body.role;
  r->sender = o->identity;
  r->boot = o->boot;
  r->sequence = ++o->role_sequence;
  r->sampled_mono_us = now;
  r->valid_us = 4500000;
  if (current_lineage(o, now)) {
    r->birth_us = o->scope.birth;
    r->founder = o->scope.founder;
    r->term = o->scope.term;
    r->active = o->scope.active;
    r->active_boot = o->scope.boot;
  }
  /* Optional ROLE describes the ordinary ANN/STATUS scope only; it grants
   * neither timing permission nor HOT authority. */
  r->timing_epoch = o->scope.timing_epoch;
  r->hot = o->serving ? o->nominee : o->armed ? o->identity : 0;
  r->hot_boot = o->serving ? o->nominee_boot : o->armed ? o->boot : 0;
  r->source_gen = o->source.source_gen;
  r->capture_gen = o->source.capture_gen;
  r->train_gen = o->serving                  ? o->scope.train
                 : (o->prepared || o->armed) ? o->scope.train
                                             : 0;
  r->shadow_gen = (o->prepared || o->armed) ? o->shadow_gen : 0;
  r->role = !o->gm_capable             ? GM7_W_RC
            : o->armed                 ? GM7_W_HOT
            : gm7_owner_output(o, now) ? GM7_W_ACTIVE
            : (o->selected.hot == o->identity &&
               o->selected.hot_boot == o->boot)
                ? GM7_W_PREPARING
                : GM7_W_STANDBY;
  r->blockers = wire_blockers(o, now);
  r->hot_duration_ms = r->active_duration_ms = r->pps_duration_ms = UINT32_MAX;
  if (o->armed && now >= o->armed_since) {
    r->known |= 1;
    r->hot_duration_ms = gm7_duration_ms(now, o->armed_since);
  }
  if (gm7_owner_output(o, now)) {
    r->known |= 2;
    r->active_duration_ms = gm7_duration_ms(now, o->active_since);
  }
  if (o->pps_observing && now >= o->pps_since && o->source.pps_healthy &&
      now >= o->source.pps_at && now - o->source.pps_at < R2_PPS_FRESH_US) {
    r->known |= 4;
    r->pps_duration_ms = gm7_duration_ms(now, o->pps_since);
  }
  return gm7_validate(p);
}
static uint16_t age(int64_t us) {
  if (us < 0)
    return UINT16_MAX;
  uint64_t ms = (uint64_t)us / 1000;
  return ms >= UINT16_MAX ? UINT16_MAX - 1 : (uint16_t)ms;
}
int gm7_gnss_snapshot(struct gm7_owner *o, struct gm7_packet *p,
                      const struct r2_clock_view *v, uint64_t now) {
  if (!o || !p || !v || o->failed || o->stopped || now < o->now ||
      o->gnss_sequence == UINT64_MAX)
    return -ESTALE;
  *p = (struct gm7_packet){.group = o->group.id,
                           .key_epoch = o->group.epoch,
                           .kind = GM7_GNSS};
  struct gm7_gnss *r = &p->body.gnss;
  r->sender = o->identity;
  r->boot = o->boot;
  r->sequence = ++o->gnss_sequence;
  r->sampled_mono_us = now;
  r->valid_us = 30000000;
  if (current_lineage(o, now)) {
    r->birth_us = o->scope.birth;
    r->founder = o->scope.founder;
    r->term = o->scope.term;
    r->active = o->scope.active;
    r->active_boot = o->scope.boot;
  }
  r->timing_epoch = o->scope.timing_epoch;
  r->source_gen = o->source.source_gen;
  r->capture_gen = o->source.capture_gen;
  r->train_gen = o->serving                  ? o->scope.train
                 : (o->prepared || o->armed) ? o->scope.train
                                             : 0;
  r->known = 4 | 16;
  r->sats = UINT8_MAX;
  r->hdop_centi = UINT16_MAX;
  r->utc_error_us = UINT32_MAX;
  r->rmc_age_ms = age(v->gps_age_us);
  r->pps_age_ms = age(v->pps_age_us);
  r->gga_age_ms = age(v->gga_age_us);
  if (v->gps_age_us >= 0) {
    r->known |= 1;
    if (v->fix_active && v->gps_age_us < R2_GPS_FRESH_US)
      r->values |= 1;
  }
  if (v->pps_age_us >= 0) {
    r->known |= 2;
    if (v->pps_present && v->pps_age_us < R2_PPS_FRESH_US)
      r->values |= 2;
  }
  if (o->eligible && (r->values & 3) == 3)
    r->values |= 4;
  if (v->gga_age_us >= 0 && v->gga_age_us < 4000000) {
    if (v->sats_used <= 99) {
      r->known |= 8;
      r->sats = (uint8_t)v->sats_used;
    }
    if (v->gga_hdop_centi != UINT16_MAX) {
      r->known |= 32;
      r->hdop_centi = v->gga_hdop_centi;
    }
  }
  r->priority1 = o->source.quality.priority1;
  r->clock_class = o->eligible ? o->source.quality.clock_class : 255;
  r->accuracy = o->source.quality.accuracy;
  r->priority2 = o->source.quality.priority2;
  r->variance = o->source.quality.variance;
  return gm7_validate(p);
}

void gm7_legacy_gnss(struct r2_gnss_record *r, const struct gm7_gnss *g,
                     uint64_t sampled) {
  *r = (struct r2_gnss_record){.root = g->sender,
                               .root_boot = g->boot,
                               .mesh_epoch = g->timing_epoch,
                               .sampled_us = sampled,
                               .valid_us = R2_GNSS_MAX_VALID_US,
                               .source_kind = R2_GNSS_SOURCE_LOCAL,
                               .priority1 = 128,
                               .clock_class = 248,
                               .accuracy = 254,
                               .priority2 = 128,
                               .variance = UINT16_MAX,
                               .sats = R2_GNSS_SATS_UNKNOWN,
                               .hdop_centi = R2_GNSS_HDOP_UNKNOWN,
                               .rmc_age_ms = g->rmc_age_ms,
                               .pps_age_ms = g->pps_age_ms,
                               .gga_age_ms = g->gga_age_ms};
  if (g->known & 1)
    r->known |= R2_GNSS_K_FIX;
  if (g->known & 2)
    r->known |= R2_GNSS_K_PPS;
  if (g->known & 4)
    r->known |= R2_GNSS_K_QUALIFIED;
  if (g->known & 8) {
    r->known |= R2_GNSS_K_SATS;
    r->sats = g->sats;
  }
  if (g->known & 32) {
    r->known |= R2_GNSS_K_HDOP;
    r->hdop_centi = g->hdop_centi;
  }
  if (g->values & 1)
    r->values |= R2_GNSS_V_FIX;
  if (g->values & 2)
    r->values |= R2_GNSS_V_PPS;
  if (g->values & 4)
    r->values |= R2_GNSS_V_QUALIFIED;
}

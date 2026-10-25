/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "gm7_codec.h"
#ifdef __cplusplus
extern "C" {
#endif
#define GM7_FRAME_US UINT64_C(250000)
#define GM7_PRIMARY_US UINT64_C(38000)
#define GM7_SECONDARY_US UINT64_C(43000)
#define GM7_JOB_AIRTIME_US 2000u
#define GM7_JOB_LATENESS_US 2000u
#define GM7_RX_GUARD_US 8000u
#define GM7_JOB_SLOTS 3u
enum gm7_opportunity { GM7_PRIMARY, GM7_SECONDARY, GM7_STANDBY };
enum gm7_job_state {
  GM7_JOB_EMPTY,
  GM7_JOB_QUEUED,
  GM7_JOB_DISPATCHED,
  GM7_JOB_PRIMARY,
  GM7_JOB_COMPLETE,
  GM7_JOB_ABORTED
};
struct gm7_window {
  uint64_t nominal, latest_start, latest_end;
  uint32_t enclosure_us;
  uint8_t channel;
  bool physical;
};
struct gm7_job {
  struct gm7_window window;
  uint64_t ticket, sequence;
  /* Disjoint immutable job kinds: REQUEST is never positive and has no source
   * permission. No retry-phase/UTC state is retained in a completed job. */
  uint64_t source_until;
  uint64_t request_deadline, first_start, final_end;
  uint32_t generation, buffer_version;
  uint16_t length;
  uint8_t pdu_prefix[12], ad[GM7_MAX_AD_BYTES], kind, opportunity, handle,
      state;
  bool positive, negative, aux_seen;
};
struct gm7_scheduler {
  struct gm7_job job[GM7_JOB_SLOTS];
  uint64_t next_ticket, last_now;
  uint32_t generation, next_buffer, late, aborted, completed, missed,
      stale_callbacks, queue_peak;
  bool failed;
};
int gm7_status_jitter(uint64_t identity, uint64_t boot, uint64_t cycle);
unsigned gm7_status_slot(uint64_t identity, uint64_t boot, uint64_t cycle);
int gm7_status_offset(uint32_t *offset, uint64_t identity, uint64_t boot,
                      uint64_t cycle, int jitter);
uint32_t gm7_discovery_offset(uint64_t identity, uint64_t boot, uint64_t frame);
int gm7_response_window(struct gm7_window *, uint64_t request_rx_mono,
                        uint64_t local_frame_start,
                        uint64_t processing_now,
                        uint32_t phase_enclosure_us);
/* Complete primary+gap+AUX airtime model, not measured controller performance.
 * Runtime hooks must also validate these exact unchained headers/LE2M PHY. */
int gm7_complete_airtime(uint32_t *us, size_t ad_length,
                         unsigned primary_payload, unsigned auxiliary_header,
                         unsigned primary_channels, unsigned secondary_phy);
void gm7_scheduler_init(struct gm7_scheduler *, uint64_t now);
int gm7_scheduler_submit(struct gm7_scheduler *, unsigned opportunity,
                         const struct gm7_packet *,
                         const struct r2_group_config *, struct gm7_window,
                         uint64_t source_until, uint64_t request_deadline,
                         uint8_t handle, uint64_t now, uint64_t *ticket);
/* Callers serialize these bodies with the controller callbacks. An HCI enqueue
 * does not call started()/complete() and supplies no RF completion evidence. */
int gm7_scheduler_dispatch(struct gm7_scheduler *, unsigned opportunity,
                           bool source_live, uint64_t now);
bool gm7_scheduler_aux_allowed(struct gm7_scheduler *, unsigned opportunity,
                               uint64_t ticket, uint32_t generation,
                               uint32_t buffer_version, uint64_t now,
                               uint32_t remaining_airtime_us, bool source_live);
int gm7_scheduler_started(struct gm7_scheduler *, unsigned opportunity,
                          uint64_t ticket, uint32_t generation,
                          uint32_t buffer_version, uint64_t actual_first_start);
int gm7_scheduler_complete(struct gm7_scheduler *, unsigned opportunity,
                           uint64_t ticket, uint32_t generation,
                           uint32_t buffer_version,
                           uint64_t actual_final_aux_end, bool crc_complete);
void gm7_scheduler_cancel_positive(struct gm7_scheduler *, uint64_t now);
void gm7_scheduler_abort(struct gm7_scheduler *, unsigned opportunity,
                         uint64_t ticket, uint64_t now);
int gm7_scheduler_release(struct gm7_scheduler *, unsigned opportunity,
                          uint64_t ticket, bool controller_drained,
                          uint64_t now);
#ifdef __cplusplus
}
#endif

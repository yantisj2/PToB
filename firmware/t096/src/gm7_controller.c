/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Dedicated GM7 complete-event owner on the pinned Zephyr split controller.
 * No HCI advertising interval, random advertising delay or independent AUX
 * ticker participates. Existing G1/ACL/RC controller owners are unchanged.
 */
/* Controller private headers intentionally follow their dependency order. */
// clang-format off
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>
#include <string.h>
#include "hal/cpu.h"
#include "hal/ccm.h"
#include "hal/radio.h"
#include "hal/ticker.h"
#include "util/util.h"
#include "util/mem.h"
#include "util/memq.h"
#include "util/mayfly.h"
#include "ticker/ticker.h"
#include "pdu_df.h"
#include "pdu_vendor.h"
#include "pdu.h"
#include "lll.h"
#include "lll_vendor.h"
#include "lll_clock.h"
#include "lll_chan.h"
#include "lll_internal.h"
#include "ull_internal.h"
#include "hal/debug.h"
#include "gm7_controller.h"
#include "gm7_clock.h"
#include "../protocol/gm7_phase.h"
// clang-format on
BUILD_ASSERT(IS_ENABLED(CONFIG_SOC_NRF52840) &&
             IS_ENABLED(CONFIG_BT_CTLR_ADV_EXT) &&
             IS_ENABLED(CONFIG_BT_OBSERVER));
/* Management-only LE2M uses the nRF52840 HAL. Keep public controller2M
 * feature/default masks OFF so normal ACL/RC PHY behavior stays unchanged. */
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_CTLR_PHY_2M));
BUILD_ASSERT(IS_ENABLED(CONFIG_CLOCK_CONTROL_NRF_K32SRC_500PPM));
BUILD_ASSERT(GM7_JOB_SLOTS == 3);
/* ull.c materializer reserves three IDs immediately before FLASH/COEX.
 * The ordinary advertising/sync/connection IDs and counts are preserved. */
struct gm7_lll {
  struct lll_hdr hdr;
};
struct gm7_native_job {
  struct ull_hdr ull;
  struct gm7_lll lll;
  struct lll_prepare_param prepare;
  memq_link_t link;
  struct mayfly mayfly;
  struct gm7_job *job;
  struct r2_gm7_tx result;
  uint32_t cancel_generation;
  uint8_t primary[15], channel, slot;
  uint32_t queued, ready, owned;
  bool auxiliary, aborted, initialized;
};
static struct gm7_native_job native[GM7_JOB_SLOTS];
static uint32_t generation = 1, source_live, source_end, source_epoch,
                scan_enabled, scan_epoch;
static uint64_t hf_now(uint32_t *epoch) {
  uint64_t now;
  return r2_gm7_clock_now(&now, epoch) ? now : UINT64_MAX;
}
static bool live(const struct gm7_native_job *n, uint64_t now, uint32_t epoch,
                 uint32_t remaining) {
  if (now == UINT64_MAX || epoch != n->result.epoch ||
      __atomic_load_n(&n->owned, __ATOMIC_ACQUIRE) != 1)
    return false;
  const struct gm7_job *j = n->job;
  if (now > j->window.latest_end ||
      (j->event_deadline && now >= j->event_deadline))
    return false;
  if (j->positive) {
    if (now >= j->source_until || j->source_until - now <= remaining)
      return false;
    if (n->cancel_generation !=
            __atomic_load_n(&generation, __ATOMIC_ACQUIRE) ||
        !__atomic_load_n(&source_live, __ATOMIC_ACQUIRE) ||
        epoch != source_epoch)
      return false;
    uint32_t current = (uint32_t)(now * 16);
    int32_t available = (int32_t)(source_end - current);
    if (available <= 0 || (uint32_t)available <= (remaining + 2u) * 16u)
      return false;
  }
  return true;
}
static void finish_isr(void *param);
static void auxiliary_isr(void *param);
static int prepare_radio(struct lll_prepare_param *p);
static void abort_radio(struct lll_prepare_param *p, void *param) {
  struct gm7_lll *lll = p ? p->param : param;
  struct gm7_native_job *n = lll->hdr.parent;
  n->aborted = true;
  r2_gm7_capture_abort(lll);
  if (!p) {
    radio_isr_set(finish_isr, lll);
    radio_disable();
    return;
  }
  (void)lll_hfclock_off();
  __atomic_store_n(&n->queued, 0, __ATOMIC_RELEASE);
  lll_done(param);
}
static int preempt(void *next, void *current, lll_prepare_cb_t *resume) {
  /* G1's existing precision priority wins conflicts. Management reports a
   * dropped job and never steals or extends a timing reservation. */
  return lll_is_abort_cb(next, current, resume);
}
static void prepare_lll(void *param) {
  int rc = lll_hfclock_on();
  LL_ASSERT(rc >= 0);
  rc = lll_prepare(preempt, abort_radio, prepare_radio, 0, param);
  LL_ASSERT(!rc || rc == -EINPROGRESS);
}
static void gm7_ticker_job(uint32_t ticks, uint32_t drift, uint32_t remainder,
                           uint16_t lazy, uint8_t force, void *param) {
  struct gm7_native_job *n = param;
  (void)drift;
  if (!__atomic_load_n(&n->owned, __ATOMIC_ACQUIRE))
    return;
  if (lazy) {
    n->aborted = true;
    __atomic_store_n(&n->ready, 1, __ATOMIC_RELEASE);
    return;
  }
  ull_ref_inc(&n->ull);
  n->prepare = (struct lll_prepare_param){.ticks_at_expire = ticks,
                                          .remainder = remainder,
                                          .lazy = 0,
                                          .force = force,
                                          .param = &n->lll};
  __atomic_store_n(&n->queued, 1, __ATOMIC_RELEASE);
  uint32_t rc = mayfly_enqueue(TICKER_USER_ID_ULL_HIGH, TICKER_USER_ID_LLL, 0,
                               &n->mayfly);
  LL_ASSERT(!rc);
}
static int prepare_radio(struct lll_prepare_param *p) {
  struct gm7_lll *lll = p->param;
  struct gm7_native_job *n = lll->hdr.parent;
  uint32_t epoch;
  uint64_t now = hf_now(&epoch);
  const struct gm7_job *j = n->job;
  __atomic_store_n(&n->queued, 0, __ATOMIC_RELEASE);
  if (!live(n, now, epoch,
            now <= j->window.latest_end ? (uint32_t)(j->window.latest_end - now)
                                        : UINT32_MAX) ||
      now > j->window.latest_start) {
    n->aborted = true;
    radio_isr_set(finish_isr, lll);
    radio_disable();
    return 0;
  }
  radio_reset();
  radio_tx_power_set(RADIO_TXP_DEFAULT);
  radio_phy_set(1, 0);
  radio_pkt_configure(RADIO_PKT_CONF_LENGTH_8BIT, PDU_AC_PAYLOAD_SIZE_MAX,
                      RADIO_PKT_CONF_PHY(1));
  uint32_t aa = sys_cpu_to_le32(PDU_AC_ACCESS_ADDR);
  radio_aa_set((uint8_t *)&aa);
  radio_crc_configure(PDU_CRC_POLYNOMIAL, PDU_AC_CRC_IV);
  lll_chan_set(37);
  radio_pkt_tx_set(n->primary);
  radio_isr_set(auxiliary_isr, lll);
  radio_tmr_tifs_set(GM7_AUX_GAP_US);
  radio_switch_complete_and_b2b_tx(1, 0, 2, 0);
  if (!r2_gm7_capture_arm(lll, true, false)) {
    n->aborted = true;
    radio_isr_set(finish_isr, lll);
    radio_disable();
    return 0;
  }
  uint32_t at = p->ticks_at_expire + lll_event_offset_get(&n->ull) +
                HAL_TICKER_US_TO_TICKS(EVENT_OVERHEAD_START_US);
  (void)radio_tmr_start(1, at, p->remainder);
  radio_tmr_end_capture();
  int rc = lll_prepare_done(lll);
  LL_ASSERT(!rc);
  return 0;
}
/* Called before clearing RADIO events. All tokens belong to this immutable
 * slot; no callback identifies work by a reused pointer alone. */
void r2_gm7_controller_capture(void *owner, bool auxiliary, uint32_t address,
                               uint32_t end, uint32_t epoch, bool complete) {
  for (unsigned i = 0; i < GM7_JOB_SLOTS; i++)
    if (owner == &native[i].lll) {
      struct gm7_native_job *n = &native[i];
      if (!n->owned || epoch != n->result.epoch) {
        n->aborted = true;
        return;
      }
      if (auxiliary) {
        n->result.aux_address = address;
        n->result.aux_end = end;
        n->result.complete = complete;
      } else
        n->result.primary_address = address;
      if (!complete)
        n->aborted = true;
      return;
    }
}
static void auxiliary_isr(void *param) {
  struct gm7_lll *lll = param;
  struct gm7_native_job *n = lll->hdr.parent;
  bool done = radio_is_done();
  r2_gm7_capture_done(lll, done, true, n->primary);
  uint32_t epoch;
  uint64_t now = hf_now(&epoch), start_lo, start_hi;
  const struct gm7_job *j = n->job;
  bool valid =
      done && !n->aborted &&
      live(n, now, epoch,
           GM7_AUX_GAP_US + (uint32_t)(11 + 10 + j->length) * 4 + 50) &&
      r2_gm7_clock_bounds(n->result.primary_address, epoch, 40,
                          radio_tx_chain_delay_get(1, 0), &start_lo,
                          &start_hi) &&
      start_lo / 16 >= j->window.nominal &&
      (start_hi + 15) / 16 <= j->window.latest_start &&
      now <= j->window.latest_end &&
      j->window.latest_end - now >=
          GM7_AUX_GAP_US + (uint64_t)(11 + 10 + j->length) * 4 + 50;
  lll_isr_tx_status_reset();
  if (!valid) {
    n->aborted = true;
    radio_isr_set(finish_isr, lll);
    radio_switch_complete_and_b2b_tx_disable();
    radio_disable();
    return;
  }
  radio_phy_set(2, 0);
  radio_pkt_configure(RADIO_PKT_CONF_LENGTH_8BIT, PDU_AC_PAYLOAD_SIZE_MAX,
                      RADIO_PKT_CONF_PHY(2));
  lll_chan_set(n->channel);
  radio_pkt_tx_set(n->job->pdu_prefix);
  n->auxiliary = true;
  __atomic_store_n(&n->result.exposed, true, __ATOMIC_RELEASE);
  radio_isr_set(finish_isr, lll);
  radio_switch_complete_and_b2b_tx_disable();
  radio_tmr_end_capture();
  if (!r2_gm7_capture_arm(lll, true, true)) {
    n->aborted = true;
    radio_disable();
  }
}
static void finish_isr(void *param) {
  struct gm7_lll *lll = param;
  struct gm7_native_job *n = lll->hdr.parent;
  if (n->auxiliary)
    r2_gm7_capture_done(lll, radio_is_done(), true, n->job->pdu_prefix);
  else
    r2_gm7_capture_abort(lll);
  n->result.complete = n->result.complete && !n->aborted;
  lll_isr_status_reset();
  __atomic_store_n(&n->ready, 1, __ATOMIC_RELEASE);
  lll_isr_cleanup(lll);
}
int r2_gm7_controller_submit(unsigned slot, struct gm7_scheduler *scheduler,
                             const uint8_t address[6], uint64_t now) {
  if (slot >= GM7_JOB_SLOTS || !scheduler || !address)
    return -EINVAL;
  struct gm7_native_job *n = &native[slot];
  struct gm7_job *j = &scheduler->job[slot];
  if (__atomic_load_n(&n->owned, __ATOMIC_ACQUIRE) ||
      j->state != GM7_JOB_QUEUED)
    return -EBUSY;
  uint32_t epoch;
  uint64_t sampled = hf_now(&epoch);
  if (sampled == UINT64_MAX || sampled < now)
    return -ESTALE;
  if (j->window.nominal < sampled + 3000)
    return -ETIMEDOUT;
  if (j->window.nominal - sampled > 250000)
    return -EINVAL;
  if (!n->initialized) {
    n->mayfly = (struct mayfly){0, 0, &n->link, &n->prepare, prepare_lll};
    n->initialized = true;
  }
  /* The mayfly link/ack state lives for the slot's entire lifetime. Queue
   * retirement does not permit resetting a link that became a queue tail. */
  n->auxiliary = n->aborted = false;
  n->ready = n->queued = 0;
  n->slot = (uint8_t)slot;
  n->job = j;
  n->channel = (uint8_t)((j->sequence ^ j->ticket) % 37);
  int rc = gm7_air_build(n->primary, j, address, n->channel);
  if (rc)
    return rc;
  ull_hdr_init(&n->ull);
  lll_hdr_init(&n->lll, n);
  n->lll.hdr.parent = n;
  /* HDR_LLL2ULL requires the ULL header at the start of n. */
  n->ull.ticks_prepare_to_start =
      HAL_TICKER_US_TO_TICKS(EVENT_OVERHEAD_XTAL_US);
  n->ull.ticks_preempt_to_start =
      HAL_TICKER_US_TO_TICKS(EVENT_OVERHEAD_PREEMPT_MIN_US);
  n->ull.ticks_slot = HAL_TICKER_US_TO_TICKS_CEIL(
      2000 + EVENT_OVERHEAD_START_US + EVENT_OVERHEAD_END_US);
  n->result = (struct r2_gm7_tx){.ticket = j->ticket,
                                 .generation = j->generation,
                                 .buffer_version = j->buffer_version,
                                 .epoch = epoch,
                                 .slot = (uint8_t)slot};
  n->cancel_generation = __atomic_load_n(&generation, __ATOMIC_ACQUIRE);
  __atomic_store_n(&n->owned, 1, __ATOMIC_RELEASE);
  uint32_t ticker_now = ticker_ticks_now_get();
  sampled = hf_now(&epoch);
  if (sampled == UINT64_MAX || epoch != n->result.epoch) {
    n->owned = 0;
    return -ESTALE;
  }
  if (sampled >= j->window.nominal) {
    n->owned = 0;
    return -ETIME;
  }
  uint32_t advance =
      HAL_TICKER_US_TO_TICKS_CEIL(j->window.nominal + 500 - sampled);
  uint32_t offset = lll_event_offset_get(&n->ull) +
                    HAL_TICKER_US_TO_TICKS(EVENT_OVERHEAD_START_US +
                                           radio_tx_ready_delay_get(1, 0));
  if (advance <= offset) {
    n->owned = 0;
    return -ERANGE;
  }
  uint32_t volatile completion = TICKER_STATUS_BUSY;
  uint32_t status = ticker_start(
      TICKER_INSTANCE_ID_CTLR, TICKER_USER_ID_THREAD, TICKER_ID_MAX + slot,
      ticker_now + advance - offset, 0, 0, 0, 0, n->ull.ticks_slot,
      gm7_ticker_job, n, ull_ticker_status_give, (void *)&completion);
  status = ull_ticker_status_take(status, &completion);
  if (status != TICKER_STATUS_SUCCESS) {
    n->owned = 0;
    return -EBUSY;
  }
  j->state = GM7_JOB_DISPATCHED;
  return 0;
}
int r2_gm7_controller_drain(unsigned slot, uint64_t now) {
  (void)now;
  if (slot >= GM7_JOB_SLOTS)
    return -EINVAL;
  struct gm7_native_job *n = &native[slot];
  if (!n->owned)
    return 0;
  uint32_t volatile completion = TICKER_STATUS_BUSY;
  uint32_t status = ticker_stop(TICKER_INSTANCE_ID_CTLR, TICKER_USER_ID_THREAD,
                                TICKER_ID_MAX + slot, ull_ticker_status_give,
                                (void *)&completion);
  (void)ull_ticker_status_take(status, &completion);
  if (ull_ref_get(&n->ull)) {
    int rc = ull_disable(&n->lll);
    if (rc && rc != -EALREADY)
      return rc;
  }
  if (ull_ref_get(&n->ull) || __atomic_load_n(&n->queued, __ATOMIC_ACQUIRE) ||
      r2_gm7_capture_busy())
    return -EAGAIN;
  __atomic_store_n(&n->owned, 0, __ATOMIC_RELEASE);
  return 0;
}
bool r2_gm7_controller_idle(void) {
  for (unsigned i = 0; i < GM7_JOB_SLOTS; i++)
    if (native[i].owned)
      return false;
  return !r2_gm7_capture_busy();
}
void r2_gm7_controller_source(uint64_t until, bool live_source) {
  uint32_t epoch;
  uint64_t now = hf_now(&epoch);
  __atomic_store_n(&source_live, 0, __ATOMIC_RELEASE);
  if (generation != UINT32_MAX && live_source && now != UINT64_MAX &&
      until > now && until - now <= 1500000) {
    source_end = (uint32_t)(until * 16);
    source_epoch = epoch;
    __atomic_store_n(&source_live, 1, __ATOMIC_RELEASE);
  }
}
void r2_gm7_controller_cancel(uint64_t now) {
  (void)now;
  uint32_t g = __atomic_load_n(&generation, __ATOMIC_RELAXED);
  __atomic_store_n(&source_live, 0, __ATOMIC_RELEASE);
  if (g < UINT32_MAX)
    __atomic_store_n(&generation, g + 1, __ATOMIC_RELEASE);
}
bool r2_gm7_controller_result(struct r2_gm7_tx *out) {
  if (!out)
    return false;
  for (unsigned i = 0; i < GM7_JOB_SLOTS; i++)
    if (__atomic_load_n(&native[i].ready, __ATOMIC_ACQUIRE)) {
      *out = native[i].result;
      __atomic_store_n(&native[i].ready, 0, __ATOMIC_RELEASE);
      return true;
    }
  return false;
}
void r2_gm7_controller_scan(bool enabled, bool aligned, uint64_t start) {
  (void)start;
  (void)aligned;
  __atomic_store_n(&scan_enabled, 0, __ATOMIC_RELEASE);
  uint32_t epoch;
  uint64_t now = hf_now(&epoch);
  scan_epoch = now == UINT64_MAX ? 0 : epoch;
  __atomic_store_n(&scan_enabled, enabled, __ATOMIC_RELEASE);
}
bool r2_gm7_controller_scan37(void) {
  if (!__atomic_load_n(&scan_enabled, __ATOMIC_ACQUIRE))
    return false;
  uint32_t epoch;
  uint64_t now = hf_now(&epoch);
  return now != UINT64_MAX && epoch == scan_epoch;
}

bool r2_gm7_controller_receiving(void) {
  return __atomic_load_n(&scan_enabled, __ATOMIC_ACQUIRE) != 0;
}

bool r2_gm7_controller_exposed(unsigned slot, uint64_t ticket) {
  if (slot >= GM7_JOB_SLOTS)
    return true;
  const struct gm7_native_job *n = &native[slot];
  if (!__atomic_load_n(&n->owned, __ATOMIC_ACQUIRE) ||
      n->result.ticket != ticket)
    return true;
  return __atomic_load_n(&n->result.exposed, __ATOMIC_ACQUIRE);
}

static uint32_t precision_enabled, precision_epoch, precision_phase,
    precision_train, precision_bound_train, precision_handle = UINT32_MAX,
                                            precision_deadline_raw;
void r2_gm7_controller_precision(bool enable, uint32_t phase, uint64_t deadline,
                                 uint32_t train) {
  __atomic_store_n(&precision_enabled, 0, __ATOMIC_RELEASE);
  uint32_t epoch;
  uint64_t now = hf_now(&epoch);
  if (!enable || !train || phase >= 62500 || now == UINT64_MAX ||
      deadline <= now || deadline - now > 1500000)
    return;
  precision_epoch = epoch;
  precision_phase = phase;
  precision_train = train;
  precision_deadline_raw = (uint32_t)(deadline * 16);
  __atomic_store_n(&precision_enabled, 1, __ATOMIC_RELEASE);
}
void r2_gm7_controller_precision_bind(uint16_t handle, uint32_t train) {
  precision_handle = handle;
  precision_bound_train = train;
}
bool r2_gm7_precision_anchor(uint16_t handle, void *header, uint32_t *anchor,
                             uint32_t interval) {
  if (!__atomic_load_n(&precision_enabled, __ATOMIC_ACQUIRE))
    return true;
  if (!header || !anchor || handle != precision_handle ||
      precision_bound_train != precision_train || interval != 62500)
    return false;
  uint32_t epoch;
  uint64_t now = hf_now(&epoch);
  if (now == UINT64_MAX || epoch != precision_epoch)
    return false;
  struct ull_hdr *ull = header;
  uint32_t offset = lll_event_offset_get(ull) +
                    HAL_TICKER_US_TO_TICKS(EVENT_OVERHEAD_START_US +
                                           radio_tx_ready_delay_get(1, 0));
  uint32_t ticker_now = ticker_ticks_now_get();
  uint32_t delta = (*anchor + offset - ticker_now) & HAL_TICKER_CNTR_MASK;
  if (delta > HAL_TICKER_US_TO_TICKS_CEIL(250000))
    return false;
  uint64_t earliest = now + HAL_TICKER_TICKS_TO_US(delta), target;
  int32_t remaining = (int32_t)(precision_deadline_raw - (uint32_t)(now * 16));
  if (remaining <= 0)
    return false;
  uint32_t prep =
      (HAL_TICKER_TICKS_TO_US_64BIT(ull->ticks_prepare_to_start) + 1);
  uint32_t slot = (HAL_TICKER_TICKS_TO_US_64BIT(ull->ticks_slot) + 1);
  if (!gm7_phase_plan(now, earliest, precision_phase, prep, slot,
                      now + (uint32_t)remaining / 16, &target))
    return false;
  uint32_t advance = HAL_TICKER_US_TO_TICKS_CEIL(target - now);
  if (advance <= offset)
    return false;
  *anchor = ticker_now + advance - offset;
  return true;
}

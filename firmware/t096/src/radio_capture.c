/* SPDX-License-Identifier: Apache-2.0 */
#include "radio_capture.h"
#include <errno.h>
#include <hal/nrf_radio.h>
#include <hal/nrf_timer.h>
#include <nrfx_ppi.h>
#include <string.h>
#include <zephyr/irq.h>
#define R2_LOCK_UNIT 8u
#include "health_lock.h"
/* LLL producer and serialized service consumer publish only32-bit indices. */
_Static_assert(__atomic_always_lock_free(4, 0),
               "Capture queue needs lock-free indices");
static struct r2_radio_record queue[R2_RADIO_CAPTURE_QUEUE];
static uint32_t head, tail, enabled, capture_epoch;
static nrf_ppi_channel_t route;
static struct {
  void *owner;
  const uint8_t *pdu;
  uint16_t event;
  uint32_t reservation_us, prepare_us, widening_us, receive_window_us;
  uint8_t channel;
  bool tx, armed;
} active;
static uint32_t completed, aborted, invalid, overflow;
static void inc(uint32_t *p) {
  uint32_t n = __atomic_load_n(p, __ATOMIC_RELAXED);
  if (n != UINT32_MAX)
    __atomic_store_n(p, n + 1, __ATOMIC_RELAXED);
}
static bool disarm(void) {
  if (nrfx_ppi_channel_disable(route) != NRFX_SUCCESS) {
    inc(&invalid);
    return false;
  }
  active.armed = false;
  return true;
}
int r2_radio_capture_enable(uint32_t epoch, unsigned channel) {
  if (!epoch || channel >= 32)
    return -EINVAL;
  unsigned key = irq_lock();
  if (__atomic_load_n(&enabled, __ATOMIC_RELAXED) || active.armed) {
    irq_unlock(key);
    return -EBUSY;
  }
  route = (nrf_ppi_channel_t)channel;
  capture_epoch = epoch;
  head = tail = 0;
  __atomic_store_n(&enabled, 1, __ATOMIC_RELEASE);
  irq_unlock(key);
  return 0;
}
int r2_radio_capture_disable(void) {
  unsigned key = irq_lock();
  __atomic_store_n(&enabled, 0, __ATOMIC_RELEASE);
  bool armed = active.armed;
  /* Do not free or falsely close while a selected controller event owns CC1. */
  if (!armed) {
    head = tail = 0;
  }
  irq_unlock(key);
  return armed ? -EBUSY : 0;
}
void r2_radio_capture_arm(void *owner, const void *pdu, uint16_t event,
                          uint8_t channel, uint8_t phy, bool tx) {
  if (!__atomic_load_n(&enabled, __ATOMIC_ACQUIRE))
    return;
  if (active.armed) {
    disarm();
    inc(&invalid);
    return;
  }
  if (!owner || !pdu || phy != 1 || channel > 36) {
    inc(&invalid);
    return;
  }
  active.owner = owner;
  active.pdu = pdu;
  active.event = event;
  active.channel = channel;
  active.tx = tx;
  active.reservation_us = active.prepare_us = active.widening_us = active.receive_window_us = 0;
  /* CC1 is only ever written by the owned PPI event; no software capture. */
  if (nrfx_ppi_channel_enable(route) != NRFX_SUCCESS) {
    inc(&invalid);
    return;
  }
  active.armed = true;
}
void r2_radio_capture_metadata(void *owner, uint32_t reservation_us,
                                uint32_t prepare_us, uint32_t widening_us,
                                uint32_t receive_window_us) {
  if (active.armed && active.owner == owner) {
    active.reservation_us = reservation_us;
    active.prepare_us = prepare_us;
    active.widening_us = widening_us;
    active.receive_window_us = receive_window_us;
  }
}
void r2_radio_capture_abort(void *owner) {
  if (active.armed && active.owner == owner) {
    disarm();
    inc(&aborted);
  }
}
void r2_radio_capture_complete(void *owner, bool done, bool crc) {
  if (!active.armed || active.owner != owner)
    return;
  bool address = nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_ADDRESS);
  uint32_t raw = nrf_timer_cc_get(NRF_TIMER3, NRF_TIMER_CC_CHANNEL1);
  if (!disarm())
    return;
  if (!done || !address || (!active.tx && !crc)) {
    inc(&invalid);
    return;
  }
  const uint8_t *p = active.pdu;
  unsigned length = p[1], ext = length ? (p[2] & 63) : 0;
  /* Complete AUX_SYNC_IND only; no AuxPtr/chaining, CTE, ACAD or ambiguity. */
  if ((p[0] & 15) != 7 || length < 1 || ext > length - 1 ||
      (ext && (p[3] & (0x10 | 0x04))) ||
      (length - 1 - ext != R2_PRECISION_AD_BYTES &&
       length - 1 - ext != R2_RADIO_CAPTURE_AD_MAX)) {
    inc(&invalid);
    return;
  }
  const uint8_t *ad = p + 3 + ext;
  unsigned ad_length = length - 1 - ext;
  if (ad[0] != ad_length - 1 || ad[1] != 255 || ad[2] != 255 || ad[3] != 255) {
    inc(&invalid);
    return;
  }
  uint32_t h = __atomic_load_n(&head, __ATOMIC_RELAXED),
           t = __atomic_load_n(&tail, __ATOMIC_ACQUIRE);
  if (h - t >= R2_RADIO_CAPTURE_QUEUE) {
    inc(&overflow);
    return;
  }
  struct r2_radio_record *r = &queue[h % R2_RADIO_CAPTURE_QUEUE];
  r->raw = raw;
  r->epoch = capture_epoch;
  r->owner = (uintptr_t)active.owner;
  r->reservation_us = active.reservation_us;
  r->prepare_us = active.prepare_us;
  r->widening_us = active.widening_us;
  r->receive_window_us = active.receive_window_us;
  r->event = active.event;
  r->channel = active.channel;
  r->tx = active.tx;
  r->length = ad_length;
  memcpy(r->ad, ad, ad_length);
  __atomic_store_n(&head, h + 1, __ATOMIC_RELEASE);
  inc(&completed);
}
bool r2_radio_capture_pop(struct r2_radio_record *out) {
  uint32_t t = __atomic_load_n(&tail, __ATOMIC_RELAXED),
           h = __atomic_load_n(&head, __ATOMIC_ACQUIRE);
  if (t == h)
    return false;
  *out = queue[t % R2_RADIO_CAPTURE_QUEUE];
  /* Serialized consumer only. Keep the actual CC1 alongside the normalized
   * event; no ISR arithmetic, controller writes or unassociated host times. */
  out->address_raw=out->raw;out->raw=r2_radio_address_reference(out->address_raw,out->tx);
  __atomic_store_n(&tail, t + 1, __ATOMIC_RELEASE);
  return true;
}
int r2_radio_capture_flush(void) {
  unsigned key=irq_lock();
  if(active.armed){irq_unlock(key);return -EBUSY;}
  uint32_t h=__atomic_load_n(&head,__ATOMIC_ACQUIRE),t=__atomic_load_n(&tail,__ATOMIC_RELAXED);
  __atomic_store_n(&tail,h,__ATOMIC_RELEASE);
  irq_unlock(key);return (int)(h-t);
}
void r2_radio_capture_stats(struct r2_radio_capture_stats *out) {
  unsigned key = irq_lock();
  *out = (struct r2_radio_capture_stats){completed, aborted, invalid, overflow,
                                         active.armed};
  irq_unlock(key);
}

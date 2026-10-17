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
static struct r2_radio_rx_stats rx_stats;
static struct r2_rx_loss_capsule loss;
_Static_assert(sizeof loss<=160,"RX capsule budget");
static void loss_event(uint64_t tuple,uint32_t outcome,void *owner,bool freeze){
 if(loss.frozen)return;
 unsigned i=loss.sequence%8;
 loss.event[i]=tuple;loss.outcome[i]=outcome;loss.owner[i]=(uint32_t)(uintptr_t)owner;
 if(loss.sequence<UINT32_MAX)++loss.sequence;else freeze=true;
 if(loss.count<8)++loss.count;
 /* Ordinary isolated failures keep rolling; task-level discipline loss
  * freezes the preceding entries, including useful steady-state history. */
 (void)freeze;
 if(loss.sequence==UINT32_MAX)loss.frozen=1;
}
void r2_radio_capture_schedule(void *owner,uint16_t event,uint32_t lazy,uint32_t skipped,bool queued){
 if(!__atomic_load_n(&enabled,__ATOMIC_ACQUIRE))return;
 if(queued){if(loss.queued<UINT32_MAX)++loss.queued;}
 else if(lazy&&loss.lazy<UINT32_MAX)++loss.lazy;
 if(queued||lazy||skipped)loss_event(event|((uint64_t)capture_epoch<<32),
   queued?32:64,owner,true);
}
void r2_radio_capture_freeze_loss(void){
 unsigned key=irq_lock();if(loss.count)loss.frozen=1;irq_unlock(key);
}
bool r2_radio_capture_loss(struct r2_rx_loss_capsule *out){
 if(!out)return false;
 unsigned key=irq_lock();*out=loss;irq_unlock(key);return out->frozen;
}
static uint64_t rx_tuple(uint8_t site) {
  return active.event|((uint64_t)active.channel<<16)|((uint64_t)site<<24)|
    ((uint64_t)capture_epoch<<32);
}
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
  uint32_t generation=loss.generation;loss=(struct r2_rx_loss_capsule){.generation=generation==UINT32_MAX?generation:generation+1};
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
  if(!tx){inc(&rx_stats.armed);rx_stats.last_arm=rx_tuple(0);
    rx_stats.last_arm_owner=(uint32_t)(uintptr_t)owner;}
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
void r2_radio_capture_abort_site(void *owner,uint8_t site) {
  if (active.armed && active.owner == owner) {
    if(!active.tx){loss_event(rx_tuple(site),16,owner,true);inc(&rx_stats.aborted);rx_stats.last_abort=rx_tuple(site);
      rx_stats.last_abort_owner=(uint32_t)(uintptr_t)owner;}
    disarm();
    inc(&aborted);
  }
}
void r2_radio_capture_abort(void *owner) {
  r2_radio_capture_abort_site(owner,R2_CAPTURE_ABORT_UNKNOWN);
}
void r2_radio_capture_complete(void *owner, bool done, bool crc) {
  if (!active.armed || active.owner != owner)
    return;
  bool address = nrf_radio_event_check(NRF_RADIO, NRF_RADIO_EVENT_ADDRESS);
  uint32_t raw = nrf_timer_cc_get(NRF_TIMER3, NRF_TIMER_CC_CHANNEL1);
  if (!disarm())
    return;
  if(!active.tx)loss_event(rx_tuple(0),(done?1u:0u)|(address?2u:0u)|(crc?4u:0u),owner,!done||!address||!crc);
  if (!done || !address || (!active.tx && !crc)) {
    if(!active.tx){
      if(!done)inc(&rx_stats.no_done);
      else if(!address)inc(&rx_stats.no_address);
      else inc(&rx_stats.bad_crc);
    }
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
    if(!active.tx)inc(&rx_stats.malformed);
    inc(&invalid);
    return;
  }
  const uint8_t *ad = p + 3 + ext;
  unsigned ad_length = length - 1 - ext;
  if (ad[0] != ad_length - 1 || ad[1] != 255 || ad[2] != 255 || ad[3] != 255) {
    if(!active.tx)inc(&rx_stats.malformed);
    inc(&invalid);
    return;
  }
  uint32_t h = __atomic_load_n(&head, __ATOMIC_RELAXED),
           t = __atomic_load_n(&tail, __ATOMIC_ACQUIRE);
  if (h - t >= R2_RADIO_CAPTURE_QUEUE) {
    if(!active.tx)inc(&rx_stats.overflow);
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
  if(!active.tx){inc(&rx_stats.completed);rx_stats.last_complete=rx_tuple(0);}
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
  *out = (struct r2_radio_capture_stats){.completed=completed,.aborted=aborted,
    .invalid=invalid,.overflow=overflow,.armed=active.armed,.rx=rx_stats};
  irq_unlock(key);
}

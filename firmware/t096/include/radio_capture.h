/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "../protocol/precision_codec.h"
#include <stdbool.h>
#include <stdint.h>
#define R2_RADIO_CAPTURE_QUEUE 16u
#define R2_RADIO_CAPTURE_AD_MAX 190u
struct r2_radio_record {
  uint32_t raw, epoch;
  uint32_t address_raw; /* Immutable hardware CC1, before event normalization. */
  uint32_t reservation_us, prepare_us, widening_us, receive_window_us;
  uintptr_t owner;
  uint16_t event;
  uint8_t channel, length;
  bool tx;
  uint8_t ad[R2_RADIO_CAPTURE_AD_MAX];
};
/* Raw local ADDRESS event. TX/RX detector equivalence is UNCALIBRATED.
 * Neither HAL START-chain delays nor an informal detector-delay range justify
 * changing these captures. A later calibrated profile must be distinct. */
#define R2_ADDRESS_TX_REFERENCE_TICKS 0u
#define R2_ADDRESS_RX_REFERENCE_TICKS 0u
static inline uint32_t r2_radio_address_reference(uint32_t raw,bool tx){
 (void)tx;return raw;
}
struct r2_radio_capture_stats {
  uint32_t completed, aborted, invalid, overflow;
  bool armed;
};
/* PPI route must already be exclusively allocated/configured by TIMER3 owner.
 */
int r2_radio_capture_enable(uint32_t epoch, unsigned channel);
int r2_radio_capture_disable(void);
/* Serialized owner transition only. Refuses while the RADIO capture is armed;
 * an old queued owner pointer cannot cross the new public lifecycle. */
int r2_radio_capture_flush(void);
bool r2_radio_capture_pop(struct r2_radio_record *out);
void r2_radio_capture_stats(struct r2_radio_capture_stats *out);
/* Pinned LLL observation points only. One RADIO event can be armed at a time.
 * No kernel calls, dynamic allocation, crypto, flash or controller task writes.
 */
void r2_radio_capture_arm(void *owner, const void *pdu, uint16_t event,
                          uint8_t channel, uint8_t phy, bool tx);
/* Passive snapshot of selected controller reservation; never schedules events. */
void r2_radio_capture_metadata(void *owner, uint32_t reservation_us,
                                uint32_t prepare_us, uint32_t widening_us,
                                uint32_t receive_window_us);
void r2_radio_capture_complete(void *owner, bool done, bool crc);
void r2_radio_capture_abort(void *owner);

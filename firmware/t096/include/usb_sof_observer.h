/* SPDX-License-Identifier: Apache-2.0
 * Offline first-milestone component; NOT linked into a field image.
 * Caller owns synchronization and obtains raw CC3 through the capture owner.
 * This validator never supplies host frame identity, source time or authority.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#ifdef R2_USB_SOURCE_STREAM
#define R2_USB_SOF_QUEUE 1u /* immediate worker export; no unused eight-row bank */
#else
#define R2_USB_SOF_QUEUE 8u
#endif
#define R2_USB_FRAME_MASK 0x7ffu
#define R2_USB_SOF_TICKS 16000u /* Existing 16MHz TIMER3, 1ms full-speed frames. */
enum r2_usb_sof_result {
 R2_USB_SOF_ACCEPTED, R2_USB_SOF_DUPLICATE, R2_USB_SOF_RACE,
 R2_USB_SOF_DISCONTINUITY, R2_USB_SOF_FULL, R2_USB_SOF_INACTIVE
};
/* Two reads of BOTH registers must bracket the scalar snapshot. A deferred
 * USB_DC_SOF callback alone does not provide this evidence. current_tick comes
 * from the existing serialized 64-bit CC2 extension, never a software arrival.
 * usb_epoch changes on reset, suspend, disconnect, reconfigure or role change.
 */
struct r2_usb_sof_snapshot {
 uint64_t current_tick;
 uint32_t cc_before,cc_after,capture_epoch,usb_epoch;
 uint16_t frame_before,frame_after;
};
struct r2_usb_sof_record {
 uint64_t tick,ordinal;
 uint32_t capture_epoch,usb_epoch,sequence,loss_total; /* missed frames + queue drops; rejects separate */
 uint16_t frame;
};
struct r2_usb_sof_observer {
 struct r2_usb_sof_record queue[R2_USB_SOF_QUEUE];
 uint64_t last_tick,ordinal;
 uint32_t capture_epoch,usb_epoch,sequence,loss_total,races,duplicates,missed,overflow;
 uint16_t last_frame;
 uint8_t head,tail,count;
 bool active,seen;
};
void r2_usb_sof_begin(struct r2_usb_sof_observer *,uint32_t capture_epoch,uint32_t usb_epoch);
void r2_usb_sof_end(struct r2_usb_sof_observer *);
enum r2_usb_sof_result r2_usb_sof_observe(struct r2_usb_sof_observer *,const struct r2_usb_sof_snapshot *);
bool r2_usb_sof_pop(struct r2_usb_sof_observer *,struct r2_usb_sof_record *);

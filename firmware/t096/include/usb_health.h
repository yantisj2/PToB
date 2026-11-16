/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#ifndef R2_USB_HEALTH_H
#define R2_USB_HEALTH_H
#include <stdbool.h>
#include <stdint.h>
/* UHW words0..3 plus EVENTCAUSE,INTEN,HFCLKSTAT,USBREGSTATUS. Read only.
 * F bit13 is NVIC enable; bit22 is NVIC pending in observation layout V13. */
void r2_usb_debug_snapshot(uint32_t out[8]);
struct r2_usb_health {
 uint32_t progress,since,mode,frame,frame_since,frame_at;
 bool watching,frame_watching;
};
/* Queued control work, latched host activity, held DMA or inconsistent PHY
 * (excluding inaccessible low-power pull-up readback)
 * may not remain stalled for30s. Idle suspend/unplug still disarm; a suspend
 * flag cannot conceal stalled DMA or a blocked suspend callback. Select DMA
 * completion progress while owned, so unrelated worker events cannot hide it.
 * Snapshot precedes clock read; unsigned subtraction handles uptime wrap. */
static inline bool r2_usb_health_stalled(struct r2_usb_health *h,const uint32_t s[4],uint32_t now)
{
 /* A proven lifecycle failure is terminal even after physical unplug or
  * explicit detach. Idle suspend alone never sets these common-driver bits.
  * 19..21 retain the first cause:ownership/PHY-exit/parity-DMA/READY. */
 if(s[0]&0x380000u)return true;
 /* An awake, connected bus delivers SOFs independently of queued work and
  * DMA. An idle PHY can otherwise stop receiving while both queues are empty.
  * FRAMECNTR is inaccessible in suspend: either suspend indication disarms.
  * Poll gaps covering a full2048ms counter turn are ambiguous, so restart the
  * observation instead of mistaking counter aliasing for a frozen bus. */
 bool awake=(s[0]&0x3c1fu)==0x3c17u&&s[3]<=0x7ffu;
 if(!awake||!h->frame_watching||h->frame!=s[3]||(uint32_t)(now-h->frame_at)>=2048u){
  h->frame_watching=awake;h->frame=s[3];h->frame_since=now;
 }
 h->frame_at=now;
 if(h->frame_watching&&(uint32_t)(now-h->frame_since)>=30000u)return true;
 bool pending=(s[0]&5u)==5u && (s[0]&992u);
 uint32_t mode=s[0]&768u; /* switching DMA/PHY/worker observations starts a new budget */
 /* Worker traffic cannot repair a persistent physical-state contradiction. */
 uint32_t progress=(s[0]&512u)?0:s[1];
 if(!pending||!h->watching||h->progress!=progress||h->mode!=mode){
  h->watching=pending;h->mode=mode;h->progress=progress;h->since=now;return false;
 }
 return (uint32_t)(now-h->since)>=30000u;
}
#endif

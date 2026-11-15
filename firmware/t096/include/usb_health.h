/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#ifndef R2_USB_HEALTH_H
#define R2_USB_HEALTH_H
#include <stdbool.h>
#include <stdint.h>
struct r2_usb_health {uint32_t progress,since,mode;bool watching;};
/* Queued control work, latched host activity, held DMA or inconsistent PHY
 * may not remain stalled for30s. Idle suspend/unplug still disarm; a suspend
 * flag cannot conceal stalled DMA or a blocked suspend callback. Select DMA
 * completion progress while owned, so unrelated worker events cannot hide it.
 * Snapshot precedes clock read; unsigned subtraction handles uptime wrap. */
static inline bool r2_usb_health_stalled(struct r2_usb_health *h,const uint32_t s[4],uint32_t now)
{
 bool pending=(s[0]&5u)==5u && (s[0]&992u);
 uint32_t mode=s[0]&768u; /* switching DMA/PHY/worker observations starts a new budget */
 if(!pending||!h->watching||h->progress!=s[1]||h->mode!=mode){
  h->watching=pending;h->mode=mode;h->progress=s[1];h->since=now;return false;
 }
 return (uint32_t)(now-h->since)>=30000u;
}
#endif

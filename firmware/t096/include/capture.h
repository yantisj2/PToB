/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "precision.h"
struct gpio_dt_spec;
/* TIMER3 CC0=PPS, CC1=BLE ADDRESS, CC2=extension, CC3=USB SOF, CC4=LoRa, CC5=GM7 END (normal image). */
int r2_capture_start(const struct gpio_dt_spec *pps, int64_t now);
void r2_capture_poll(int64_t now);
bool r2_capture_pps(struct r2_capture *edge, int64_t now);
bool r2_capture_now(struct r2_capture *current);
/* Paired scalar current counter/monotonic sample under the capture lock.
 * Software presentation anchor, not a calibrated absolute/output timestamp. */
bool r2_capture_now_at(struct r2_capture *current,uint64_t *sampled_us);
bool r2_capture_radio(uint32_t raw,uint32_t epoch,struct r2_capture *event);
#if !defined(R2_DC_PROFILE) && !defined(R2_PC_PROFILE)
/* DIO1 must already have an allocated public GPIO-driver IN event. The
 * capture owner allocates a controller-excluding PPI route to CC4. */
int r2_capture_lora_attach(const struct gpio_dt_spec *dio,uint32_t fem_clear_task);
bool r2_capture_lora(struct r2_capture *event);
int r2_capture_lora_detach(void);
#endif
int r2_capture_stop(void);
int r2_capture_error(void);
#ifdef R2_USB_SOF_PROBE
#include "usb_sof_observer.h"
/* Stable register bracket and CC2 extension; no USB IRQ/event ownership. */
bool r2_capture_usb_sample(struct r2_usb_sof_snapshot *);
#endif

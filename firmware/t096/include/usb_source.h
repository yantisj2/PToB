/* SPDX-License-Identifier: Apache-2.0 -- counter/source evidence, no new authority. */
#pragma once
#include <stdbool.h>
#include <stdint.h>
struct r2_uc_clock;
struct r2_identity;
struct usb_setup_packet;
void r2_usb_source_init(const struct r2_identity *);
void r2_usb_source_begin(uint64_t activation,uint64_t capture_epoch);
/* Keep an already published compatible map through preparation, at its own
 * original deadlines. Incompatible or unestablished scope uses begin's fence. */
void r2_usb_source_prepare(uint64_t activation,uint64_t capture_epoch,
                           const struct r2_uc_clock *previous);
void r2_usb_source_withdraw(void);
void r2_usb_source_publish(const struct r2_uc_clock *);
int r2_usb_source_request(struct usb_setup_packet *,int32_t *,uint8_t **);
int r2_usb_anchor_request(struct usb_setup_packet *,int32_t *,uint8_t **);

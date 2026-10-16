/* SPDX-License-Identifier: Apache-2.0 -- raw diagnostic, no UTC/authority. */
#pragma once
#include <stdbool.h>
#include <zephyr/usb/usb_device.h>
struct r2_identity;
uint8_t r2_usb_reference_number(void);
void r2_usb_sof_init(const struct r2_identity *);
void r2_usb_sof_status(struct usb_cfg_data *,enum usb_dc_status_code,const uint8_t *);
int r2_usb_sof_request(struct usb_setup_packet *,int32_t *,uint8_t **);
bool r2_usb_sof_scope(uint32_t *epoch);

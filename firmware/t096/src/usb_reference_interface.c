/* SPDX-License-Identifier: Apache-2.0
 * API-access milestone: one vendor interface, zero dedicated endpoints.
 * Explicit R2_USB_SOF_PROBE additionally exposes a raw EP0 IN diagnostic.
 * Neither profile exports a source map or clock authority.
 * The existing CDC and USB driver remain their sole resource owners.
 */
#include <zephyr/usb/usb_device.h>
#ifdef R2_USB_SOF_PROBE
#include "usb_sof_probe.h"
#ifdef R2_USB_SOURCE_STREAM
#include "usb_source.h"
#include <zephyr/sys/byteorder.h>
#include <errno.h>
/* UEP1 v1: 32 bytes, six saturating transport counters, no clock authority. */
extern void r2_usb_ep0_diagnostics(uint32_t words[6]);
static uint8_t diagnostic[32];
static int ep0_diagnostic_request(struct usb_setup_packet *s,int32_t *length,uint8_t **data){
 if(s->bmRequestType!=0xc1||s->bRequest!=0x56||s->wValue||
    s->wIndex!=r2_usb_reference_number()||s->wLength!=sizeof diagnostic)return -ENOTSUP;
 uint32_t words[6];r2_usb_ep0_diagnostics(words);
 diagnostic[0]='U';diagnostic[1]='E';diagnostic[2]='P';diagnostic[3]='1';
 sys_put_le16(1,diagnostic+4);sys_put_le16(sizeof diagnostic,diagnostic+6);
 for(unsigned i=0;i<6;i++)sys_put_le32(words[i],diagnostic+8+4*i);
 *length=sizeof diagnostic;*data=diagnostic;return 0;
}
static int timing_request(struct usb_setup_packet *setup,int32_t *length,uint8_t **data){
 if(setup->bRequest==0x56)return ep0_diagnostic_request(setup,length,data);
 if(setup->bRequest==0x54||setup->bRequest==0x55)return r2_usb_anchor_request(setup,length,data);
 if(setup->bRequest==0x52||setup->bRequest==0x53)return r2_usb_source_request(setup,length,data);
 return r2_usb_sof_request(setup,length,data);
}
#endif
#endif

USBD_CLASS_DESCR_DEFINE(primary, 0) struct usb_if_descriptor r2_usb_reference_descriptor = {
 .bLength=sizeof(struct usb_if_descriptor), .bDescriptorType=USB_DESC_INTERFACE,
 .bInterfaceNumber=0, .bAlternateSetting=0, .bNumEndpoints=0,
 .bInterfaceClass=USB_BCC_VENDOR, .bInterfaceSubClass=0x54, .bInterfaceProtocol=1,
 .iInterface=0,
};
#ifdef R2_USB_SOF_PROBE
uint8_t r2_usb_reference_number(void){return r2_usb_reference_descriptor.bInterfaceNumber;}
#endif
static void interface_config(struct usb_desc_header *head,uint8_t number)
{
 (void)head;r2_usb_reference_descriptor.bInterfaceNumber=number;
}
USBD_DEFINE_CFG_DATA(r2_usb_reference_config) = {
 .usb_device_description=NULL,
 .interface_config=interface_config,
 .interface_descriptor=&r2_usb_reference_descriptor,
#ifdef R2_USB_SOF_PROBE
 .cb_usb_status=r2_usb_sof_status,
 .interface={.class_handler=NULL,.custom_handler=NULL,.vendor_handler=
#ifdef R2_USB_SOURCE_STREAM
 timing_request
#else
 r2_usb_sof_request
#endif
 },
#else
 .cb_usb_status=NULL,
 .interface={.class_handler=NULL,.custom_handler=NULL,.vendor_handler=NULL},
#endif
 .num_endpoints=0,.endpoint=NULL,
};

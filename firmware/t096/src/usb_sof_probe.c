/* SPDX-License-Identifier: Apache-2.0
 * Versioned EP0 IN diagnostic for the raw association milestone. The worker
 * exports hardware CC3 boundaries; request/callback arrivals never create time.
 * No UTC/source map is exported. Production bulk transport remains separate.
 */
#include "usb_sof_probe.h"
#include "capture.h"
#include "app.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <errno.h>
#include <string.h>
static struct k_spinlock usb_lock;
static struct r2_usb_sof_observer observer;
static uint64_t silicon,boot;
static uint32_t usb_epoch,fences;
static bool configured,suspended;
static uint8_t payload[80]; /* EP0 owner serializes requests through completion. */
bool r2_usb_sof_scope(uint32_t *epoch){
 k_spinlock_key_t key=k_spin_lock(&usb_lock);*epoch=usb_epoch;
 bool live=configured&&!suspended&&usb_epoch&&usb_epoch<UINT32_MAX;
 k_spin_unlock(&usb_lock,key);return live;
}
static void fence(void)
{
 r2_usb_sof_end(&observer);if(usb_epoch<UINT32_MAX)++usb_epoch;
 else configured=false;
 if(fences<UINT32_MAX)++fences;
}
void r2_usb_sof_init(const struct r2_identity *id)
{
 if(!id||id->result)return;
 boot=id->boot;
 for(unsigned i=0;i<16;i++)silicon=(silicon<<4)|(id->silicon[i]<='9'?(unsigned)(id->silicon[i]-'0'):(unsigned)(id->silicon[i]-'a'+10));
}
void r2_usb_sof_status(struct usb_cfg_data *cfg,enum usb_dc_status_code status,const uint8_t *param)
{
 (void)cfg;(void)param;
 k_spinlock_key_t key=k_spin_lock(&usb_lock);
 switch(status){
 case USB_DC_CONFIGURED:configured=true;suspended=false;fence();break;
 case USB_DC_RESET:case USB_DC_DISCONNECTED:configured=false;fence();break;
 case USB_DC_SUSPEND:suspended=true;fence();break;
 case USB_DC_RESUME:suspended=false;fence();break;
 default:break;
 }
 k_spin_unlock(&usb_lock,key);
}
int r2_usb_sof_request(struct usb_setup_packet *setup,int32_t *length,uint8_t **data)
{
 if(setup->bmRequestType!=0xc1||setup->bRequest!=0x51||setup->wValue||
    setup->wIndex!=r2_usb_reference_number()||setup->wLength!=sizeof(payload))return -ENOTSUP;
 struct r2_usb_sof_snapshot raw={0};struct r2_usb_sof_record record={0};
 k_spinlock_key_t key=k_spin_lock(&usb_lock);
 unsigned result=R2_USB_SOF_INACTIVE;bool valid=false;
 if(configured&&!suspended&&silicon&&boot&&usb_epoch<UINT32_MAX&&r2_capture_usb_sample(&raw)){
  if(!observer.active)r2_usb_sof_begin(&observer,raw.capture_epoch,usb_epoch);
  raw.usb_epoch=usb_epoch;result=r2_usb_sof_observe(&observer,&raw);
  if(result==R2_USB_SOF_ACCEPTED)valid=r2_usb_sof_pop(&observer,&record);
  /* A long consumer stall ends association. The next request starts fresh;
   * no arbitrary frame wrap is inferred and the fence remains visible. */
  if(result==R2_USB_SOF_DISCONTINUITY)fence();
 }
 memset(payload,0,sizeof(payload));memcpy(payload,"UFS1",4);
 sys_put_le16(1,payload+4);sys_put_le16(sizeof(payload),payload+6);
 sys_put_le64(silicon,payload+8);sys_put_le64(boot,payload+16);
 sys_put_le32(usb_epoch,payload+24);sys_put_le32(raw.capture_epoch,payload+28);
 sys_put_le32(observer.sequence,payload+32);sys_put_le32(observer.loss_total,payload+36);
 sys_put_le16(record.frame,payload+40);sys_put_le16(valid?1:0,payload+42);
 sys_put_le64(record.tick,payload+44);sys_put_le64(raw.current_tick,payload+52);
 sys_put_le32(observer.races,payload+60);sys_put_le32(observer.missed,payload+64);
 sys_put_le32(observer.overflow,payload+68);sys_put_le32(fences,payload+72);
 sys_put_le32(result,payload+76);
 k_spin_unlock(&usb_lock,key);*data=payload;*length=sizeof(payload);return 0;
}

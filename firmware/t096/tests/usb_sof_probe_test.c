/* SPDX-License-Identifier: Apache-2.0 */
#include <stdio.h>
#include "../src/usb_sof_probe.c"
uint8_t r2_usb_reference_number(void){return 2;}
static uint64_t get(const uint8_t *p,unsigned n){uint64_t v=0;for(unsigned i=0;i<n;i++)v|=(uint64_t)p[i]<<(8*i);return v;}
int main(void){
 struct r2_identity id={.silicon="1000000000000004",.boot=UINT64_C(0x123456789abcdef0)};
 r2_usb_sof_init(&id);
 struct usb_setup_packet setup={0xc1,0x51,0,2,80};int32_t length=0;uint8_t *data=NULL;
 test_raw=(struct r2_usb_sof_snapshot){.current_tick=100100,.cc_before=100000,.cc_after=100000,.frame_before=2047,.frame_after=2047,.capture_epoch=1};
 assert(!r2_usb_sof_request(&setup,&length,&data)&&get(data+42,2)==0);
 r2_usb_sof_status(NULL,USB_DC_CONFIGURED,NULL);
 assert(!r2_usb_sof_request(&setup,&length,&data)&&length==80&&!memcmp(data,"UFS1",4));
 assert(get(data+4,2)==1&&get(data+6,2)==80&&get(data+8,8)==UINT64_C(0x1000000000000004)&&get(data+16,8)==id.boot);
 assert(get(data+42,2)==1&&get(data+44,8)==100000&&get(data+52,8)==100100);
 uint32_t old_epoch=(uint32_t)get(data+24,4);
 test_raw.current_tick+=16000;test_raw.cc_before+=16000;test_raw.cc_after+=16000;test_raw.frame_before=test_raw.frame_after=0;
 assert(!r2_usb_sof_request(&setup,&length,&data)&&get(data+42,2)==1&&get(data+40,2)==0);
 assert(!r2_usb_sof_request(&setup,&length,&data)&&!get(data+42,2)&&get(data+76,4)==R2_USB_SOF_DUPLICATE);
 for(unsigned mode=0;mode<5;mode++){
  struct usb_setup_packet bad=setup;
  if(mode==0)bad.bmRequestType=0x41;else if(mode==1)bad.bRequest++;else if(mode==2)bad.wValue=1;else if(mode==3)bad.wIndex++;else bad.wLength--;
  assert(r2_usb_sof_request(&bad,&length,&data)==-ENOTSUP);
 }
 r2_usb_sof_status(NULL,USB_DC_SUSPEND,NULL);assert(!r2_usb_sof_request(&setup,&length,&data)&&!get(data+42,2));
 r2_usb_sof_status(NULL,USB_DC_RESUME,NULL);assert(!r2_usb_sof_request(&setup,&length,&data)&&get(data+42,2)&&get(data+24,4)>old_epoch);
 // >1s consumer stall must fence, then reacquire in a new USB measurement epoch.
 old_epoch=(uint32_t)get(data+24,4);test_raw.current_tick+=2048u*16000;test_raw.cc_before+=2048u*16000;test_raw.cc_after+=2048u*16000;
 assert(!r2_usb_sof_request(&setup,&length,&data)&&!get(data+42,2)&&get(data+24,4)>old_epoch);
 assert(!r2_usb_sof_request(&setup,&length,&data)&&get(data+42,2));
 r2_usb_sof_status(NULL,USB_DC_RESET,NULL);assert(!r2_usb_sof_request(&setup,&length,&data)&&!get(data+42,2));
 r2_usb_sof_status(NULL,USB_DC_CONFIGURED,NULL);assert(!r2_usb_sof_request(&setup,&length,&data)&&get(data+42,2));
 test_available=false;assert(!r2_usb_sof_request(&setup,&length,&data)&&!get(data+42,2));
 puts("PASS raw UFS1 identity/boot/length/endianness, request guards, frame wrap, stall fence, USB epochs and unavailable capture");
}

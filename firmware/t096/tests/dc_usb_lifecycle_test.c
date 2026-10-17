/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "../src/usb_sof_probe.c"
#include <assert.h>
#include <stdio.h>
static unsigned resets;
uint8_t r2_usb_reference_number(void){return 2;}
void r2_dc_usb_observation(bool valid,uint64_t tick,uint64_t ns,uint16_t frame,
 uint32_t usb,uint32_t capture,uint32_t source,uint32_t generation){assert(!valid);++resets;}
int main(void){
 uint32_t epoch;
 r2_usb_sof_status(NULL,USB_DC_CONFIGURED,NULL);assert(resets==1&&r2_usb_sof_scope(&epoch));
 uint32_t before=epoch;
 for(unsigned i=0;i<1000;i++)r2_usb_sof_status(NULL,USB_DC_SOF,NULL);
 assert(resets==1&&r2_usb_sof_scope(&epoch)&&epoch==before);
 r2_usb_sof_status(NULL,USB_DC_SUSPEND,NULL);assert(resets==2&&!r2_usb_sof_scope(&epoch));
 r2_usb_sof_status(NULL,USB_DC_RESUME,NULL);assert(resets==3&&r2_usb_sof_scope(&epoch));
 r2_usb_sof_status(NULL,USB_DC_RESET,NULL);assert(resets==4&&!r2_usb_sof_scope(&epoch));
 puts("PASS DC USB lifecycle: ordinary SOF never clears RMS; configure/suspend/resume/reset fence it");
}

#include <assert.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
struct usb_setup_packet {uint8_t bmRequestType,bRequest;uint16_t wValue,wIndex,wLength;};
static uint8_t diagnostic[32];
static uint8_t r2_usb_reference_number(void){return 2;}
static void sys_put_le16(uint16_t x,uint8_t *p){p[0]=x;p[1]=x>>8;}
static void sys_put_le32(uint32_t x,uint8_t *p){for(unsigned i=0;i<4;i++)p[i]=x>>(8*i);}
static void r2_usb_ep0_diagnostics(uint32_t words[6]){for(unsigned i=0;i<6;i++)words[i]=i==5?UINT32_MAX:i+1;}
#include "diagnostic_under_test.h"
int main(void){struct usb_setup_packet s={0xc1,0x56,0,2,32};int32_t n=-1;uint8_t *p=NULL;
 assert(!ep0_diagnostic_request(&s,&n,&p)&&n==32&&p==diagnostic&&!memcmp(p,"UEP1\1\0\40\0",8));
 for(unsigned i=0;i<5;i++)assert(p[8+4*i]==i+1&&p[9+4*i]==0&&p[10+4*i]==0&&p[11+4*i]==0);
 assert(p[28]==255&&p[29]==255&&p[30]==255&&p[31]==255);
 for(unsigned i=0;i<5;i++){struct usb_setup_packet bad=s;if(i==0)bad.wLength++;else if(i==1)bad.wIndex++;else if(i==2)bad.wValue++;else if(i==3)bad.bRequest++;else bad.bmRequestType=0x41;assert(ep0_diagnostic_request(&bad,&n,&p)==-ENOTSUP);}
 puts("PASS readonly UEP1 layout, saturation and exact request guards");}

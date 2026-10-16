/* SPDX-License-Identifier: Apache-2.0 -- public driver-boundary substitutions. */
#define nrfx_ppi_channel_alloc base_ppi_alloc
#define nrfx_ppi_channel_assign base_ppi_assign
#define nrfx_ppi_channel_enable base_ppi_enable
#define nrfx_ppi_channel_disable base_ppi_disable
#define nrfx_ppi_channel_free base_ppi_free
#define nrfx_timer_capture_get base_capture_get
#define nrfx_timer_capture_task_address_get base_task_address
#include "capture_fake.h"
#undef nrfx_ppi_channel_alloc
#undef nrfx_ppi_channel_assign
#undef nrfx_ppi_channel_enable
#undef nrfx_ppi_channel_disable
#undef nrfx_ppi_channel_free
#undef nrfx_timer_capture_get
#undef nrfx_timer_capture_task_address_get
#define R2_USB_SOF_PROBE 1
#define NRF_TIMER_CC_CHANNEL3 3
#define USBD_FRAMECNTR_FRAMECNTR_Msk 0x7ffu
#define USBD_ENABLE_ENABLE_Enabled 1
#define NRF_USBD_EVENT_SOF 1
static struct {uint32_t ENABLE,LOWPOWER,FRAMECNTR;} usb_registers={1,0,77};
#define NRF_USBD (&usb_registers)
static uint32_t usb_raw;
static bool usb_race,usb_route;
static inline uint32_t nrf_usbd_framecntr_get(void *p){assert(p==NRF_USBD);return usb_registers.FRAMECNTR;}
static inline uint32_t nrf_usbd_event_address_get(void *p,unsigned event){assert(p==NRF_USBD&&event==1);return 7890;}
static inline uint32_t nrfx_timer_capture_task_address_get(const nrfx_timer_t *t,unsigned cc){return cc==3?1237:base_task_address(t,cc);}
static inline uint32_t nrfx_timer_capture_get(const nrfx_timer_t *t,unsigned cc){if(cc!=3)return base_capture_get(t,cc);assert(timer_started);uint32_t raw=usb_raw;if(usb_race)++usb_raw;return raw;}
static inline int nrfx_ppi_channel_alloc(nrf_ppi_channel_t *ch){
 if(allocated==((1u<<5)|(1u<<6))){if(failure==15)return 1;*ch=7;allocated|=1u<<7;return 0;}return base_ppi_alloc(ch);
}
static inline int nrfx_ppi_channel_assign(nrf_ppi_channel_t ch,uint32_t event,uint32_t task){if(ch!=7)return base_ppi_assign(ch,event,task);assert(event==7890&&task==1237&&(allocated&(1u<<7)));return failure==16?1:0;}
static inline int nrfx_ppi_channel_enable(nrf_ppi_channel_t ch){if(ch!=7)return base_ppi_enable(ch);if(failure==17)return 1;usb_route=true;return 0;}
static inline int nrfx_ppi_channel_disable(nrf_ppi_channel_t ch){if(ch!=7)return base_ppi_disable(ch);if(failure==18)return 1;usb_route=false;return 0;}
static inline int nrfx_ppi_channel_free(nrf_ppi_channel_t ch){if(ch!=7)return base_ppi_free(ch);assert(!usb_route);if(failure==19)return 1;allocated&=~(1u<<7);return 0;}

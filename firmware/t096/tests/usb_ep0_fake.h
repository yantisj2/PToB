/* SPDX-License-Identifier: Apache-2.0 -- boundary fake for extracted exact bodies. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#define __ASSERT_NO_MSG(x) assert(x)
#define __ASSERT(x,...) assert(x)
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
#define K_FOREVER -1
#define USB_DC_EP_CONTROL 0
#define USB_DC_EP_BULK 1
#define USB_DC_EP_SETUP 1
#define USB_DC_EP_DATA_IN 2
#define USB_DC_EP_DATA_OUT 3
#define USB_SREQ_SET_ADDRESS 5
#define USB_REQTYPE_GET_TYPE(x) ((x)&0x60)
#define USB_REQTYPE_TYPE_STANDARD 0
#define NRF_USBD_EPIN(x) (0x80|(x))
#define NRF_USBD_EPOUT(x) (x)
#define NRF_USBD_EPIN_CHECK(x) ((x)&0x80)
#define NRF_USBD_EPOUT_CHECK(x) (!NRF_USBD_EPIN_CHECK(x))
#define NRF_USBD_COMMON_EVT_SETUP 1
#define NRF_USBD_COMMON_EP_OK 0
#define NRF_USBD_COMMON_EP_ABORTED 1
#define NRF_USBD_COMMON_EP_WAITING 2
#define NRFX_SUCCESS 0
#define NRFX_ERROR_BUSY 17
#define NRF_USBD_COMMON_EPIN0 0x80
#define NRF_USBD_COMMON_EPOUT0 0
#define USBD_BMREQUESTTYPE_DIRECTION_Msk 0x80
#define USBD_BMREQUESTTYPE_DIRECTION_HostToDevice 0
#define USBD_BMREQUESTTYPE_DIRECTION_Pos 7
#define MIN(a,b) ((a)<(b)?(a):(b))
typedef unsigned nrfx_err_t;
typedef unsigned nrf_usbd_common_ep_status_t;
struct usb_setup_packet {uint8_t bmRequestType,bRequest;uint16_t wValue,wIndex,wLength;};
typedef struct usb_setup_packet nrf_usbd_common_setup_t;
struct nrf_usbd_ep_ctx {
 struct {void (*cb)(uint8_t,int);uint32_t max_sz;bool en;uint8_t addr;int type;} cfg;
 struct {uint32_t len;uint8_t *data,*curr;} buf;
 bool read_complete,read_pending,write_in_progress,trans_zlp;
};
enum {EP_EVT_SETUP_RECV,EP_EVT_RECV_REQ,EP_EVT_RECV_COMPLETE,EP_EVT_WRITE_COMPLETE};
enum {USBD_EVT_EP};
struct usbd_ep_event {uint32_t generation;struct nrf_usbd_ep_ctx *ep;int evt_type;};
struct usbd_event {int evt_type;union {struct usbd_ep_event ep_evt;} evt;};
struct nrf_usbd_ctx {struct usb_setup_packet setup;int drv_lock;uint16_t ctrl_read_len;uint32_t ep0_generation,ep0_active_generation,ep0_diagnostics[6];};
typedef struct {int type;union {struct {uint8_t ep;unsigned status;} eptransfer;} data;} nrf_usbd_common_evt_t;
struct fake_regs {uint8_t BMREQUESTTYPE,BREQUEST,WVALUEL,WVALUEH,WINDEXL,WINDEXH,WLENGTHL,WLENGTHH;uint32_t EVENTS_SOF,INTENSET,EVENTS_EPDATA,EPDATASTATUS,EVENTS_EP0DATADONE,EVENTS_USBRESET,EVENTS_USBEVENT,EVENTS_EP0SETUP;};
static struct fake_regs regs;
#define NRF_USBD (&regs)
static struct nrf_usbd_ctx usbd_ctx;
static struct nrf_usbd_ep_ctx endpoints[3];
static uint8_t epbuf[3][64];
static struct usbd_event events[256];static unsigned allocated,queued[256],head,tail;
static unsigned irq_depth,setup_clear,data_clear,submissions,callbacks[4],out_submissions;
static bool inject_setup,write_callback,inject_mutex;
static int callback_write_result;
static uint32_t callback_write_bytes;
static uint8_t m_last_setup_dir;
static uint32_t m_ep_dma_waiting,m_ep_ready;
static void accept_setup(void);
static void ev_setup_handler(void);
static void stack_callback(uint8_t ep,int status);
static inline struct nrf_usbd_ctx *get_usbd_ctx(void){return &usbd_ctx;}
static bool dev_attached(void){return true;}
static bool dev_ready(void){return true;}
static unsigned irq_lock(void){return irq_depth++;}
static void irq_unlock(unsigned key){assert(irq_depth==key+1);irq_depth=key;}
static void k_mutex_lock(int *p,int timeout){assert(!irq_depth);(void)p;(void)timeout;if(inject_mutex){inject_mutex=false;ev_setup_handler();}}
static void k_mutex_unlock(int *p){assert(!irq_depth);(void)p;}
static struct nrf_usbd_ep_ctx *endpoint_ctx(uint8_t ep){return &endpoints[ep==0x80?0:ep==0?1:2];}
static unsigned ep_addr_to_nrfx(uint8_t ep){return ep;}
static unsigned ep2bit(unsigned ep){return ep==0x80?1:0;}
static bool usb_reqtype_is_to_device(struct usb_setup_packet *s){return !(s->bmRequestType&0x80);}
static unsigned nrf_usbd_setup_bmrequesttype_get(void *p){(void)p;return regs.BMREQUESTTYPE;}
static unsigned nrf_usbd_setup_brequest_get(void *p){(void)p;return regs.BREQUEST;}
static unsigned nrf_usbd_setup_wvalue_get(void *p){(void)p;return regs.WVALUEL|(regs.WVALUEH<<8);}
static unsigned nrf_usbd_setup_windex_get(void *p){(void)p;return regs.WINDEXL|(regs.WINDEXH<<8);}
static unsigned nrf_usbd_setup_wlength_get(void *p){(void)p;return regs.WLENGTHL|(regs.WLENGTHH<<8);}
static void nrf_usbd_common_setup_get(nrf_usbd_common_setup_t *s){*s=(struct usb_setup_packet){regs.BMREQUESTTYPE,regs.BREQUEST,nrf_usbd_setup_wvalue_get(0),nrf_usbd_setup_windex_get(0),nrf_usbd_setup_wlength_get(0)};}
static void nrf_usbd_common_setup_clear(void){++setup_clear;}
static void nrf_usbd_common_setup_data_clear(void){++data_clear;}
static unsigned nrf_usbd_common_last_setup_dir_get(void){return m_last_setup_dir;}
struct fake_transfer {uint32_t length;bool out;};
#define NRF_USBD_COMMON_TRANSFER_IN(name,data,len,flags) struct fake_transfer name={(len),false}
#define NRF_USBD_COMMON_TRANSFER_OUT(name,data,len) struct fake_transfer name={(len),true}
static nrfx_err_t nrf_usbd_common_ep_transfer(unsigned ep,struct fake_transfer *t){
 if(t->out){++out_submissions;return 0;}
 assert(ep==0x80||ep==0x81);++submissions;return 0;
}
static nrf_usbd_common_ep_status_t nrf_usbd_common_ep_status_get(unsigned ep,uint32_t *len){(void)ep;*len=0;return 0;}
static struct usbd_event *usbd_evt_alloc(void){assert(!irq_depth);assert(allocated<256);struct usbd_event *e=&events[allocated++];memset(e,0,sizeof *e);e->evt.ep_evt.generation=usbd_ctx.ep0_generation;return e;}
static void usbd_evt_put(struct usbd_event *e){assert(tail<256);queued[tail++]=(unsigned)(e-events);}
static void usbd_work_schedule(void){}
static void m_event_handler(const nrf_usbd_common_evt_t *e){assert(e->type==NRF_USBD_COMMON_EVT_SETUP);accept_setup();}
int usb_dc_ep_write(uint8_t,const uint8_t *,uint32_t,uint32_t *);
int usb_dc_ep_read_wait(uint8_t,uint8_t *,uint32_t,uint32_t *);
int usb_dc_ep_read_continue(uint8_t);

#define USBD_IRQn 39
#define USBD_INTEN_SOF_Msk 1
#define BIT(x) (1u<<(x))
#define unlikely(x) (x)
static int dma_available;
static unsigned dma_ep=0x80;
static uint32_t dma_endevent;
static volatile uint32_t *usbd_ep_to_endevent(unsigned ep){(void)ep;return &dma_endevent;}
static int k_sem_count_get(int *p){return *p;}
static void nrf_usbd_dma_finished(unsigned ep){(void)ep;dma_available=1;}
static void ev_sof_handler(void){}
static void ev_usbreset_handler(void){}
static void ev_usbevent_handler(void){}
static void usbd_dmareq_process(void){}
static void ev_epdata_handler(uint32_t bits);

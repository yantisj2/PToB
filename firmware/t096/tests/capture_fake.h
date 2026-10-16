/* SPDX-License-Identifier: Apache-2.0 -- replace only public driver boundary */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#define BUILD_ASSERT(x) _Static_assert(x,#x)
#define DT_NODELABEL(x) 0
#define DT_NODE_HAS_STATUS(n,s) 0
#define CONFIG_SOC_NRF52840 1
#define CONFIG_BT_CTLR_SW_SWITCH_SINGLE_TIMER 0
#define CONFIG_BT_CTLR_TIFS_HW 0
#define IS_ENABLED(x) x
#define EVENT_TIMER_ID 0
struct device {int unused;};static const struct device gpio1={0};
#define DEVICE_DT_GET(n) (&gpio1)
struct gpio_dt_spec {const struct device *port;unsigned pin;};
#define GPIO_INT_DISABLE 0
#define GPIO_INT_EDGE_TO_ACTIVE 1
typedef int atomic_t;
static inline bool atomic_cas(atomic_t *p,int old,int value){if(*p!=old)return false;*p=value;return true;}
static inline void atomic_clear(atomic_t *p){*p=0;}
static inline int atomic_get(const atomic_t *p){return *p;}
static inline void atomic_set(atomic_t *p,int value){*p=value;}
struct k_spinlock{int unused;};typedef int k_spinlock_key_t;
static inline int k_spin_lock(struct k_spinlock *l){(void)l;return 0;}
static inline void k_spin_unlock(struct k_spinlock *l,int key){(void)l;(void)key;}
static int64_t fake_now;
static inline int64_t k_uptime_ticks(void){return fake_now;}
static inline int64_t k_ticks_to_us_floor64(int64_t t){return t;}
static unsigned failure,releases,uninitializations;
static unsigned allocated;static bool timer_started,route_enabled;
static uint32_t raw_now,raw_pps;
struct sys_notify{int unused;};struct onoff_client{struct sys_notify notify;};
struct onoff_manager{int unused;};static struct onoff_manager mgr;
#define CLOCK_CONTROL_NRF_SUBSYS_HF 1
static inline struct onoff_manager *z_nrf_clock_control_get_onoff(int s){(void)s;return &mgr;}
static inline void sys_notify_init_spinwait(struct sys_notify *n){(void)n;}
static inline int onoff_request(struct onoff_manager *m,struct onoff_client *c){(void)m;(void)c;return 0;}
static inline int sys_notify_fetch_result(struct sys_notify *n,int *out){(void)n;if(failure==1)return -EAGAIN;*out=0;return 0;}
static inline int onoff_cancel_or_release(struct onoff_manager *m,struct onoff_client *c){(void)m;(void)c;if(failure==10)return -EIO;++releases;return 0;}
typedef struct{int unused;} nrfx_timer_t;
typedef struct{uint32_t frequency;unsigned bit_width,mode;} nrfx_timer_config_t;
#define NRFX_TIMER_INSTANCE(n) {n}
#define NRFX_TIMER_DEFAULT_CONFIG(hz) {.frequency=hz}
#define NRF_TIMER_BIT_WIDTH_32 32
#define NRF_TIMER_MODE_TIMER 0
#define NRF_TIMER_CC_CHANNEL0 0
#define NRF_TIMER_CC_CHANNEL2 2
#define NRFX_SUCCESS 0
static inline int nrfx_timer_init(const nrfx_timer_t *t,const nrfx_timer_config_t *c,void *handler){(void)t;assert(c->frequency==16000000&&c->bit_width==32&&!handler);return failure==2?1:0;}
static inline void nrfx_timer_enable(const nrfx_timer_t *t){(void)t;timer_started=true;}
static inline void nrfx_timer_disable(const nrfx_timer_t *t){(void)t;timer_started=false;}
static inline void nrfx_timer_uninit(const nrfx_timer_t *t){(void)t;++uninitializations;}
static inline uint32_t nrfx_timer_capture(const nrfx_timer_t *t,unsigned cc){(void)t;assert(timer_started&&cc==2);return raw_now;}
static inline uint32_t nrfx_timer_capture_get(const nrfx_timer_t *t,unsigned cc){(void)t;assert(timer_started&&cc==0);return raw_pps;}
static inline uint32_t nrfx_timer_capture_task_address_get(const nrfx_timer_t *t,unsigned cc){(void)t;assert(cc<=1);return 1234+cc;}
typedef unsigned nrf_ppi_channel_t;
static inline int nrfx_ppi_channel_alloc(nrf_ppi_channel_t *ch){if(failure==3)return 1;*ch=allocated?6:5;allocated|=1u<<*ch;return 0;}
static inline int nrfx_ppi_channel_assign(nrf_ppi_channel_t ch,uint32_t event,uint32_t task){assert((allocated&(1u<<ch))&&((ch==5&&event==5678&&task==1234)||(ch==6&&event==6789&&task==1235)));return failure==6?1:0;}
static inline int nrfx_ppi_channel_enable(nrf_ppi_channel_t ch){assert((ch==5||ch==6)&&(allocated&(1u<<ch)));if(failure==7)return 1;route_enabled=true;return 0;}
static inline int nrfx_ppi_channel_disable(nrf_ppi_channel_t ch){assert((ch==5||ch==6)&&(allocated&(1u<<ch)));if(failure==9)return 1;if(ch==5)route_enabled=false;return 0;}
static inline int nrfx_ppi_channel_free(nrf_ppi_channel_t ch){assert((allocated&(1u<<ch))&&(ch==6||!route_enabled));allocated&=~(1u<<ch);return 0;}
typedef struct{void *p_reg;} nrfx_gpiote_t;
#define NRFX_GPIOTE_INSTANCE(n) {NULL}
#define NRF_GPIO_PIN_MAP(p,n) ((p)*32+(n))
static inline int nrfx_gpiote_channel_get(const nrfx_gpiote_t *g,unsigned pin,uint8_t *ch){(void)g;assert(pin==43);if(failure==5)return 1;*ch=3;return 0;}
static inline uint32_t nrfx_gpiote_in_event_address_get(const nrfx_gpiote_t *g,unsigned pin){(void)g;assert(pin==43);return 5678;}
static inline unsigned nrf_gpiote_in_event_get(unsigned ch){assert(ch==3);return 3;}
static inline uint32_t nrf_gpiote_event_address_get(void *reg,unsigned event){(void)reg;assert(event==3);return 5678;}
static inline int gpio_pin_interrupt_configure_dt(const struct gpio_dt_spec *p,unsigned mode){assert(p->pin==11);return (failure==4&&mode==GPIO_INT_DISABLE)||(failure==8&&mode==GPIO_INT_EDGE_TO_ACTIVE)?-EIO:0;}

#define NRF_RADIO ((void *)1)
#define NRF_RADIO_EVENT_ADDRESS 1
static inline uint32_t nrf_radio_event_address_get(void *r,unsigned event){assert(r==NRF_RADIO&&event==1);return 6789;}
static bool radio_open;
static inline int r2_radio_capture_enable(uint32_t epoch,unsigned ch){assert(epoch==1&&ch==6);radio_open=true;return 0;}
static inline int r2_radio_capture_disable(void){radio_open=false;return 0;}

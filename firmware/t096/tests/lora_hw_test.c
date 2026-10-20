/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Full production owner body, fake peripheral boundary. Physical RF/timing
 * and the pinned driver itself remain separate native/hardware checks. */
#include "lora_hw.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BUILD_ASSERT(x) _Static_assert(x,#x)
#define IS_ENABLED(x) (x)
#define CONFIG_SOC_NRF52840 1
#define CONFIG_PM_DEVICE_RUNTIME 0
#define DT_NODE_HAS_STATUS(n,s) 0
#define DT_NODELABEL(x) x
#define DEVICE_DT_GET(x) (&device)
#define GPIO_DT_SPEC_GET(n,p) {&device,n,0}
#define NRF_GPIO_PIN_MAP(p,n) ((p)*32+(n))
#define NRFX_TIMER_INSTANCE(x) {x}
#define NRFX_GPIOTE_INSTANCE(x) {x}
#define NRFX_TIMER_DEFAULT_CONFIG(hz) {hz,0,0}
#define NRFX_SUCCESS 0
#define NRF_TIMER_BIT_WIDTH_32 32
#define NRF_GPIO_PIN_S0S1 0
#define NRF_GPIO_PIN_INPUT_DISCONNECT 0
#define NRF_GPIO_PIN_NOPULL 0
#define NRF_GPIOTE_POLARITY_TOGGLE 0
#define NRF_GPIOTE_INITIAL_VALUE_LOW 0
#define NRF_TIMER_CC_CHANNEL0 0
#define NRF_TIMER_SHORT_COMPARE0_STOP_MASK 1
#define NRF_TIMER_EVENT_COMPARE0 0
#define EVENT_TIMER_ID 0
#define GPIO_INPUT 0
#define GPIO_INT_EDGE_TO_ACTIVE 1
#define GPIO_INT_DISABLE 0
#define BIT(x) (1u<<(x))
#define SPI_WORD_SET(x) (x)
#define SPI_TRANSFER_MSB 0
#define IRQ_CONNECT(a,b,c,d,e)
#define K_MSEC(x) (x)
#define K_SEM_DEFINE(n,a,b) struct sem n={a}
enum{lora_reset=16,lora_ctx=41,lora_fem_power=30,lora_busy=19,lora_dio1=21,lora_cs=5};
struct device{int unused;};static struct device device;
struct gpio_dt_spec{const struct device *port;unsigned pin,dt_flags;};
struct gpio_callback{void (*handler)(const struct device *,struct gpio_callback *,uint32_t);};
struct spi_buf{void *buf;size_t len;};
struct spi_buf_set{struct spi_buf *buffers;size_t count;};
struct spi_config{unsigned frequency,operation,slave;struct{struct gpio_dt_spec gpio;unsigned delay;}cs;};
struct sem{int count;};typedef int atomic_t;typedef int atomic_val_t;
typedef int nrf_ppi_channel_t;typedef int nrf_timer_event_t;
typedef struct{int instance;}nrfx_timer_t,nrfx_gpiote_t;
typedef struct{unsigned frequency,bit_width,interrupt_priority;}nrfx_timer_config_t;
typedef struct{unsigned drive,input_connect,pull;}nrfx_gpiote_output_config_t;
typedef struct{unsigned task_ch,polarity,init_val;}nrfx_gpiote_task_config_t;
struct r2_capture{uint64_t tick,epoch;int kind;};
static uint64_t mock_us=1000000,physical_tick=15999999;
static int pins[64],irq_flags,packet_length=80,busy_high,suppress_dma,alloc_fail,stop_at_attach,stop_at_spi;
static unsigned submissions,attach_count,detach_count;static bool route;
static bool expire_at_fem;static unsigned set_tx_commands;
static void preempt_cutoff(void);
static uint8_t regs[0x1000],payload[80];
static int atomic_get(atomic_t *a){return *a;}
static void atomic_set(atomic_t *a,int v){*a=v;}
static void atomic_clear(atomic_t *a){*a=0;}
static void atomic_inc(atomic_t *a){++*a;}
static bool atomic_cas(atomic_t *a,int old,int n){if(*a!=old)return false;*a=n;return true;}
static unsigned irq_lock(void){return 0;}static void irq_unlock(unsigned k){(void)k;preempt_cutoff();}
static int64_t k_uptime_get(void){return (int64_t)(mock_us/1000);}
static int64_t k_uptime_ticks(void){return (int64_t)mock_us;}
static uint64_t k_ticks_to_us_floor64(int64_t ticks){return (uint64_t)ticks;}
static void k_sleep(int ms){mock_us+=(uint64_t)ms*1000;}
static void k_sem_reset(struct sem *s){s->count=0;}
static void k_sem_give(struct sem *s){s->count=1;}
static int k_sem_take(struct sem *s,int ms){if(s->count){s->count=0;return 0;}mock_us+=(uint64_t)ms*1000;return -EAGAIN;}
static int gpio_pin_set_dt(const struct gpio_dt_spec *p,int active){pins[p->pin]=p->pin==16?!active:active;return 0;}
static int gpio_pin_get_dt(const struct gpio_dt_spec *p){return p->pin==19?busy_high:pins[p->pin];}
static int gpio_pin_configure_dt(const struct gpio_dt_spec *p,int flags){(void)p;(void)flags;return 0;}
static int gpio_pin_interrupt_configure_dt(const struct gpio_dt_spec *p,int flags){(void)p;(void)flags;return 0;}
static void gpio_init_callback(struct gpio_callback *p,void(*fn)(const struct device *,struct gpio_callback *,uint32_t),unsigned mask){(void)mask;p->handler=fn;}
static int gpio_add_callback(const struct device *d,struct gpio_callback *p){(void)d;(void)p;return 0;}
static int nrf_gpio_pin_out_read(unsigned pin){return pins[pin];}
static bool device_is_ready(const struct device *d){(void)d;return true;}
static bool nrfx_gpiote_init_check(const nrfx_gpiote_t *g){(void)g;return true;}
static int nrfx_gpiote_channel_alloc(const nrfx_gpiote_t *g,uint8_t *ch){(void)g;*ch=2;return alloc_fail;}
static int nrfx_gpiote_output_configure(const nrfx_gpiote_t *g,unsigned p,const nrfx_gpiote_output_config_t *o,const nrfx_gpiote_task_config_t *t){(void)g;(void)p;(void)o;(void)t;return 0;}
static void nrfx_gpiote_out_task_enable(const nrfx_gpiote_t *g,unsigned p){(void)g;(void)p;}
static void nrfx_gpiote_clr_task_trigger(const nrfx_gpiote_t *g,unsigned p){(void)g;pins[p]=0;}
static void nrfx_gpiote_set_task_trigger(const nrfx_gpiote_t *g,unsigned p){(void)g;pins[p]=1;}
static uint32_t nrfx_gpiote_clr_task_address_get(const nrfx_gpiote_t *g,unsigned p){(void)g;(void)p;return 0x1234;}
static int nrfx_ppi_channel_alloc(nrf_ppi_channel_t *ch){*ch=8;return 0;}
static int nrfx_ppi_channel_enable(nrf_ppi_channel_t ch){(void)ch;return 0;}
static int nrfx_ppi_channel_disable(nrf_ppi_channel_t ch){(void)ch;return 0;}
static int nrfx_ppi_channel_assign(nrf_ppi_channel_t ch,uint32_t e,uint32_t t){(void)ch;(void)e;assert(t==0x1234);return 0;}
static int nrfx_ppi_channel_fork_assign(nrf_ppi_channel_t ch,uint32_t t){(void)ch;assert(!t);return 0;}
static void nrfx_timer_disable(const nrfx_timer_t *t){(void)t;}
static void nrfx_timer_clear(const nrfx_timer_t *t){(void)t;}
static void nrfx_timer_enable(const nrfx_timer_t *t){(void)t;}
static void nrfx_timer_extended_compare(const nrfx_timer_t *t,int ch,unsigned us,int shorts,bool irq){(void)t;(void)ch;(void)shorts;(void)irq;assert(us==360000);}
static int nrfx_timer_init(const nrfx_timer_t *t,const nrfx_timer_config_t *c,void(*fn)(int,void*)){(void)t;(void)fn;assert(c->frequency==1000000&&c->interrupt_priority==6);return 0;}
static uint32_t nrfx_timer_compare_event_address_get(const nrfx_timer_t *t,int ch){(void)t;(void)ch;return 0x5678;}
static bool r2_capture_now(struct r2_capture *c){*c=(struct r2_capture){mock_us*16,7,3};return true;}
static bool r2_capture_lora(struct r2_capture *c){assert(route);*c=(struct r2_capture){physical_tick,7,4};return true;}
static int r2_capture_lora_attach(const struct gpio_dt_spec *p,uint32_t task){assert(p->pin==21&&task==0x1234);route=true;++attach_count;if(stop_at_attach)r2_lora_hw_stop_begin(mock_us+200000);return 0;}
static int r2_capture_lora_detach(void){route=false;++detach_count;return 0;}
static int spi_transceive_cb(const struct device *d,const struct spi_config *c,const struct spi_buf_set *tx,const struct spi_buf_set *rx,void(*fn)(const struct device*,int,void*),void *arg){
 assert(c->frequency==8000000&&tx->count==1&&rx->count==1);
 ++submissions;uint8_t *a=tx->buffers->buf,*b=rx->buffers->buf;size_t n=tx->buffers->len;
 assert(n==rx->buffers->len&&n<=83);memset(b,0,n);
 switch(a[0]){
 case 0x0d:regs[(a[1]<<8)|a[2]]=a[3];break;
 case 0x1d:b[3]=0x22;b[4]=regs[(a[1]<<8)|a[2]];break;
 case 0x12:b[1]=0x22;b[2]=irq_flags>>8;b[3]=irq_flags;break;
 case 0x13:b[1]=0x22;b[2]=packet_length;b[3]=0;break;
 case 0x1e:b[2]=0x22;memcpy(b+3,payload,n-3);break;
 case 0x02:irq_flags=0;break;
 case 0x82:assert(a[1]==0&&a[2]==0&&a[3]==0);break;
 case 0x83:++set_tx_commands;assert(a[1]==0&&a[2]==0x55&&a[3]==0);break;
 case 0x93:assert(a[1]==0x20);break;
 default:break;
 }
 if(stop_at_spi)r2_lora_hw_stop_begin(mock_us+200000);
 if(!suppress_dma)fn(d,0,arg);return 0;
}
#include R2_LORA_OWNER_BODY
static void preempt_cutoff(void){
 if(expire_at_fem&&pins[12]){
  expire_at_fem=false;mock_us+=360001;pins[12]=0; /* Hardware PPI CLR. */
  cutoff_irq(NRF_TIMER_EVENT_COMPARE0,NULL);
 }
}
static void inject(int flags){irq_flags=flags;pins[12]=0;dio_irq(&device,&dio_callback,BIT(21));}
static void closed(void){struct r2_lora_hw_ack ack;r2_lora_hw_stop_poll(&ack);assert(ack.dma_retired&&ack.radio_quiescent&&ack.fem_off&&ack.capture_released);}
int main(int argc,char **argv){
 assert(argc==2);int mode=atoi(argv[1]);for(unsigned i=0;i<80;i++)payload[i]=(uint8_t)(i+11);
 if(mode==1){alloc_fail=1;assert(r2_lora_hw_start(1,true)<0);assert(!submissions&&!route);closed();return 0;}
 if(mode==2){stop_at_attach=1;assert(r2_lora_hw_start(1,true)==-ECANCELED);assert(!submissions&&!pins[30]&&!pins[16]);closed();assert(!atomic_get(&fault));return 0;}
 assert(!r2_lora_hw_start(4,mode!=3&&mode!=11&&mode!=14));
 if(mode==3){assert(!r2_lora_hw_tx(payload,80,5));inject(1);mock_us+=400000;cutoff_irq(0,NULL);struct r2_lora_hw_event e;
  assert(!atomic_get(&fault)&&!counters.cutoffs); /* Latched physical DONE wins. */
  assert(r2_lora_hw_take(&e)==1&&e.kind==R2_LORA_HW_TX_DONE&&e.generation==5&&e.tick==physical_tick&&e.capture_epoch==7);
  assert(e.local_us<mock_us);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==4){assert(r2_lora_hw_tx(payload,80,9)==-EBUSY);inject(2);struct r2_lora_hw_event e;
  assert(r2_lora_hw_take(&e)==1&&e.length==80&&!memcmp(e.frame,payload,80)&&e.tick==physical_tick);
  memset(payload,0,80);assert(e.frame[0]==11);assert(r2_lora_hw_take(&e)==0);assert(!r2_lora_hw_rx_rearm());
  packet_length=68;inject(2);assert(r2_lora_hw_take(&e)==1&&e.length==68);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==5){inject(0x42);struct r2_lora_hw_event e;assert(r2_lora_hw_take(&e)==0);assert(counters.crc_bad==1&&atomic_get(&phase)==2);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==6){packet_length=81;inject(2);struct r2_lora_hw_event e;assert(r2_lora_hw_take(&e)==0&&atomic_get(&phase)==2);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==7){inject(2);inject(2);struct r2_lora_hw_event e;assert(counters.overflow==1&&r2_lora_hw_take(&e)==1);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==8){busy_high=1;inject(2);struct r2_lora_hw_event e;unsigned before=submissions;assert(r2_lora_hw_take(&e)==-ETIMEDOUT&&submissions==before);closed();return 0;}
 if(mode==9){suppress_dma=1;inject(2);struct r2_lora_hw_event e;assert(r2_lora_hw_take(&e)==-ETIMEDOUT);uint8_t old[83];memcpy(old,dma_tx,83);size_t len=tx_buffer.len;unsigned n=submissions;
  struct r2_lora_hw_ack ack;r2_lora_hw_stop_poll(&ack);assert(!ack.dma_retired&&!ack.capture_released&&ack.fem_off&&ack.radio_quiescent);
  assert(r2_lora_hw_start(6,true)<0&&r2_lora_hw_take(&e)<0);assert(r2_lora_hw_rx_rearm()<0);assert(submissions==n&&len==tx_buffer.len&&!memcmp(old,dma_tx,83));
  spi_done(&device,0,NULL);closed();assert(r2_lora_hw_start(6,true)<0);return 0;}
 if(mode==10){stop_at_spi=1;inject(2);struct r2_lora_hw_event e;assert(r2_lora_hw_take(&e)==-ECANCELED);closed();assert(!atomic_get(&fault));return 0;}
 if(mode==11){assert(!r2_lora_hw_tx(payload,80,5));cutoff_irq(0,NULL);struct r2_lora_hw_event e;assert(r2_lora_hw_take(&e)==-ETIMEDOUT&&counters.cutoffs==1);closed();return 0;}
 if(mode==14){expire_at_fem=true;assert(r2_lora_hw_tx(payload,80,5)==-ECANCELED);assert(!set_tx_commands&&!pins[12]&&!pins[16]&&!pins[30]&&!atomic_get(&running));assert(counters.cutoffs==1&&atomic_get(&fault)==-ETIMEDOUT);closed();return 0;}
 if(mode==13){r2_lora_hw_stop_begin(mock_us+200000);closed();pins[21]=1;assert(r2_lora_hw_start(6,true)==-EIO);assert(!pins[12]&&!pins[30]&&!pins[16]);closed();return 0;}
 if(mode==12){r2_lora_hw_stop_begin(mock_us+200000);closed();assert(!r2_lora_hw_start(6,true));assert(attach_count==2&&detach_count==1);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 assert(!"unknown mode");
}

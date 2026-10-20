/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Full production owner body, fake peripheral boundary. Physical RF/timing
 * and the pinned driver itself remain separate native/hardware checks. */
#include "lora_hw.h"
#include "lora_watchdog.h"
union r2_diag_scratch r2_diag_scratch;
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
static unsigned submissions,attach_count,detach_count;static bool route,watchdog_at_attach;
static bool expire_at_fem,delay_before_fem,spi_eagain_payload,invalid_capture,stuck_dio,abort_in_settx,done_in_settx,withhold_settx,abort_at_commit,expire_after_commit,busy_on_settx,busy_at_fem,ready_at_busy_deadline,cutoff_at_busy_deadline,error_at_busy_deadline;static uint64_t busy_since;static int settx_error;static unsigned irq_depth;static unsigned set_tx_commands,last_budget;
static void preempt_cutoff(void);
static void preempt_submission(void);
static void preempt_before_lock(void);
static uint8_t regs[0x1000],payload[R2_LORA_HW_BYTES];
static int atomic_get(atomic_t *a){return *a;}
static void atomic_set(atomic_t *a,int v){*a=v;}
static void atomic_clear(atomic_t *a){*a=0;}
static void atomic_inc(atomic_t *a){++*a;}
static bool atomic_cas(atomic_t *a,int old,int n){if(*a!=old)return false;*a=n;return true;}
static unsigned irq_lock(void){if(!irq_depth)preempt_before_lock();unsigned prior=irq_depth++;return prior;}static void irq_unlock(unsigned k){irq_depth=k;if(!irq_depth)preempt_cutoff();}
static int64_t k_uptime_get(void){return (int64_t)(mock_us/1000);}
static uint32_t k_uptime_get_32(void){return (uint32_t)(mock_us/1000);}
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
static void nrfx_timer_extended_compare(const nrfx_timer_t *t,int ch,unsigned us,int shorts,bool irq){(void)t;(void)ch;(void)shorts;(void)irq;assert(us==360000||us==450000);last_budget=us;}
static int nrfx_timer_init(const nrfx_timer_t *t,const nrfx_timer_config_t *c,void(*fn)(int,void*)){(void)t;(void)fn;assert(c->frequency==1000000&&c->interrupt_priority==6);return 0;}
static uint32_t nrfx_timer_compare_event_address_get(const nrfx_timer_t *t,int ch){(void)t;(void)ch;return 0x5678;}
static bool r2_capture_now(struct r2_capture *c){*c=(struct r2_capture){mock_us*16,7,3};return true;}
static bool r2_capture_lora(struct r2_capture *c){assert(route);if(invalid_capture)return false;*c=(struct r2_capture){physical_tick,7,4};return true;}
static int r2_capture_lora_attach(const struct gpio_dt_spec *p,uint32_t task){assert(p->pin==21&&task==0x1234);route=true;++attach_count;if(stop_at_attach)r2_lora_hw_stop_begin(mock_us+200000);if(watchdog_at_attach)r2_lora_hw_watchdog_trip(-ETIMEDOUT);return 0;}
static int r2_capture_lora_detach(void){route=false;++detach_count;return 0;}
static int spi_transceive_cb(const struct device *d,const struct spi_config *c,const struct spi_buf_set *tx,const struct spi_buf_set *rx,void(*fn)(const struct device*,int,void*),void *arg){
 assert(c->frequency==8000000&&tx->count==1&&rx->count==1);
 ++submissions;uint8_t *a=tx->buffers->buf,*b=rx->buffers->buf;size_t n=tx->buffers->len;
 assert(n==rx->buffers->len&&n<=R2_LORA_HW_BYTES+3);memset(b,0,n);
 switch(a[0]){
 case 0x0d:regs[(a[1]<<8)|a[2]]=a[3];break;
 case 0x1d:b[3]=0x22;b[4]=regs[(a[1]<<8)|a[2]];break;
 case 0x12:b[1]=0x22;b[2]=irq_flags>>8;b[3]=irq_flags;break;
 case 0x13:b[1]=0x22;b[2]=packet_length;b[3]=0;break;
 case 0x1e:b[2]=0x22;memcpy(b+3,payload,n-3);break;
 case 0x02:irq_flags=0;if(!stuck_dio)pins[21]=0;break;
 case 0x82:assert(a[1]==0&&a[2]==0&&a[3]==0);break;
 case 0x83:++set_tx_commands;assert(a[1]==0&&a[2]==(last_budget==360000?0x55:0x69)&&a[3]==0);break;
 case 0x0e:if(delay_before_fem)mock_us+=500000;break;
 case 0x93:assert(a[1]==0x20);break;
 default:break;
 }
 if(stop_at_spi)r2_lora_hw_stop_begin(mock_us+200000);
 if(!suppress_dma&&!(withhold_settx&&a[0]==0x83))fn(d,spi_eagain_payload&&a[0]==0x0e?-EAGAIN:a[0]==0x83?settx_error:0,arg);
 if(a[0]==0x83){if(busy_on_settx){busy_on_settx=false;busy_high=1;busy_since=mock_us;}preempt_submission();}return 0;
}
#include R2_LORA_OWNER_BODY
static void preempt_before_lock(void){
 if(atomic_get(&phase)==4&&busy_high&&mock_us-busy_since>=30000){
  if(ready_at_busy_deadline){ready_at_busy_deadline=false;busy_high=0;}
  if(error_at_busy_deadline){error_at_busy_deadline=false;busy_high=-EIO;}
  if(cutoff_at_busy_deadline){cutoff_at_busy_deadline=false;mock_us+=last_budget;pins[12]=0;cutoff_irq(NRF_TIMER_EVENT_COMPARE0,NULL);}
 }
 if(abort_at_commit&&atomic_get(&phase)==4&&pins[12]&&set_tx_commands&&((wd.stage>>8)==R2_LORA_STAGE_BUSY_AFTER)){
  abort_at_commit=false;mock_us+=last_budget+1;pins[12]=0;cutoff_irq(NRF_TIMER_EVENT_COMPARE0,NULL);
 }
}
static void preempt_cutoff(void){
 if(busy_at_fem&&atomic_get(&phase)==4&&pins[12]){busy_at_fem=false;busy_high=1;busy_since=mock_us;}
 if(expire_after_commit&&atomic_get(&phase)==3&&pins[12]){
  expire_after_commit=false;mock_us+=last_budget+1;pins[12]=0;cutoff_irq(NRF_TIMER_EVENT_COMPARE0,NULL);
 }
 if(expire_at_fem&&pins[12]){
  expire_at_fem=false;mock_us+=last_budget+1;pins[12]=0; /* Hardware PPI CLR. */
  cutoff_irq(NRF_TIMER_EVENT_COMPARE0,NULL);
 }
}
static void preempt_submission(void){
 if(abort_in_settx||done_in_settx){
  bool completed_tx=done_in_settx;abort_in_settx=done_in_settx=false;
  mock_us+=last_budget+1;pins[12]=0;
  if(completed_tx){irq_flags=1;pins[21]=1;}
  cutoff_irq(NRF_TIMER_EVENT_COMPARE0,NULL);
 }
}
static void inject(int flags){irq_flags=flags;pins[12]=0;dio_irq(&device,&dio_callback,BIT(21));}
static void closed(void){struct r2_lora_hw_ack ack;r2_lora_hw_stop_poll(&ack);assert(ack.dma_retired&&ack.radio_quiescent&&ack.fem_off&&ack.capture_released);}
int main(int argc,char **argv){
 assert(argc==2);int mode=atoi(argv[1]);for(unsigned i=0;i<R2_LORA_HW_BYTES;i++)payload[i]=(uint8_t)(i+11);
 if(mode==1){alloc_fail=1;assert(r2_lora_hw_start(1,true)<0);assert(!submissions&&!route);closed();return 0;}
 if(mode==2){stop_at_attach=1;assert(r2_lora_hw_start(1,true)==-ECANCELED);assert(!submissions&&!pins[30]&&!pins[16]);closed();assert(!atomic_get(&fault));return 0;}
 if(mode==44){watchdog_at_attach=true;assert(r2_lora_hw_start(1,true)==-ECANCELED);
  assert(wd.terminal&&atomic_get(&fault)==-ETIMEDOUT&&!submissions&&!pins[30]&&!pins[16]&&!atomic_get(&running));
  closed();assert(wd.closure==31);return 0;}
 bool tx_mode=mode==3||mode==11||mode==14||mode==15||mode==16||mode==17||mode==19||mode==20||mode==21||mode==22||mode==23||mode==24||mode==25||mode==27||mode==28||mode==29||mode==30||mode==31||mode==32||mode==33||mode>=34;
 assert(!r2_lora_hw_start(4,!tx_mode));
 if(mode==3){assert(!r2_lora_hw_tx(payload,80,5));inject(1);mock_us+=400000;cutoff_irq(0,NULL);struct r2_lora_hw_event e;
  assert(!atomic_get(&fault)&&!counters.cutoffs); /* Latched physical DONE wins. */
  assert(r2_lora_hw_take(&e)==1&&e.kind==R2_LORA_HW_TX_DONE&&e.generation==5&&e.tick==physical_tick&&e.capture_epoch==7);
  assert(e.local_us<mock_us);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==4){assert(r2_lora_hw_tx(payload,80,9)==-EBUSY);inject(2);struct r2_lora_hw_event e;
  assert(r2_lora_hw_take(&e)==1&&e.length==80&&!memcmp(e.frame,payload,80)&&e.tick==physical_tick);
  memset(payload,0,80);assert(e.frame[0]==11);assert(r2_lora_hw_take(&e)==0);assert(!r2_lora_hw_rx_rearm());
  packet_length=68;inject(2);assert(r2_lora_hw_take(&e)==1&&e.length==68);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==5){inject(0x42);struct r2_lora_hw_event e;assert(r2_lora_hw_take(&e)==0);assert(counters.crc_bad==1&&atomic_get(&phase)==2);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==6){packet_length=R2_LORA_HW_BYTES+1;inject(2);struct r2_lora_hw_event e;assert(r2_lora_hw_take(&e)==0&&atomic_get(&phase)==2);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==7){inject(2);inject(2);struct r2_lora_hw_event e;assert(counters.overflow==1&&r2_lora_hw_take(&e)==1);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==8){busy_high=1;inject(2);struct r2_lora_hw_event e;unsigned before=submissions;assert(r2_lora_hw_take(&e)==-ETIMEDOUT&&submissions==before&&counters.last_busy_op==0x12);closed();return 0;}
 if(mode==9){suppress_dma=1;inject(2);struct r2_lora_hw_event e;assert(r2_lora_hw_take(&e)==-ETIMEDOUT);uint8_t old[R2_LORA_HW_BYTES+3];memcpy(old,dma_tx,sizeof old);size_t len=tx_buffer.len;unsigned n=submissions;
  struct r2_lora_hw_ack ack;r2_lora_hw_stop_poll(&ack);assert(!ack.dma_retired&&!ack.capture_released&&ack.fem_off&&ack.radio_quiescent);
  assert(r2_lora_hw_start(6,true)<0&&r2_lora_hw_take(&e)<0);assert(r2_lora_hw_rx_rearm()<0);assert(submissions==n&&len==tx_buffer.len&&!memcmp(old,dma_tx,sizeof old));
  spi_done(&device,0,NULL);closed();assert(r2_lora_hw_start(6,true)<0);return 0;}
 if(mode==10){stop_at_spi=1;inject(2);struct r2_lora_hw_event e;assert(r2_lora_hw_take(&e)==-ECANCELED);closed();assert(!atomic_get(&fault));return 0;}
 if(mode==11){assert(!r2_lora_hw_tx(payload,80,5));cutoff_irq(0,NULL);struct r2_lora_hw_event e;assert(r2_lora_hw_take(&e)==-ETIMEDOUT&&counters.cutoffs==1&&!counters.launch_aborts);closed();return 0;}
 if(mode==14){expire_at_fem=true;assert(r2_lora_hw_tx(payload,80,5)==-EINTR);assert(!set_tx_commands&&!pins[12]&&!pins[16]&&!pins[30]&&!atomic_get(&running));assert(counters.launch_aborts==1&&!counters.cutoffs&&!atomic_get(&fault));closed();
  assert(!r2_lora_hw_start(6,false));assert(!r2_lora_hw_tx(payload,80,7));inject(1);struct r2_lora_hw_event e;assert(r2_lora_hw_take(&e)==1&&e.generation==7);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==15){delay_before_fem=true;uint64_t until=mock_us+451000;
  assert(r2_lora_hw_tx_before(payload,128,5,until)==-EAGAIN);
  assert(!set_tx_commands&&!pins[12]&&atomic_get(&phase)==1&&!atomic_get(&fault)&&atomic_get(&running));
  delay_before_fem=false;assert(!r2_lora_hw_tx_before(payload,80,6,mock_us+361000));
  assert(last_budget==360000&&set_tx_commands==1);inject(1);struct r2_lora_hw_event e;assert(r2_lora_hw_take(&e)==1);
  r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==16){assert(!r2_lora_hw_tx_before(payload,128,5,mock_us+451000));assert(last_budget==450000&&set_tx_commands==1);
  inject(1);mock_us+=500000;cutoff_irq(0,NULL);struct r2_lora_hw_event e;
  assert(!atomic_get(&fault)&&!counters.cutoffs&&r2_lora_hw_take(&e)==1&&e.tick==physical_tick);
  r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==17||mode==20){size_t n=mode==17?80:128;uint32_t budget=r2_lora_hw_budget_us(n);
  assert(r2_lora_hw_tx_before(payload,n,5,mock_us+budget+999)==-EAGAIN);
  assert(!set_tx_commands&&!pins[12]&&!atomic_get(&fault)&&atomic_get(&phase)==1);
  assert(!r2_lora_hw_tx_before(payload,n,6,mock_us+budget+1000));assert(last_budget==budget);
  inject(1);struct r2_lora_hw_event e;assert(r2_lora_hw_take(&e)==1);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==18){packet_length=128;inject(2);struct r2_lora_hw_event e;
  assert(r2_lora_hw_take(&e)==1&&e.length==128&&!memcmp(e.frame,payload,128)&&e.tick==physical_tick);
  memset(payload,0,sizeof payload);assert(e.frame[127]==(uint8_t)(127+11));
  assert(!r2_lora_hw_rx_rearm());packet_length=0;inject(2);assert(r2_lora_hw_take(&e)==0);
  r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==19){expire_at_fem=true;assert(r2_lora_hw_tx(payload,128,5)==-EINTR);
  assert(last_budget==450000&&!set_tx_commands&&!pins[12]&&!pins[16]&&!pins[30]&&!atomic_get(&running));
  assert(counters.launch_aborts==1&&!counters.cutoffs&&!atomic_get(&fault));closed();return 0;}
 if(mode==21){assert(!r2_lora_hw_budget_us(0)&&!r2_lora_hw_budget_us(129));unsigned before=submissions;
  assert(r2_lora_hw_tx_before(payload,0,5,UINT64_MAX)==-EINVAL&&r2_lora_hw_tx_before(payload,129,5,UINT64_MAX)==-EINVAL);
  assert(submissions==before&&!set_tx_commands&&!pins[12]);assert(!r2_lora_hw_tx_before(payload,128,5,UINT64_MAX));
  inject(1);struct r2_lora_hw_event e;assert(r2_lora_hw_take(&e)==1);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==22){stop_at_spi=1;assert(r2_lora_hw_tx_before(payload,128,5,mock_us+451000)==-ECANCELED);
  assert(!set_tx_commands&&!pins[12]&&!atomic_get(&fault));closed();return 0;}
 if(mode==23){spi_eagain_payload=true;assert(r2_lora_hw_tx_before(payload,128,5,mock_us+451000)==-EIO);
  assert(atomic_get(&fault)==-EAGAIN&&counters.spi_error==1&&!set_tx_commands&&!pins[12]&&!atomic_get(&running));closed();return 0;}
 if(mode==24||mode==25){assert(!r2_lora_hw_tx(payload,128,5));
  /* The physical edge/CC4/FEM inhibition happened before BLE delayed both
   * callbacks. The cutoff ISR runs before the ordinary DIO callback. */
  irq_flags=mode==24?1:0x200;pins[21]=1;pins[12]=0;mock_us+=6000000;cutoff_irq(0,NULL);
  assert(atomic_get(&latched)&&!atomic_get(&fault)&&!counters.cutoffs);
  dio_irq(&device,&dio_callback,BIT(21));assert(!counters.overflow);struct r2_lora_hw_event e;
  if(mode==24)assert(r2_lora_hw_take(&e)==1&&e.tick==physical_tick&&e.capture_epoch==7&&counters.tx_done==1);
  else assert(r2_lora_hw_take(&e)==-EIO&&!counters.tx_done&&atomic_get(&fault));
  r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==26){invalid_capture=true;inject(2);struct r2_lora_hw_event e;
  assert(!atomic_get(&fault)&&r2_lora_hw_take(&e)==1&&e.kind==R2_LORA_HW_RX_DONE&&!e.tick&&!e.capture_epoch&&counters.invalid_capture==1);
  assert(!r2_lora_hw_rx_rearm());invalid_capture=false;inject(2);
  assert(r2_lora_hw_take(&e)==1&&e.tick==physical_tick&&e.capture_epoch==7);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==27){assert(!r2_lora_hw_tx(payload,80,5));invalid_capture=true;inject(1);struct r2_lora_hw_event e;
  assert(!atomic_get(&fault)&&r2_lora_hw_take(&e)==1&&e.kind==R2_LORA_HW_TX_DONE&&!e.tick&&!e.capture_epoch&&counters.invalid_capture==1);
  invalid_capture=false;assert(!r2_lora_hw_tx(payload,80,6));inject(1);
  assert(r2_lora_hw_take(&e)==1&&e.tick==physical_tick&&e.capture_epoch==7);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==28){abort_in_settx=true;assert(r2_lora_hw_tx(payload,80,5)==-EINTR);
  assert(set_tx_commands==1&&counters.launch_aborts==1&&!counters.cutoffs&&!atomic_get(&fault)&&!atomic_get(&dma_inflight)&&!atomic_get(&running));
  assert(!pins[12]&&!pins[16]&&!pins[30]);assert(r2_lora_hw_start(6,false)==-EBUSY);closed();assert(!r2_lora_hw_start(6,false));r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==29){done_in_settx=true;assert(!r2_lora_hw_tx(payload,80,5));struct r2_lora_hw_event e;
  assert(set_tx_commands==1&&!counters.launch_aborts&&!counters.cutoffs&&!atomic_get(&fault));
  assert(r2_lora_hw_take(&e)==1&&e.kind==R2_LORA_HW_TX_DONE&&e.tick==physical_tick);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==30){abort_in_settx=true;settx_error=-EIO;assert(r2_lora_hw_tx(payload,80,5)==-EIO);
  assert(counters.launch_aborts==1&&counters.spi_error==1&&atomic_get(&fault)==-EIO);closed();assert(r2_lora_hw_start(6,false)==-EIO);return 0;}
 if(mode==31){abort_in_settx=withhold_settx=true;assert(r2_lora_hw_tx(payload,80,5)==-ETIMEDOUT);
  assert(counters.launch_aborts==1&&counters.spi_error==1&&atomic_get(&fault)==-ETIMEDOUT&&atomic_get(&dma_inflight));
  struct r2_lora_hw_ack ack;r2_lora_hw_stop_poll(&ack);assert(!ack.dma_retired&&!ack.capture_released&&ack.fem_off&&ack.radio_quiescent);assert(r2_lora_hw_start(6,false)<0);
  spi_done(&device,0,NULL);closed();assert(r2_lora_hw_start(6,false)==-ETIMEDOUT);return 0;}
 if(mode==32){abort_at_commit=true;assert(r2_lora_hw_tx(payload,80,5)==-EINTR);
  assert(set_tx_commands==1&&counters.launch_aborts==1&&!counters.cutoffs&&!atomic_get(&fault));closed();return 0;}
 if(mode==33){expire_after_commit=true;assert(!r2_lora_hw_tx(payload,80,5));struct r2_lora_hw_event e;
  assert(set_tx_commands==1&&!counters.launch_aborts&&counters.cutoffs==1&&atomic_get(&fault)==-ETIMEDOUT);assert(r2_lora_hw_take(&e)==-ETIMEDOUT);closed();return 0;}
 if(mode==34){busy_on_settx=true;assert(r2_lora_hw_tx(payload,80,5)==-EINTR);
  assert(set_tx_commands==1&&counters.launch_aborts==1&&counters.busy_aborts==1&&counters.last_busy_op==0x83&&!counters.cutoffs&&!counters.spi_error&&!atomic_get(&fault));
  assert(!pins[12]&&!pins[16]&&!pins[30]&&!atomic_get(&running));closed();busy_high=0;assert(!r2_lora_hw_start(6,false));r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==35){
  for(unsigned i=0;i<3;i++){
   busy_at_fem=true;assert(r2_lora_hw_tx(payload,80,5+i*2)==-EINTR);assert(!set_tx_commands&&counters.busy_aborts==i+1&&counters.launch_aborts==i+1&&!atomic_get(&fault));
   assert(r2_lora_hw_start(6+i*2,false)==-EBUSY);closed();busy_high=0;assert(!r2_lora_hw_start(6+i*2,false));
  }
  counters.busy_aborts=UINT16_MAX;counters.launch_aborts=UINT32_MAX;busy_at_fem=true;assert(r2_lora_hw_tx(payload,80,20)==-EINTR);assert(counters.busy_aborts==UINT16_MAX&&counters.launch_aborts==UINT32_MAX);closed();return 0;}
 if(mode==36){busy_high=1;assert(r2_lora_hw_tx(payload,80,5)==-ETIMEDOUT);
  assert(!set_tx_commands&&!counters.busy_aborts&&!counters.launch_aborts&&counters.last_busy_op==0x80&&atomic_get(&fault)==-ETIMEDOUT);closed();return 0;}
 if(mode==37){busy_at_fem=ready_at_busy_deadline=true;assert(!r2_lora_hw_tx(payload,80,5));
  assert(set_tx_commands==1&&!counters.busy_aborts&&!counters.launch_aborts&&!counters.last_busy_op&&!atomic_get(&fault));inject(1);struct r2_lora_hw_event e;assert(r2_lora_hw_take(&e)==1);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 if(mode==38){busy_at_fem=cutoff_at_busy_deadline=true;assert(r2_lora_hw_tx(payload,80,5)==-EINTR);
  assert(!set_tx_commands&&counters.launch_aborts==1&&!counters.busy_aborts&&!counters.cutoffs&&!atomic_get(&fault));closed();return 0;}
 if(mode==39){settx_error=-ETIMEDOUT;busy_on_settx=true;assert(r2_lora_hw_tx(payload,80,5)==-ETIMEDOUT);
  assert(set_tx_commands==1&&!counters.busy_aborts&&!counters.launch_aborts&&counters.spi_error==1&&atomic_get(&fault)==-ETIMEDOUT);closed();return 0;}
 if(mode==40){busy_at_fem=error_at_busy_deadline=true;assert(r2_lora_hw_tx(payload,80,5)==-EIO);
  assert(!set_tx_commands&&!counters.busy_aborts&&!counters.launch_aborts&&!counters.last_busy_op&&atomic_get(&fault)==-EIO);closed();return 0;}
 if(mode==41){
  suppress_dma=1;assert(r2_lora_hw_tx(payload,80,5)==-ETIMEDOUT);
  assert(wd.terminal&&wd.serial==1&&wd.record[0]==R2_LORA_FAULT_MAGIC);
  assert((int32_t)wd.record[1]==-ETIMEDOUT&&(wd.record[3]>>8)==R2_LORA_STAGE_DMA&&(wd.record[3]&255)==0x80);
  assert(wd.record[5]&(1u<<9));uint32_t original[16];memcpy(original,wd.record,64);
  struct r2_lora_hw_ack ack;r2_lora_hw_stop_poll(&ack);assert(wd.closure==22&&!ack.dma_retired&&!ack.capture_released);
  r2_lora_hw_watchdog_trip(-EIO);assert(!memcmp(original,wd.record,64)&&wd.coalesced);
  spi_done(&device,0,NULL);closed();assert(wd.closure==31&&!memcmp(original,wd.record,64));return 0;
 }
 if(mode==42){
  assert(!r2_lora_hw_tx(payload,80,5));unsigned before=submissions;
  r2_lora_hw_watchdog_trip(-ETIMEDOUT);
  assert(wd.terminal&&submissions==before&&wd.record[6]==5&&(wd.record[5]&255)==3);
  assert((wd.record[5]&(1u<<12))&&!atomic_get(&running)&&!pins[12]&&!pins[16]);closed();assert(wd.closure==31);
  assert(r2_lora_hw_start(6,false)==-ETIMEDOUT);return 0;
 }
 if(mode==43){
  busy_on_settx=true;assert(r2_lora_hw_tx(payload,80,5)==-EINTR);
  assert(!wd.terminal&&wd.serial==1&&(wd.record[3]&255)==0x83&&(wd.record[10]&(1u<<16)));
  uint32_t original[16];memcpy(original,wd.record,64);closed();busy_high=0;assert(!r2_lora_hw_start(6,false));
  busy_at_fem=true;assert(r2_lora_hw_tx(payload,80,7)==-EINTR);assert(wd.serial==1&&wd.coalesced&&!memcmp(original,wd.record,64));closed();
  r2_lora_hw_watchdog_trip(-EIO);assert(wd.serial==2&&wd.terminal&&(int32_t)wd.record[1]==-EIO);return 0;
 }
 if(mode==13){r2_lora_hw_stop_begin(mock_us+200000);closed();pins[21]=1;stuck_dio=true;assert(r2_lora_hw_start(6,true)==-EIO);assert(!pins[12]&&!pins[30]&&!pins[16]);closed();return 0;}
 if(mode==12){r2_lora_hw_stop_begin(mock_us+200000);closed();assert(!r2_lora_hw_start(6,true));assert(attach_count==2&&detach_count==1);r2_lora_hw_stop_begin(mock_us+200000);closed();return 0;}
 assert(!"unknown mode");
}

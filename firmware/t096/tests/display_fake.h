#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>
#include <string.h>
#include "ui_framebuffer.h"
struct device {int unused;};static struct device fake_spi;
enum {spi0,tft_cs,tft_dc,tft_reset,tft_backlight,tft_button,temp,clock};
#define DT_NODELABEL(n) n
#define DEVICE_DT_GET(n) (&fake_spi)
#define GPIO_DT_SPEC_GET(n,p) {.pin=n}
#define GPIO_OUTPUT_INACTIVE 0
#define GPIO_INPUT 1
#define GPIO_PULL_UP 2
#define SPI_OP_MODE_MASTER 0
#define SPI_WORD_SET(n) n
#define SPI_TRANSFER_MSB 0
#define K_POLL_TYPE_SIGNAL 0
#define K_POLL_MODE_NOTIFY_ONLY 0
#define K_MSEC(n) n
#define K_NO_WAIT 0
#define K_POLL_EVENT_INITIALIZER(a,b,s) {0}
struct gpio_dt_spec {int pin;};
struct spi_config {unsigned frequency,operation;};
struct k_poll_signal {unsigned signaled;int result;};
struct k_poll_event {int unused;};
struct spi_buf {void *buf;size_t len;};
struct spi_buf_set {struct spi_buf *buffers;size_t count;};
static unsigned delayed_completion;
static int64_t fake_now;static bool timeout;static unsigned transfers,largest;static int lights;static const void *pending_dma;
static unsigned fail_transfer,delay_transfer,transfer_delay;
static unsigned payload_transfers,payload_bytes;
static uint8_t wire[R2_UI_BYTES],dma_snapshot[R2_UI_DMA_BYTES];
static const uint8_t *payload_buffer;
static bool cs_active,dc_data;
static void (*spi_wait_hook)(const uint8_t *,size_t);
static bool device_is_ready(const struct device *d){return true;}
static bool gpio_is_ready_dt(const struct gpio_dt_spec *p){return true;}
static int gpio_pin_configure_dt(const struct gpio_dt_spec *p,unsigned flags){return 0;}
static int gpio_pin_set_dt(const struct gpio_dt_spec *p,int v){if(p->pin==tft_backlight)lights=v;if(p->pin==tft_cs)cs_active=v;if(p->pin==tft_dc)dc_data=v;return 0;}
static int gpio_pin_get_dt(const struct gpio_dt_spec *p){return 0;}
static int64_t k_uptime_get(void){return fake_now;}
static void k_msleep(int ms){fake_now+=ms;}
static int spi_write(const struct device *d,const struct spi_config *c,const struct spi_buf_set *b){
 assert(!pending_dma&&cs_active&&b->count==1);
 ++transfers;if(b->buffers[0].len>largest)largest=b->buffers[0].len;
 size_t size=b->buffers[0].len;assert(size<=sizeof dma_snapshot);
 pending_dma=b->buffers[0].buf;
 memcpy(dma_snapshot,pending_dma,size);
 if(size>16){
  assert(dc_data&&size==R2_UI_DMA_BYTES&&payload_bytes+size<=sizeof wire);
  if(!payload_buffer)payload_buffer=pending_dma;else assert(payload_buffer==pending_dma);
  memcpy(wire+payload_bytes,pending_dma,size);payload_bytes+=(unsigned)size;++payload_transfers;
 }
 if(spi_wait_hook)spi_wait_hook(pending_dma,size);
 assert(!memcmp(dma_snapshot,pending_dma,size));
 fake_now+=(b->buffers[0].len*8*1000+c->frequency-1)/c->frequency;
 fake_now+=timeout?10:delayed_completion;delayed_completion=0;
 if(transfers==delay_transfer)fake_now+=transfer_delay;
 pending_dma=NULL; /* public synchronous completion or bounded driver abort */
 return timeout||transfers==fail_transfer?-116:0;
}

#define SENSOR_CHAN_DIE_TEMP 1
struct sensor_value {int32_t val1,val2;};
static int temp_cal,temp_skip,temp_read_error;
static struct sensor_value temp_value;
static int z_nrf_clock_calibration_count(void){return temp_cal;}
static int z_nrf_clock_calibration_skips_count(void){return temp_skip;}
static int sensor_channel_get(const struct device *d,int chan,struct sensor_value *v){*v=temp_value;return temp_read_error;}
#if defined(CONFIG_NRFX_TEMP)
enum {CLOCK_CONTROL_NRF_SUBSYS_HF,CLOCK_CONTROL_STATUS_OFF,CLOCK_CONTROL_STATUS_ON,NRFX_SUCCESS=0,NRFX_ERROR_ALREADY=1,NRFX_ERROR_INTERNAL=2};
typedef unsigned nrfx_err_t;
typedef struct {unsigned interrupt_priority;} nrfx_temp_config_t;
static unsigned hf_status=CLOCK_CONTROL_STATUS_ON,temp_init_error,temp_measure_error,temp_elapsed;
static unsigned temp_init_calls,temp_measure_calls,temp_uninit_calls;
static int32_t temp_raw;static bool temp_owned;
static unsigned clock_control_get_status(const struct device *d,unsigned s){return hf_status;}
static nrfx_err_t nrfx_temp_init(const nrfx_temp_config_t *cfg,void *handler){assert(!handler&&cfg->interrupt_priority==6&&!temp_owned);++temp_init_calls;if(temp_init_error)return temp_init_error;temp_owned=true;return NRFX_SUCCESS;}
static nrfx_err_t nrfx_temp_measure(void){assert(temp_owned);++temp_measure_calls;fake_now+=temp_elapsed;return temp_measure_error;}
static int32_t nrfx_temp_result_get(void){assert(temp_owned);return temp_raw;}
static void nrfx_temp_uninit(void){assert(temp_owned);temp_owned=false;++temp_uninit_calls;}
#endif
struct pwm_dt_spec {uint32_t period;};
#define PWM_DT_SPEC_GET(n) {.period=1000000}
static unsigned pwm_calls,pwm_pulse;static int pwm_error;
static bool pwm_is_ready_dt(const struct pwm_dt_spec *p){return true;}
static int pwm_set_pulse_dt(const struct pwm_dt_spec *p,uint32_t pulse){++pwm_calls;if(pwm_error)return pwm_error;pwm_pulse=pulse;return 0;}

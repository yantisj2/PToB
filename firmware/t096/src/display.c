/* SPDX-License-Identifier: Apache-2.0
 * ST7735 setup retained from src/display.c; public Zephyr SPI/GPIO ownership. */
#include "ui.h"
#include <errno.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#if (defined(R2_RC_PROFILE) || defined(R2_DC_PROFILE)) && defined(CONFIG_CLOCK_CONTROL_NRF_K32SRC_SYNTH) && defined(CONFIG_NRFX_TEMP)
#include <zephyr/drivers/clock_control.h>
#include <nrfx_temp.h>
#endif
static const struct device *spi=DEVICE_DT_GET(DT_NODELABEL(spi0));
#define PIN(name) GPIO_DT_SPEC_GET(DT_NODELABEL(name),gpios)
static const struct gpio_dt_spec cs=PIN(tft_cs),dc=PIN(tft_dc),reset=PIN(tft_reset),light=PIN(tft_backlight),button=PIN(tft_button);
static const struct pwm_dt_spec backlight=PWM_DT_SPEC_GET(DT_NODELABEL(ui_backlight));
static uint8_t brightness=UINT8_MAX;
static const struct spi_config config={.frequency=8000000,.operation=SPI_OP_MODE_MASTER|SPI_WORD_SET(8)|SPI_TRANSFER_MSB};
static struct spi_buf tx;
static struct spi_buf_set tx_set={.buffers=&tx,.count=1};
static uint8_t command_bytes[16];
/* Persistent EasyDMA storage: only this sleeping synchronous SPI owner writes
 * it, and only after the prior public completion/abort has retired DMA. */
_Alignas(4) static uint8_t frame_dma[R2_UI_DMA_BYTES];
static int64_t transaction_deadline;
static int transfer(const uint8_t *data,size_t size)
{
 /* Pinned synchronous SPIM owns timeout/STOP/DISABLE cleanup. Reserve the
  * entire computed wire time +10ms tolerance +1ms cleanup before submitting.
  * A failed frame remains failed; no new call borrows beyond its deadline. */
 int64_t reserve=(size*8*1000+config.frequency-1)/config.frequency+11;
 if(transaction_deadline-k_uptime_get()<reserve)return -ETIMEDOUT;
 tx.buf=(void *)data;tx.len=size;
 int rc=spi_write(spi,&config,&tx_set);
 return k_uptime_get()>=transaction_deadline?-ETIMEDOUT:rc;
}
static bool command(uint8_t value,const uint8_t *args,size_t n)
{
 if(n>sizeof(command_bytes))return false;
 if(gpio_pin_set_dt(&cs,1)||gpio_pin_set_dt(&dc,0))return false;
 command_bytes[0]=value;int rc=transfer(command_bytes,1);
 if(!rc&&n){memcpy(command_bytes,args,n);rc=gpio_pin_set_dt(&dc,1);if(!rc)rc=transfer(command_bytes,n);}
 {int end=gpio_pin_set_dt(&cs,0);if(!rc)rc=end;}
 return rc==0;
}
int r2_display_init(void)
{
 transaction_deadline=k_uptime_get()+500; /* Entire initialization, including sleeps. */
 if(!device_is_ready(spi))return -ENODEV;
 if(!pwm_is_ready_dt(&backlight))return -ENODEV;
 const struct gpio_dt_spec *pins[]={&cs,&dc,&reset,&light,&button};
 for(unsigned i=0;i<5;i++)if(!gpio_is_ready_dt(pins[i]))return -ENODEV;
 int rc=gpio_pin_configure_dt(&cs,GPIO_OUTPUT_INACTIVE);
 if(!rc)rc=gpio_pin_configure_dt(&dc,GPIO_OUTPUT_INACTIVE);
 if(!rc)rc=gpio_pin_configure_dt(&reset,GPIO_OUTPUT_INACTIVE);
 if(!rc)rc=gpio_pin_configure_dt(&light,GPIO_OUTPUT_INACTIVE);
 if(!rc)rc=gpio_pin_configure_dt(&button,GPIO_INPUT|GPIO_PULL_UP);
 if(rc)return rc;
 rc=gpio_pin_set_dt(&reset,1);if(rc)return rc;k_msleep(10);
 rc=gpio_pin_set_dt(&reset,0);if(rc)return rc;k_msleep(10);
 bool okay=command(0x01,0,0);k_msleep(150);okay&=command(0x11,0,0);k_msleep(120);
    const uint8_t frame_rate[] = {0x01u, 0x2Cu, 0x2Du};
    const uint8_t frame_partial[] = {0x01u, 0x2Cu, 0x2Du, 0x01u, 0x2Cu, 0x2Du};
    const uint8_t power1[] = {0xA2u, 0x02u, 0x84u};
    const uint8_t power3[] = {0x0Au, 0x00u};
    const uint8_t power4[] = {0x8Au, 0x2Au};
    const uint8_t power5[] = {0x8Au, 0xEEu};
    const uint8_t gamma_pos[] = {
        0x02u,0x1Cu,0x07u,0x12u,0x37u,0x32u,0x29u,0x2Du,
        0x29u,0x25u,0x2Bu,0x39u,0x00u,0x01u,0x03u,0x10u
    };
    const uint8_t gamma_neg[] = {
        0x03u,0x1Du,0x07u,0x06u,0x2Eu,0x2Cu,0x29u,0x2Du,
        0x2Eu,0x2Eu,0x37u,0x3Fu,0x00u,0x00u,0x02u,0x10u
    };
    const uint8_t colmod = 0x05u;
    const uint8_t madctl = 0x08u;
    const uint8_t inversion = 0x07u;
    const uint8_t power2 = 0xC5u;
    const uint8_t vcom = 0x0Eu;
    okay &= command(0xB1u, frame_rate, sizeof frame_rate);
    okay &= command(0xB2u, frame_rate, sizeof frame_rate);
    okay &= command(0xB3u, frame_partial, sizeof frame_partial);
    okay &= command(0xB4u, &inversion, 1);
    okay &= command(0xC0u, power1, sizeof power1);
    okay &= command(0xC1u, &power2, 1);
    okay &= command(0xC2u, power3, sizeof power3);
    okay &= command(0xC3u, power4, sizeof power4);
    okay &= command(0xC4u, power5, sizeof power5);
    okay &= command(0xC5u, &vcom, 1);
    okay &= command(0x20u, 0, 0); /* INVOFF */
    okay &= command(0x36u, &madctl, 1);
    okay &= command(0x3Au, &colmod, 1); /* RGB565 */
    okay &= command(0xE0u, gamma_pos, sizeof gamma_pos);
    okay &= command(0xE1u, gamma_neg, sizeof gamma_neg);
    okay &= command(0x13u, 0, 0); /* NORON */
    k_msleep(10);
    okay &= command(0x29u, 0, 0); /* DISPON */
    k_msleep(20);

 return k_uptime_get()>=transaction_deadline?-ETIMEDOUT:okay?0:-EIO;
}
int r2_display_frame(const uint8_t *fb)
{
 transaction_deadline=k_uptime_get()+250;
 const uint8_t col[]={0,24,0,103},row[]={0,0,0,159};
 if(!command(0x2a,col,4)||!command(0x2b,row,4)||!command(0x2c,0,0))return -EIO;
 int rc=gpio_pin_set_dt(&cs,1);if(!rc)rc=gpio_pin_set_dt(&dc,1);
 /* Five32-row batches retain the exact RGB565 wire pixels without a second
  * full frame. Keep CS asserted and RAMWR active throughout; BLE can preempt
  * each sleeping public SPI call. All batches share the original250ms bound.
  * On any error, stop before rewriting storage or submitting another batch. */
 for(size_t first=0;!rc&&first<R2_UI_PIXELS;first+=R2_UI_DMA_PIXELS){
  if(!r2_ui_expand_rgb565(frame_dma,sizeof frame_dma,fb+first,R2_UI_DMA_PIXELS))rc=-EINVAL;
  else rc=transfer(frame_dma,sizeof frame_dma);
 }
 {int done=gpio_pin_set_dt(&cs,0);if(!rc)rc=done;}
 return rc;
}
/* Only called by the sole UI owner after spi_write returned and the public
 * driver completed/aborted its transfer. No application peripheral reset. */
int r2_display_recover(void){return gpio_pin_set_dt(&cs,0);}
int r2_display_button(void){return gpio_pin_get_dt(&button);}
int r2_display_brightness(uint8_t percent){
 if(percent!=50&&percent!=100)return -EINVAL;
 if(percent==brightness)return 0;
 /* Keep PWM0 running at every healthy brightness. TOP-1 for full brightness
  * differs by <0.007%, avoids the driver's stop/restart busy wait, and uses
  * no application IRQ, TIMER/PPI channel or controller clock request. */
 uint32_t pulse=percent==100?backlight.period-1:backlight.period*percent/100;
 int rc=pwm_set_pulse_dt(&backlight,pulse);if(!rc)brightness=percent;return rc;
}
int r2_display_off(void){if(!pwm_is_ready_dt(&backlight))return -ENODEV;int rc=pwm_set_pulse_dt(&backlight,0);if(!rc)brightness=0;return rc;}

/* LFRC calibration is the existing TEMP owner. Read its completed cache only;
 * never issue another sample_fetch/HF request or take its blocking mutex. */
int r2_display_temperature(int16_t *quarter_c)
{
#if (defined(R2_RC_PROFILE) || defined(R2_DC_PROFILE)) && defined(CONFIG_CLOCK_CONTROL_NRF_K32SRC_SYNTH)
#if defined(CONFIG_NRFX_TEMP)
 /* Sole UI TEMP owner on synthesized-LF images. The stock LF owner already
  * holds HFXO; never request/release it, enable an IRQ, or touch TIMER/PPI.
  * Pinned nrfx blocking mode attempts100 checks at4us and always stops TEMP.
  * Preemption beyond2ms remains a failed sample, with the driver released.
  * One conversion per4s; failures immediately remove temperature/color. */
 static uint64_t due,observed;
 static int16_t cached;
 static bool valid;
 uint64_t now=k_uptime_get();
 if(now>=due){
  due=now+4000;valid=false;
  const struct device *clock=DEVICE_DT_GET(DT_NODELABEL(clock));
  if(!device_is_ready(clock)||clock_control_get_status(clock,CLOCK_CONTROL_NRF_SUBSYS_HF)!=CLOCK_CONTROL_STATUS_ON)return -EAGAIN;
  const nrfx_temp_config_t cfg={.interrupt_priority=6};
  if(nrfx_temp_init(&cfg,NULL)!=NRFX_SUCCESS)return -EBUSY;
  nrfx_err_t rc=nrfx_temp_measure();
  int32_t q=rc==NRFX_SUCCESS?nrfx_temp_result_get():0;
  nrfx_temp_uninit();
  uint64_t end=k_uptime_get();
  if(rc!=NRFX_SUCCESS||end<now||end-now>=2||q<-160||q>340)return -EAGAIN;
  cached=(int16_t)q;observed=end;valid=true;now=end;
 }
 if(!valid||now<observed||now-observed>12000)return -EAGAIN;
 *quarter_c=cached;return 0;
#else
 (void)quarter_c;return -ENOTSUP;
#endif
#else
 static int prior_cal=-1,prior_skip=-1;
 static int64_t observed;
 static int16_t cached;
 static bool valid;
 const struct device *dev=DEVICE_DT_GET(DT_NODELABEL(temp));
 int cal=z_nrf_clock_calibration_count(),skip=z_nrf_clock_calibration_skips_count();
 int64_t now=k_uptime_get();
 /* A skipped cycle proves an actual successful TEMP read. Initial forced
  * calibration can precede sensor initialization and is insufficient. */
 if(!device_is_ready(dev)||cal<0||skip<=0)return -EAGAIN;
 if(cal!=prior_cal||skip!=prior_skip){
  struct sensor_value value;
  prior_cal=cal;prior_skip=skip;
  int rc=sensor_channel_get(dev,SENSOR_CHAN_DIE_TEMP,&value);
  int64_t q=rc?0:(int64_t)value.val1*4+value.val2/250000;
  valid=!rc&&q>=-160&&q<=340;
  if(valid){cached=(int16_t)q;observed=now;}
 }
 if(!valid||now<observed||now-observed>12000)return -EAGAIN;
 *quarter_c=cached;return 0;
#endif
}

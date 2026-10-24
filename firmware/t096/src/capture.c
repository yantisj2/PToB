#if defined(R2_GM7_NATIVE)
#include "gm7_clock.h"
#include "gm7_controller.h"
#endif
/* SPDX-License-Identifier: Apache-2.0
 * Pinned nRF52840 capture adapter: Zephyr owns GPIOTE IRQ; nrfx allocates PPI
 * excluding the compiled controller mask. No CLOCK/RADIO task writes.
 */
#include "capture.h"
#ifndef R2_DC_PROFILE
#include "radio_capture.h"
#include "radio_nrf5_resources.h"
#endif
#include <errno.h>
#include <nrfx_gpiote.h>
#include <nrfx_ppi.h>
#include <nrfx_timer.h>
#ifndef R2_DC_PROFILE
#include <hal/nrf_radio.h>
#endif
#ifdef R2_USB_SOF_PROBE
#include <hal/nrf_usbd.h>
BUILD_ASSERT(NRF_TIMER_CC_CHANNEL3==3);
BUILD_ASSERT(USBD_FRAMECNTR_FRAMECNTR_Msk==0x7ffu);
static nrf_ppi_channel_t usb_channel;
static bool usb_owned;
#endif
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/onoff.h>
#define R2_LOCK_UNIT 4u
#include "health_lock.h"
BUILD_ASSERT(!DT_NODE_HAS_STATUS(DT_NODELABEL(timer3), okay));
BUILD_ASSERT(IS_ENABLED(CONFIG_SOC_NRF52840));
#ifndef R2_DC_PROFILE
BUILD_ASSERT(EVENT_TIMER_ID != 3);
/* Pinned non-single timer profile uses TIMER0+TIMER1. Other profiles require a
 * new resource audit before selecting this application timer. */
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_CTLR_SW_SWITCH_SINGLE_TIMER));
BUILD_ASSERT(!IS_ENABLED(CONFIG_BT_CTLR_TIFS_HW));
#endif
static const nrfx_timer_t timer = NRFX_TIMER_INSTANCE(3);
static const nrfx_gpiote_t gpiote = NRFX_GPIOTE_INSTANCE(0);
static struct onoff_client client;
static struct onoff_manager *manager;
static struct gpio_dt_spec pin;
static struct k_spinlock lock;
static nrf_ppi_channel_t channel;
#if !defined(R2_DC_PROFILE) && !defined(R2_PC_PROFILE)
static nrf_ppi_channel_t lora_channel;
static bool lora_owned;
BUILD_ASSERT(TIMER3_CC_NUM >= 5);
#endif
#ifndef R2_DC_PROFILE
static nrf_ppi_channel_t radio_channel;
static bool radio_owned,radio_enabled;
#if defined(R2_GM7_NATIVE)
static nrf_ppi_channel_t gm7_end_route;
static bool gm7_end_owned,gm7_enabled;
BUILD_ASSERT(TIMER3_CC_NUM>=6);
#endif
#endif
static bool requested, timer_owned, ppi_owned, used, pending;
static atomic_t accepting, error;
static uint64_t epoch, extended;
static uint32_t previous_raw;
static int64_t request_at, previous_mono;
static void fault(int rc) {
#if defined(R2_GM7_NATIVE)
  r2_gm7_clock_invalidate();
#endif
  if (rc)
    atomic_cas(&error, 0, rc);
  atomic_clear(&accepting);
}
int r2_capture_error(void) { return atomic_get(&error); }
/* IRQ and thread reads share this lock; kernel time only detects lost counter
 * continuity. The event value itself always comes from hardware CC0. */
static bool extend(uint32_t raw, int64_t now) {
  if (now < previous_mono || now - previous_mono > 1000000) {
    fault(-ESTALE);
    return false;
  }
  uint32_t delta = raw - previous_raw;
  if (delta >= UINT32_C(0x80000000) || extended > UINT64_MAX - delta) {
    fault(-ERANGE);
    return false;
  }
  extended += delta;
  previous_raw = raw;
  previous_mono = now;
#if defined(R2_GM7_NATIVE)
  r2_gm7_clock_publish(extended,raw,epoch,true);
#endif
  return true;
}
int r2_capture_start(const struct gpio_dt_spec *pps, int64_t now) {
  if (used)
    return -EALREADY;
  used = true;
  pin = *pps;
  /* Board PPS is P1.11. Reject another endpoint rather than guessing a route.
   */
  if (pin.port != DEVICE_DT_GET(DT_NODELABEL(gpio1)) || pin.pin != 11)
    return -EINVAL;
  manager = z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
  sys_notify_init_spinwait(&client.notify);
  int rc = onoff_request(manager, &client);
  if (rc < 0) {
    fault(rc);
    return rc;
  }
  requested = pending = true;
  request_at = now;
  return 0;
}
void r2_capture_poll(int64_t now) {
  if (pending) {
    int result = -EAGAIN;
    if (now < request_at || now - request_at >= 20000) {
      fault(-ETIMEDOUT);
      pending = false;
      (void)r2_capture_stop();
      return;
    }
    if (sys_notify_fetch_result(&client.notify, &result))
      return;
    pending = false;
    if (result) {
      fault(result);
      (void)r2_capture_stop();
      return;
    }
    nrfx_timer_config_t config = NRFX_TIMER_DEFAULT_CONFIG(16000000);
    config.bit_width = NRF_TIMER_BIT_WIDTH_32;
    config.mode = NRF_TIMER_MODE_TIMER;
    int rc = nrfx_timer_init(&timer, &config, NULL) == NRFX_SUCCESS ? 0 : -EIO;
    if (!rc)
      timer_owned = true;
    if (!rc) {
      rc = nrfx_ppi_channel_alloc(&channel) == NRFX_SUCCESS ? 0 : -ENOMEM;
      if (!rc)
        ppi_owned = true;
    }
#ifndef R2_DC_PROFILE
    if (!rc) {
      rc = nrfx_ppi_channel_alloc(&radio_channel) == NRFX_SUCCESS ? 0 : -ENOMEM;
      if (!rc) radio_owned = true;
    }
    if (!rc && nrfx_ppi_channel_assign(radio_channel,
          nrf_radio_event_address_get(NRF_RADIO,NRF_RADIO_EVENT_ADDRESS),
          nrfx_timer_capture_task_address_get(&timer,1)) != NRFX_SUCCESS)
      rc = -EIO;
#endif
#if defined(R2_GM7_NATIVE)
    if(!rc){rc=nrfx_ppi_channel_alloc(&gm7_end_route)==NRFX_SUCCESS?0:-ENOMEM;if(!rc)gm7_end_owned=true;}
    if(!rc&&nrfx_ppi_channel_assign(gm7_end_route,
      nrf_radio_event_address_get(NRF_RADIO,NRF_RADIO_EVENT_END),
      nrfx_timer_capture_task_address_get(&timer,5))!=NRFX_SUCCESS)rc=-EIO;
#endif
    /* Disable retains the GPIO driver's allocated IN channel. Re-enable clears
     * stale event state before its event and IRQ are enabled in the pinned
     * driver. */
    if (!rc)
      rc = gpio_pin_interrupt_configure_dt(&pin, GPIO_INT_DISABLE);
    uint8_t gchannel = 0;
    if (!rc && nrfx_gpiote_channel_get(&gpiote, NRF_GPIO_PIN_MAP(1, 11),
                                       &gchannel) != NRFX_SUCCESS)
      rc = -ENOTSUP;
    uint32_t event = 0;
    if (!rc) {
      event =
          nrfx_gpiote_in_event_address_get(&gpiote, NRF_GPIO_PIN_MAP(1, 11));
      if (event != nrf_gpiote_event_address_get(
                       gpiote.p_reg, nrf_gpiote_in_event_get(gchannel)))
        rc = -ENOTSUP;
    }
    if (!rc && nrfx_ppi_channel_assign(channel, event,
                                       nrfx_timer_capture_task_address_get(
                                           &timer, 0)) != NRFX_SUCCESS)
      rc = -EIO;
#ifdef R2_USB_SOF_PROBE
    if(!rc){
      rc=nrfx_ppi_channel_alloc(&usb_channel)==NRFX_SUCCESS?0:-ENOMEM;
      if(!rc)usb_owned=true;
    }
    if(!rc&&nrfx_ppi_channel_assign(usb_channel,
          nrf_usbd_event_address_get(NRF_USBD,NRF_USBD_EVENT_SOF),
          nrfx_timer_capture_task_address_get(&timer,3))!=NRFX_SUCCESS)rc=-EIO;
#endif
    if (!rc) {
      nrfx_timer_enable(&timer);
      previous_raw = nrfx_timer_capture(&timer, NRF_TIMER_CC_CHANNEL2);
      extended = previous_raw;
      previous_mono = now;
      ++epoch;
#ifndef R2_DC_PROFILE
      rc = r2_radio_capture_enable((uint32_t)epoch,radio_channel);
      if (!rc) radio_enabled = true;
#if defined(R2_GM7_NATIVE)
      if(!rc){rc=r2_gm7_capture_enable((uint32_t)epoch,radio_channel,gm7_end_route);if(!rc)gm7_enabled=true;}
#endif
#endif
      if (!rc && nrfx_ppi_channel_enable(channel) != NRFX_SUCCESS)
        rc = -EIO;
#ifdef R2_USB_SOF_PROBE
      if(!rc&&nrfx_ppi_channel_enable(usb_channel)!=NRFX_SUCCESS)rc=-EIO;
#endif
      if (!rc){
        atomic_set(&accepting, 1);
#if defined(R2_GM7_NATIVE)
        k_spinlock_key_t gm7_key=k_spin_lock(&lock);
        r2_gm7_clock_begin((uint32_t)epoch);
        r2_gm7_clock_publish(extended,previous_raw,epoch,true);
        k_spin_unlock(&lock,gm7_key);
#endif
      }
    }
    int gpio_rc =
        gpio_pin_interrupt_configure_dt(&pin, GPIO_INT_EDGE_TO_ACTIVE);
    if (!rc)
      rc = gpio_rc;
    uint8_t after = 255;
    if (!rc && (nrfx_gpiote_channel_get(&gpiote, NRF_GPIO_PIN_MAP(1, 11),
                                        &after) != NRFX_SUCCESS ||
                after != gchannel))
      rc = -EFAULT;
    if (rc) {
      fault(rc);
      (void)r2_capture_stop();
      return;
    }
  }
  if (atomic_get(&accepting)) {
    k_spinlock_key_t key = k_spin_lock(&lock);
    (void)extend(nrfx_timer_capture(&timer, NRF_TIMER_CC_CHANNEL2),
                 k_ticks_to_us_floor64(k_uptime_ticks()));
    k_spin_unlock(&lock, key);
  }
}
bool r2_capture_pps(struct r2_capture *edge, int64_t now) {
  (void)now; /* Callback arrival is not the serialized counter-read time. */
  bool valid = false;
  k_spinlock_key_t key = k_spin_lock(&lock);
  if (atomic_get(&accepting)) {
    uint32_t raw = nrfx_timer_capture_get(&timer, NRF_TIMER_CC_CHANNEL0);
    uint32_t current = nrfx_timer_capture(&timer, NRF_TIMER_CC_CHANNEL2);
    uint32_t age = current - raw;
    /* Another reader may have extended the shared timer since IRQ entry.
     * Test continuity at this locked hardware read, not at old arrival time.
     * A stale PPS CC value rejects only this input; it cannot invalidate a
     * healthy timer used by BLE. Real extension faults remain latched. */
    if (extend(current, k_ticks_to_us_floor64(k_uptime_ticks())) &&
        age <= 4000000 && extended >= age) {
      *edge = (struct r2_capture){extended - age, epoch, R2_CAPTURE_PPS};
      valid = true;
    }
  }
  k_spin_unlock(&lock, key);
  return valid;
}
int r2_capture_stop(void) {
#if defined(R2_GM7_NATIVE)
  r2_gm7_clock_invalidate();
#endif
  k_spinlock_key_t key = k_spin_lock(&lock);
#if !defined(R2_DC_PROFILE) && !defined(R2_PC_PROFILE)
  /* LoRa must acknowledge its independent route release before this common
   * timer/HFXO owner can close. Do not break healthy BLE on a local fault. */
  if(lora_owned){k_spin_unlock(&lock,key);return -EBUSY;}
#endif
  atomic_clear(&accepting);
  pending = false;
  int rc = 0;
#ifdef R2_USB_SOF_PROBE
  if(usb_owned){
    if(nrfx_ppi_channel_disable(usb_channel)!=NRFX_SUCCESS||
       nrfx_ppi_channel_free(usb_channel)!=NRFX_SUCCESS)rc=-EIO;
    else usb_owned=false;
  }
#endif
#ifndef R2_DC_PROFILE
#if defined(R2_GM7_NATIVE)
  if(!rc&&gm7_enabled){rc=r2_gm7_capture_disable();if(!rc)gm7_enabled=false;}
  if(!rc&&gm7_end_owned){
   if(nrfx_ppi_channel_disable(gm7_end_route)!=NRFX_SUCCESS||nrfx_ppi_channel_free(gm7_end_route)!=NRFX_SUCCESS)rc=-EIO;
   else gm7_end_owned=false;
  }
#endif
  if (!rc && radio_enabled) {
    rc = r2_radio_capture_disable();
    if (!rc) radio_enabled = false;
  }
  if (!rc && radio_owned) {
    if (nrfx_ppi_channel_disable(radio_channel) != NRFX_SUCCESS ||
        nrfx_ppi_channel_free(radio_channel) != NRFX_SUCCESS) rc = -EIO;
    else radio_owned = false;
  }
#endif
  if (!rc && ppi_owned) {
    if (nrfx_ppi_channel_disable(channel) != NRFX_SUCCESS)
      rc = -EIO;
    if (!rc) {
      if (nrfx_ppi_channel_free(channel) != NRFX_SUCCESS)
        rc = -EIO;
      else
        ppi_owned = false;
    }
  }
  if (!rc && timer_owned) {
    nrfx_timer_disable(&timer);
    nrfx_timer_uninit(&timer);
    timer_owned = false;
  }
  k_spin_unlock(&lock, key);
  if (!rc && requested) {
    rc = onoff_cancel_or_release(manager, &client);
    if (rc >= 0) {
      requested = false;
      rc = 0;
    }
  }
  if (rc)
    fault(rc);
  return rc;
}
bool r2_capture_now_at(struct r2_capture *current,uint64_t *sampled_us) {
  bool valid=false;
  k_spinlock_key_t key=k_spin_lock(&lock);
  if(atomic_get(&accepting)){
    uint32_t raw=nrfx_timer_capture(&timer,NRF_TIMER_CC_CHANNEL2);
    int64_t mono=k_ticks_to_us_floor64(k_uptime_ticks());
    if(extend(raw,mono)){
      *current=(struct r2_capture){extended,epoch,R2_CAPTURE_NOW};
      if(sampled_us)*sampled_us=(uint64_t)mono;
      valid=true;
    }
  }
  k_spin_unlock(&lock,key);return valid;
}
bool r2_capture_now(struct r2_capture *current){return r2_capture_now_at(current,NULL);}
bool r2_capture_radio(uint32_t raw,uint32_t event_epoch,struct r2_capture *event)
{
  bool valid=false;
  k_spinlock_key_t key=k_spin_lock(&lock);
  if(atomic_get(&accepting)&&epoch==event_epoch){
    uint32_t current=nrfx_timer_capture(&timer,NRF_TIMER_CC_CHANNEL2);
    uint32_t age=current-raw;
    if(extend(current,k_ticks_to_us_floor64(k_uptime_ticks()))&&age<=4000000&&extended>=age){
      *event=(struct r2_capture){extended-age,epoch,R2_CAPTURE_RADIO};valid=true;
    }
  }
  k_spin_unlock(&lock,key);return valid;
}
#if !defined(R2_DC_PROFILE) && !defined(R2_PC_PROFILE)
int r2_capture_lora_attach(const struct gpio_dt_spec *dio,uint32_t fem_clear_task)
{
  if(!dio||dio->port!=DEVICE_DT_GET(DT_NODELABEL(gpio0))||dio->pin!=21)return -EINVAL;
  if(fem_clear_task!=nrfx_gpiote_clr_task_address_get(&gpiote,NRF_GPIO_PIN_MAP(0,12)))return -EINVAL;
  k_spinlock_key_t key=k_spin_lock(&lock);
  int rc=0;uint8_t gchannel;
  if(!atomic_get(&accepting))rc=-EAGAIN;
  else if(lora_owned)rc=-EALREADY;
  else if(nrfx_gpiote_channel_get(&gpiote,NRF_GPIO_PIN_MAP(0,21),&gchannel)!=NRFX_SUCCESS)rc=-ENOTSUP;
  else {
    uint32_t event=nrfx_gpiote_in_event_address_get(&gpiote,NRF_GPIO_PIN_MAP(0,21));
    if(event!=nrf_gpiote_event_address_get(gpiote.p_reg,nrf_gpiote_in_event_get(gchannel)))rc=-ENOTSUP;
    else if(nrfx_ppi_channel_alloc(&lora_channel)!=NRFX_SUCCESS)rc=-ENOMEM;
    else {
      lora_owned=true;
      if(nrfx_ppi_channel_assign(lora_channel,event,
           nrfx_timer_capture_task_address_get(&timer,4))!=NRFX_SUCCESS||
         nrfx_ppi_channel_fork_assign(lora_channel,fem_clear_task)!=NRFX_SUCCESS||
         nrfx_ppi_channel_enable(lora_channel)!=NRFX_SUCCESS)rc=-EIO;
      if(rc&&nrfx_ppi_channel_disable(lora_channel)==NRFX_SUCCESS&&
          nrfx_ppi_channel_fork_assign(lora_channel,0)==NRFX_SUCCESS&&
          nrfx_ppi_channel_free(lora_channel)==NRFX_SUCCESS)lora_owned=false;
    }
  }
  k_spin_unlock(&lock,key);return rc;
}
bool r2_capture_lora(struct r2_capture *event)
{
  bool valid=false;k_spinlock_key_t key=k_spin_lock(&lock);
  if(lora_owned&&atomic_get(&accepting)){
    uint32_t raw=nrfx_timer_capture_get(&timer,NRF_TIMER_CC_CHANNEL4);
    uint32_t current=nrfx_timer_capture(&timer,NRF_TIMER_CC_CHANNEL2),age=current-raw;
    /* IRQ arrival is not the event time. A delayed/stale CC only rejects
     * LoRa; it must never fault an otherwise continuous BLE capture clock. */
    if(extend(current,k_ticks_to_us_floor64(k_uptime_ticks()))&&age<=8u*R2_CAPTURE_HZ&&extended>=age){
      *event=(struct r2_capture){extended-age,epoch,R2_CAPTURE_LORA};valid=true;
    }
  }
  k_spin_unlock(&lock,key);return valid;
}
int r2_capture_lora_detach(void)
{
  int rc=0;k_spinlock_key_t key=k_spin_lock(&lock);
  if(lora_owned){
    if(nrfx_ppi_channel_disable(lora_channel)!=NRFX_SUCCESS||
       nrfx_ppi_channel_fork_assign(lora_channel,0)!=NRFX_SUCCESS||
       nrfx_ppi_channel_free(lora_channel)!=NRFX_SUCCESS)rc=-EIO;
    else lora_owned=false;
  }
  k_spin_unlock(&lock,key);return rc;
}
#endif
#ifdef R2_USB_SOF_PROBE
bool r2_capture_usb_sample(struct r2_usb_sof_snapshot *out)
{
  bool valid=false;
  k_spinlock_key_t key=k_spin_lock(&lock);
  /* The USB class additionally requires its configured, nonsuspended epoch.
   * No USBD event is cleared and no peripheral task is triggered here. */
  if(atomic_get(&accepting)&&usb_owned&&epoch<=UINT32_MAX&&
     NRF_USBD->ENABLE==USBD_ENABLE_ENABLE_Enabled&&!NRF_USBD->LOWPOWER){
    uint16_t before=(uint16_t)nrf_usbd_framecntr_get(NRF_USBD);
    uint32_t first=nrfx_timer_capture_get(&timer,NRF_TIMER_CC_CHANNEL3);
    uint32_t current=nrfx_timer_capture(&timer,NRF_TIMER_CC_CHANNEL2);
    uint32_t last=nrfx_timer_capture_get(&timer,NRF_TIMER_CC_CHANNEL3);
    uint16_t after=(uint16_t)nrf_usbd_framecntr_get(NRF_USBD);
    if(extend(current,k_ticks_to_us_floor64(k_uptime_ticks()))){
      *out=(struct r2_usb_sof_snapshot){.current_tick=extended,.cc_before=first,
        .cc_after=last,.frame_before=before,.frame_after=after,.capture_epoch=(uint32_t)epoch};
      valid=true;
    }
  }
  k_spin_unlock(&lock,key);return valid;
}
#endif

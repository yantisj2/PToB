/* Copyright(c)2026 Jonathan Yantis. MIT harness; extracted Apache-2.0 bodies.
 * Selected nRF52840 masks/helpers; explicit event/async-disable model. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define BIT(n) (UINT32_C(1)<<(n))
#define EVENT_TIMER_ID 0
#define CONFIG_BT_CTLR_LE_ENC 1
#include "radio_nrf5_ppi_resources.h"
#define SW_SWITCH_TIMER_TASK_GROUP(index) (SW_SWITCH_TIMER_TASK_GROUP_BASE+(index))
static struct {uint32_t SHORTS;}radio;
#define NRF_RADIO (&radio)
#define NRF_PPI ((void*)1)
#define NRF_RTC ((void*)2)
#define RTC_EVTENCLR_COMPARE2_Msk 4
#define NRF_RADIO_TASK_DISABLE 1
static uint32_t enabled;
static bool rtc_event,active,disabled_irq;
static unsigned task_calls,reset_calls;
static void compare_event(void){if(enabled&(BIT(20)|BIT(21)))active=true;}
static void nrf_ppi_channels_enable(void *p,uint32_t mask){(void)p;enabled|=mask;}
static void nrf_ppi_channels_disable(void *p,uint32_t mask){(void)p;enabled&=~mask;}
static void nrf_ppi_group_disable(void *p,unsigned group){(void)p;assert(group<2);}
static void nrf_rtc_event_disable(void *p,uint32_t mask){(void)p;assert(mask==4);rtc_event=false;++reset_calls;}
static void nrf_radio_task_trigger(void *p,unsigned task){(void)p;assert(task==1&&!radio.SHORTS);
#ifdef FIXED
assert(!(enabled&(BIT(20)|BIT(21)))&&!rtc_event&&reset_calls);
#endif
++task_calls;}
#include "actual_hal.inc"
static void complete_disable(void){assert(task_calls);active=false;disabled_irq=true;}
int main(void){
 for(unsigned trx=0;trx<2;trx++)for(unsigned early=0;early<2;early++){
  enabled=BIT(0)|BIT(6)|BIT(7)|BIT(8)|BIT(22)|BIT(25)|BIT(26)|BIT(27);active=false;disabled_irq=false;rtc_event=true;reset_calls=task_calls=0;radio.SHORTS=UINT32_MAX;
  hal_radio_enable_on_tick_ppi_config_and_enable(trx);
  assert((enabled&(BIT(20)|BIT(21)))==(trx?BIT(20):BIT(21)));
  if(early)compare_event();radio_disable();assert(task_calls==1);
#ifdef FIXED
  assert(!(enabled&(BIT(20)|BIT(21)))&&!rtc_event&&reset_calls==1);
#else
  assert((enabled&(BIT(20)|BIT(21)))&&rtc_event&&!reset_calls);
#endif
  /* Hardware completes DISABLE later. A later compare event is a distinct
   * PPI task, regardless of a previously latched EVENTS register value. */
  complete_disable();compare_event();assert(disabled_irq);
#ifdef FIXED
  assert(!active);
#else
  assert(active); /* Original cleanup leaves the TXEN/RXEN route live. */
#endif
  assert(enabled&BIT(0)); /* No all-channel blanket reset. */
  radio_disable();complete_disable();assert(disabled_irq);
  hal_radio_enable_on_tick_ppi_config_and_enable(trx);compare_event();assert(active);
 }
 puts("PASS actual radio_disable/status_reset/PPI helpers/masks: TX+RX, earlier event, post-DISABLE late compare, repeated disable and rearm; explicit peripheral event model");
}

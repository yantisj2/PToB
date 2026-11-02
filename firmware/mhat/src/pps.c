/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * TIMER3 compares -> PPI -> GPIOTE SET/CLR, with hardware one-shot disable.
 * The immutable RC owner offer retains its original source/session deadlines.
 */
#include "capture.h"
#include "rc_unicast.h"
#include "usb_source.h"
#include "pps_plan.h"
#include "pps_offer.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <nrfx_gpiote.h>
#include <nrfx_ppi.h>
#include <hal/nrf_timer.h>
#include <hal/nrf_gpio.h>
static struct k_spinlock lock;
static struct r2_uc_clock offer;
static nrf_ppi_channel_t rise,fall;
static nrf_ppi_channel_group_t group;
static uint8_t channel;
static bool ready,armed;
static uint64_t scheduled_second,last_second;
static uint32_t emitted,rejected,revision;
static uint32_t last_lead_ns,last_map_age_us,armed_revision,deferred;
static const nrfx_gpiote_t gpiote=NRFX_GPIOTE_INSTANCE(0);
#define PPS_PIN 34u /* P1.02 D2/RTS -> M-HAT -> Pi GPIO16 */
static void disarm(void) {
    if(ready){nrfx_ppi_group_disable(group);nrf_gpiote_task_trigger(NRF_GPIOTE,(nrf_gpiote_task_t)(NRF_GPIOTE_TASK_CLR_0+4*channel));}
    armed=false;
}
int mhat_pps_init(void) {
    if(!nrfx_gpiote_init_check(&gpiote))return -ENODEV;
    if(nrfx_gpiote_channel_alloc(&gpiote,&channel)!=NRFX_SUCCESS||
       nrfx_ppi_channel_alloc(&rise)!=NRFX_SUCCESS||nrfx_ppi_channel_alloc(&fall)!=NRFX_SUCCESS||
       nrfx_ppi_group_alloc(&group)!=NRFX_SUCCESS)return -ENOMEM;
    nrf_gpio_cfg_output(PPS_PIN);nrf_gpio_pin_clear(PPS_PIN);
    nrf_gpiote_task_configure(NRF_GPIOTE,channel,PPS_PIN,NRF_GPIOTE_POLARITY_TOGGLE,NRF_GPIOTE_INITIAL_VALUE_LOW);
    nrf_gpiote_task_enable(NRF_GPIOTE,channel);
    uint32_t set=nrf_gpiote_task_address_get(NRF_GPIOTE,(nrf_gpiote_task_t)(NRF_GPIOTE_TASK_SET_0+4*channel));
    uint32_t clr=nrf_gpiote_task_address_get(NRF_GPIOTE,(nrf_gpiote_task_t)(NRF_GPIOTE_TASK_CLR_0+4*channel));
    if(nrfx_ppi_channel_assign(rise,nrf_timer_event_address_get(NRF_TIMER3,NRF_TIMER_EVENT_COMPARE4),set)!=NRFX_SUCCESS||
       nrfx_ppi_channel_assign(fall,nrf_timer_event_address_get(NRF_TIMER3,NRF_TIMER_EVENT_COMPARE5),clr)!=NRFX_SUCCESS||
       nrfx_ppi_channels_include_in_group((1u<<rise)|(1u<<fall),group)!=NRFX_SUCCESS||
       nrfx_ppi_channel_fork_assign(fall,nrf_ppi_task_address_get(NRF_PPI,nrf_ppi_group_disable_task_get(NRF_PPI,group)))!=NRFX_SUCCESS)return -EIO;
    ready=true;disarm();return 0;
}
void r2_usb_source_withdraw(void){k_spinlock_key_t k=k_spin_lock(&lock);if((offer.valid||armed)&&revision<UINT32_MAX)++revision;offer=(struct r2_uc_clock){0};disarm();k_spin_unlock(&lock,k);}
void r2_usb_source_begin(uint64_t activation,uint64_t epoch){(void)activation;(void)epoch;r2_usb_source_withdraw();}
void r2_usb_source_prepare(uint64_t activation,uint64_t epoch,const struct r2_uc_clock *old){
    if(old&&old->valid&&old->capture_epoch==epoch&&activation<old->until_tick)r2_usb_source_publish(old);
    else r2_usb_source_withdraw();
}
void r2_usb_source_publish(const struct r2_uc_clock *next){
    k_spinlock_key_t k=k_spin_lock(&lock);
    if(!next||!next->valid){
        if((offer.valid||armed)&&revision<UINT32_MAX)++revision;
        offer=(struct r2_uc_clock){0};disarm();
    }else if(!mhat_pps_offer_same(&offer,next)){
        /* Same owner map at routine polling cadence is not a mutation. Fence
         * real changes only, matching the T096 USB publisher's contract. */
        if(revision>=UINT32_MAX-1){revision=UINT32_MAX;offer=(struct r2_uc_clock){0};disarm();}
        else {
            ++revision;
            if(offer.capture_epoch!=next->capture_epoch||memcmp(offer.scope,next->scope,16)||
               offer.origin.root!=next->origin.root||offer.origin.boot!=next->origin.boot||
               offer.origin.epoch!=next->origin.epoch||offer.origin.utc_offset!=next->origin.utc_offset||
               next->until_tick<offer.until_tick||next->session_until<offer.session_until||
               next->origin.expires<offer.origin.expires)disarm();
            offer=*next;
        }
    }
    k_spin_unlock(&lock,k);
}
void mhat_pps_poll(void) {
    if(!ready)return;
    struct r2_capture now;if(!r2_capture_now(&now)){r2_usb_source_withdraw();return;}
    struct r2_uc_clock snapshot;uint32_t rev;uint64_t last;bool pending;
    k_spinlock_key_t k=k_spin_lock(&lock);
    if(armed&&nrf_timer_event_check(NRF_TIMER3,NRF_TIMER_EVENT_COMPARE4)) {
        ++emitted;last_second=scheduled_second;
        nrf_timer_event_clear(NRF_TIMER3,NRF_TIMER_EVENT_COMPARE4);
    }
    if(armed&&nrf_timer_event_check(NRF_TIMER3,NRF_TIMER_EVENT_COMPARE5)) {
        armed=false;nrf_timer_event_clear(NRF_TIMER3,NRF_TIMER_EVENT_COMPARE5);
    }
    snapshot=offer;rev=revision;last=last_second;pending=armed;
    k_spin_unlock(&lock,k);
    uint64_t utc,hi,second;
    if(snapshot.capture_epoch!=now.epoch||!mhat_utc_at(&snapshot,now.tick,&utc)){
        k=k_spin_lock(&lock);if(revision==rev)disarm();k_spin_unlock(&lock,k);return;
    }
    /* Checked projection and binary search run with interrupts enabled. */
    if(!pending&&UINT64_C(1000000000)-utc%UINT64_C(1000000000)>UINT64_C(21000000)){
        ++deferred;return;
    }
    if(pending||!mhat_pps_plan(&snapshot,now.tick,last,&hi,&second))return;
    if(!mhat_pps_commit_ready(now.tick,hi)){++deferred;return;}
    k=k_spin_lock(&lock);
    struct r2_capture after;
    if(!mhat_pps_revision_current(rev,revision)||armed||!r2_capture_now(&after)||after.epoch!=now.epoch||hi<=after.tick+16000){
        ++rejected;k_spin_unlock(&lock,k);return;
    }
    disarm();nrf_timer_event_clear(NRF_TIMER3,NRF_TIMER_EVENT_COMPARE4);nrf_timer_event_clear(NRF_TIMER3,NRF_TIMER_EVENT_COMPARE5);
    nrf_timer_cc_set(NRF_TIMER3,NRF_TIMER_CC_CHANNEL4,(uint32_t)hi);
    nrf_timer_cc_set(NRF_TIMER3,NRF_TIMER_CC_CHANNEL5,(uint32_t)(hi+160000));
    last_lead_ns=(uint32_t)((hi-after.tick)*125/2);
    last_map_age_us=(uint32_t)((after.tick-snapshot.map.effective)/16);
    armed_revision=rev;
    scheduled_second=second;armed=true;nrfx_ppi_group_enable(group);
    k_spin_unlock(&lock,k);
}
void mhat_pps_stats(uint32_t out[4]){k_spinlock_key_t k=k_spin_lock(&lock);out[0]=emitted;out[1]=rejected;out[2]=armed;out[3]=offer.valid;k_spin_unlock(&lock,k);}
void mhat_pps_debug(uint32_t out[5]){k_spinlock_key_t k=k_spin_lock(&lock);out[0]=last_lead_ns;out[1]=last_map_age_us;out[2]=revision;out[3]=armed_revision;out[4]=deferred;k_spin_unlock(&lock,k);}

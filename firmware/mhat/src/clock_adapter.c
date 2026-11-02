/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "capture.h"
#include "radio_capture.h"
#include <errno.h>
#include <nrfx_timer.h>
#include <nrfx_ppi.h>
#include <hal/nrf_radio.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/sys/onoff.h>
static const nrfx_timer_t timer = NRFX_TIMER_INSTANCE(3);
static struct onoff_client client;
static struct k_spinlock lock;
static nrf_ppi_channel_t route;
static uint64_t ticks, epoch;
static uint32_t raw;
static int64_t last_us;
static int fault;
static bool running;
int mhat_clock_start(uint64_t incarnation) {
    struct onoff_manager *manager=z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
    sys_notify_init_spinwait(&client.notify);
    if(onoff_request(manager,&client)<0)return -EIO;
    int result=-EAGAIN;
    for(unsigned i=0;i<25;i++) {
        if(!sys_notify_fetch_result(&client.notify,&result))break;
        k_sleep(K_MSEC(1));
    }
    if(result)return result;
    nrfx_timer_config_t config=NRFX_TIMER_DEFAULT_CONFIG(16000000);
    config.bit_width=NRF_TIMER_BIT_WIDTH_32;
    if(nrfx_timer_init(&timer,&config,NULL)!=NRFX_SUCCESS)return -EIO;
    if(nrfx_ppi_channel_alloc(&route)!=NRFX_SUCCESS)return -ENOMEM;
    if(nrfx_ppi_channel_assign(route,nrf_radio_event_address_get(NRF_RADIO,NRF_RADIO_EVENT_ADDRESS),
                              nrfx_timer_capture_task_address_get(&timer,1))!=NRFX_SUCCESS)return -EIO;
    nrfx_timer_enable(&timer);
    epoch=incarnation;raw=nrfx_timer_capture(&timer,2);ticks=raw;
    last_us=k_ticks_to_us_floor64(k_uptime_ticks());running=true;
    return r2_radio_capture_enable((uint32_t)epoch,route);
}
static bool current(struct r2_capture *out,uint64_t *mono) {
    k_spinlock_key_t key=k_spin_lock(&lock);
    int64_t at=k_ticks_to_us_floor64(k_uptime_ticks());
    uint32_t n=nrfx_timer_capture(&timer,2),delta=n-raw;
    if(!running||fault||at<last_us||at-last_us>1000000||delta>=0x80000000u)fault=-ESTALE;
    if(!fault){ticks+=delta;raw=n;last_us=at;*out=(struct r2_capture){ticks,epoch,R2_CAPTURE_NOW};if(mono)*mono=at;}
    k_spin_unlock(&lock,key);return !fault;
}
bool r2_capture_now(struct r2_capture *out){return current(out,NULL);}
bool r2_capture_now_at(struct r2_capture *out,uint64_t *at){return current(out,at);}
bool r2_capture_radio(uint32_t value,uint32_t e,struct r2_capture *out){
    struct r2_capture n;if(!current(&n,NULL)||e!=(uint32_t)n.epoch)return false;
    uint32_t age=(uint32_t)n.tick-value;
    if(age>16000000||n.tick<age)return false;
    *out=(struct r2_capture){n.tick-age,n.epoch,R2_CAPTURE_RADIO};return true;
}
void r2_capture_poll(int64_t now){struct r2_capture n;(void)now;(void)current(&n,NULL);}
int r2_capture_error(void){return fault;}
int r2_capture_stop(void){running=false;return r2_radio_capture_disable();}

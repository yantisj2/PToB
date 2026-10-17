/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include "app.h"
#include <errno.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/sys/onoff.h>
static struct r2_platform state={.clock_result=-EAGAIN,.bt_result=-ENOTSUP,
 .bt_submit=-ENOTSUP,.close_result=-EAGAIN,.bt_done=true};
static struct onoff_client client;
void r2_platform_start(void){
 struct onoff_manager *manager=z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
 state.clock_start=k_uptime_get();sys_notify_init_spinwait(&client.notify);
 state.clock_request=onoff_request(manager,&client);int result=-EAGAIN;
 if(state.clock_request>=0){
  while(k_uptime_get()-state.clock_start<R2_CLOCK_MS){
   if(!sys_notify_fetch_result(&client.notify,&result))break;
   k_sleep(K_MSEC(1));
  }
  state.clock_end=k_uptime_get();
  state.clock_result=state.clock_end-state.clock_start>=R2_CLOCK_MS?-ETIMEDOUT:result;
  state.clock_status=clock_control_get_status(DEVICE_DT_GET(DT_NODELABEL(clock)),CLOCK_CONTROL_NRF_SUBSYS_HF);
  state.clock_release=onoff_cancel_or_release(manager,&client);
 }else{state.clock_result=state.clock_request;state.clock_end=k_uptime_get();}
 state.clock_done=true;
}
int r2_platform_close(void){
 if(!state.clock_done||state.clock_result||state.clock_release<0||state.close_done)return -EACCES;
 state.close_done=true;state.close_result=0;state.close_end=k_uptime_get();return 0;
}
void r2_platform_snapshot(struct r2_platform *out){*out=state;}

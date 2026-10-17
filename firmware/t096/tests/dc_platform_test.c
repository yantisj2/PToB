/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include "app.h"
static struct {int unused;} manager;
static int64_t now;
static int mode,released;
#define CLOCK_CONTROL_NRF_SUBSYS_HF ((void*)1)
struct onoff_manager {int unused;};struct onoff_client {struct {int unused;} notify;};
#define k_sleep(ms) (now+=(ms))
int64_t k_uptime_get(void){return now;}
static void *z_nrf_clock_control_get_onoff(void *subsystem){return &manager;}
static void sys_notify_init_spinwait(void *p){}
static int onoff_request(struct onoff_manager *m,struct onoff_client *c){return mode==2?-EIO:0;}
static int sys_notify_fetch_result(void *p,int *result){if(mode==1||now<2)return -EAGAIN;*result=0;return 0;}
static int onoff_cancel_or_release(struct onoff_manager *m,struct onoff_client *c){++released;return mode==3?-EIO:0;}
static int clock_control_get_status(const struct device *d,void *subsystem){return 2;}
#include "../src/dc_platform.c"
int main(int argc,char **argv){
 assert(argc==2);mode=argv[1][0]-'0';
 assert(r2_platform_close()==-EACCES);r2_platform_start();struct r2_platform p;r2_platform_snapshot(&p);
 assert(p.clock_done&&now<=20&&p.bt_done&&p.bt_result==-ENOTSUP&&p.bt_submit==-ENOTSUP);
 if(!mode){assert(released==1&&!p.clock_result&&!p.clock_release);assert(!r2_platform_close());
  r2_platform_snapshot(&p);assert(p.close_done&&!p.close_result);assert(r2_platform_close()==-EACCES);}
 else{assert(r2_platform_close()==-EACCES);if(mode==1)assert(p.clock_result==-ETIMEDOUT&&now==20);
  if(mode==2)assert(!released&&p.clock_result==-EIO);if(mode==3)assert(released&&p.clock_release==-EIO);}
 puts("PASS DC platform: bounded HFXO probe/release, no BT API, failed-start closure refusal");
}

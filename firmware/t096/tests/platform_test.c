/* Actual platform owner, public clock/Bluetooth boundary fault injection. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
static struct { int unused; } manager;
static int64_t now;
static int mode,released,created,requested;
#define CLOCK_CONTROL_NRF_SUBSYS_HF ((void*)1)
struct onoff_manager {int unused;};struct onoff_client {struct {int unused;} notify;};
struct k_sem {int unused;};
#define K_SEM_DEFINE(name,start,max) struct k_sem name
#define K_FOREVER (-1)
#undef k_thread_create
#define k_thread_create(...) (++created)
#undef k_sleep
#define k_sleep(ms) (now+=(ms))
int64_t k_uptime_get(void){return now;}
static void *z_nrf_clock_control_get_onoff(void *subsystem){return &manager;}
static void sys_notify_init_spinwait(void *p){}
static int onoff_request(struct onoff_manager *m,struct onoff_client *c){++requested;return mode==2?-5:0;}
static int sys_notify_fetch_result(void *p,int *result){if(mode==1||now<2)return -EAGAIN;*result=0;return 0;}
static int onoff_cancel_or_release(struct onoff_manager *m,struct onoff_client *c){++released;return mode==3?-5:0;}
static int clock_control_get_status(const struct device *d,void *subsystem){return 2;}
static int bt_enable(void (*callback)(int)){return 0;}
static int bt_disable(void){return 0;}
static int k_sem_take(struct k_sem *s,int timeout){return 0;}
static void k_sem_give(struct k_sem *s){}
#include "../src/platform.c"
int main(int argc,char **argv)
{
 assert(argc==2);mode=argv[1][0]-'0';r2_platform_start();struct r2_platform p;r2_platform_snapshot(&p);
 assert(requested==1&&p.clock_done&&now<=20);
 if(mode==0)assert(created==1&&released==1&&!p.clock_result&&!p.clock_release);
 if(mode==1)assert(created==0&&released==1&&p.clock_result==-ETIMEDOUT&&now==20);
 if(mode==2)assert(created==0&&!released&&p.clock_result==-5);
 if(mode==3)assert(created==0&&released==1&&p.clock_release==-5);
 puts("R2 actual platform owner: bounded request/release and failure prevents BT startup PASS");
}

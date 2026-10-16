/* SPDX-License-Identifier: Apache-2.0 */
#include "app.h"
#include <errno.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/nrf_clock_control.h>
#include <zephyr/sys/onoff.h>
#define R2_LOCK_UNIT 7u
#include "health_lock.h"
static struct r2_platform state={.clock_result=-EAGAIN,.bt_result=-EAGAIN,.bt_submit=-EAGAIN,.close_result=-EAGAIN};
static struct k_spinlock lock;
static struct onoff_client clock_client;
K_SEM_DEFINE(close_requested,0,1);
K_THREAD_STACK_DEFINE(platform_stack,2048);
static struct k_thread platform_thread;
static void bluetooth_ready(int result)
{
  k_spinlock_key_t key=k_spin_lock(&lock);
  state.bt_result=result; state.bt_end=k_uptime_get(); state.bt_done=true;
  k_spin_unlock(&lock,key);
}
static void worker(void *a,void *b,void *c)
{
  k_spinlock_key_t key=k_spin_lock(&lock); state.bt_start=k_uptime_get(); k_spin_unlock(&lock,key);
  int rc=bt_enable(bluetooth_ready);
  key=k_spin_lock(&lock); state.bt_submit=rc; k_spin_unlock(&lock,key);
  if(rc) bluetooth_ready(rc);
  (void)k_sem_take(&close_requested,K_FOREVER);
  rc=bt_disable();
  key=k_spin_lock(&lock);
  state.close_result=rc; state.close_end=k_uptime_get(); state.close_done=true;
  k_spin_unlock(&lock,key);
}
void r2_platform_start(void)
{
  /* This request ends before USB or bt_enable. The 20 ms probe is a test,
   * not an adopted HFXO reference or a delay used to repair the driver. */
  struct onoff_manager *manager=z_nrf_clock_control_get_onoff(CLOCK_CONTROL_NRF_SUBSYS_HF);
  state.clock_start=k_uptime_get();
  sys_notify_init_spinwait(&clock_client.notify);
  state.clock_request=onoff_request(manager,&clock_client);
  int result=-EAGAIN;
  if(state.clock_request>=0) {
    while(k_uptime_get()-state.clock_start<R2_CLOCK_MS) {
      if(!sys_notify_fetch_result(&clock_client.notify,&result)) break;
      k_sleep(K_MSEC(1));
    }
    state.clock_end=k_uptime_get();
    state.clock_result=state.clock_end-state.clock_start>=R2_CLOCK_MS?-ETIMEDOUT:result;
    state.clock_status=clock_control_get_status(DEVICE_DT_GET(DT_NODELABEL(clock)),CLOCK_CONTROL_NRF_SUBSYS_HF);
    state.clock_release=onoff_cancel_or_release(manager,&clock_client);
  } else {
    state.clock_result=state.clock_request; state.clock_end=k_uptime_get(); state.clock_release=0;
  }
  state.clock_done=true;
  /* No attempt to initialize Bluetooth after uncertain clock ownership. */
  if(state.clock_result || state.clock_release<0) return;
  k_thread_create(&platform_thread,platform_stack,K_THREAD_STACK_SIZEOF(platform_stack),worker,
    NULL,NULL,NULL,8,0,K_NO_WAIT);
  k_thread_name_set(&platform_thread,"r2_platform");
}
int r2_platform_close(void)
{
  struct r2_platform s; r2_platform_snapshot(&s);
  if(!s.bt_done || s.bt_result || s.bt_submit || s.close_done) return -EACCES;
  k_sem_give(&close_requested); return 0;
}
void r2_platform_snapshot(struct r2_platform *out)
{
  k_spinlock_key_t key=k_spin_lock(&lock); *out=state; k_spin_unlock(&lock,key);
}

/* SPDX-License-Identifier: Apache-2.0 */
#include "app.h"
#include "ptob_device.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/hwinfo.h>
#include <zephyr/drivers/entropy.h>
#include <zephyr/drivers/retained_mem.h>
#include <zephyr/init.h>
#include <zephyr/sys/reboot.h>
static int safe_result=-EAGAIN;
static uint8_t serial[23]="TC-00-0000000000000000";
uint8_t *usb_update_sn_string_descriptor(void) { return serial; }
static int safe_pins(void)
{
  static const struct { struct gpio_dt_spec pin; gpio_flags_t flags; } pins[] = {
    {GPIO_DT_SPEC_GET(DT_NODELABEL(gnss_enable),gpios),GPIO_OUTPUT_INACTIVE},
    {GPIO_DT_SPEC_GET(DT_NODELABEL(vext_enable),gpios),GPIO_OUTPUT_INACTIVE},
    {GPIO_DT_SPEC_GET(DT_NODELABEL(lora_cs),gpios),GPIO_OUTPUT_INACTIVE},
    {GPIO_DT_SPEC_GET(DT_NODELABEL(lora_reset),gpios),GPIO_OUTPUT_ACTIVE},
    {GPIO_DT_SPEC_GET(DT_NODELABEL(lora_csd),gpios),GPIO_OUTPUT_INACTIVE},
    {GPIO_DT_SPEC_GET(DT_NODELABEL(lora_ctx),gpios),GPIO_OUTPUT_INACTIVE},
    {GPIO_DT_SPEC_GET(DT_NODELABEL(lora_fem_power),gpios),GPIO_OUTPUT_INACTIVE},
    {GPIO_DT_SPEC_GET(DT_NODELABEL(tft_backlight),gpios),GPIO_OUTPUT_INACTIVE},
  };
  for (size_t i=0;i<ARRAY_SIZE(pins);i++) {
    if (!gpio_is_ready_dt(&pins[i].pin)) return safe_result=-ENODEV;
    int rc=gpio_pin_configure_dt(&pins[i].pin,pins[i].flags);
    if (rc) return safe_result=rc;
  }
  return safe_result=0;
}
BUILD_ASSERT(CONFIG_GPIO_INIT_PRIORITY < 41);
SYS_INIT(safe_pins,PRE_KERNEL_1,41);
int r2_board_safe(void) { return safe_result; }
void r2_identify(struct r2_identity *id)
{
  memset(id,0,sizeof(*id)); strcpy(id->unit,"TC-00"); id->battery=-1;
  uint8_t silicon[8];
  if (hwinfo_get_device_id(silicon,sizeof(silicon)) != sizeof(silicon)) { id->result=-ENODEV; return; }
  for (unsigned i=0;i<8;i++) snprintf(id->silicon+2*i,3,"%02x",silicon[i]);
  if (strcmp(id->silicon,PTOB_DEVICE_ID)) { id->result=-EACCES; return; }
  strcpy(id->unit,PTOB_DEVICE_LABEL); id->battery=PTOB_DEVICE_BATTERY;
  memcpy(serial,id->unit,5);
  for (unsigned i=0;i<16;i++) serial[6+i]=id->silicon[i]>='a'?id->silicon[i]-'a'+'A':id->silicon[i];
  const struct device *entropy=DEVICE_DT_GET(DT_CHOSEN(zephyr_entropy));
  if (!device_is_ready(entropy)) { id->result=-ENODEV; return; }
  uint8_t *p=(uint8_t *)&id->boot; size_t n=0; int64_t end=k_uptime_get()+30;
  while (n<8 && k_uptime_get()<end) {
    int got=entropy_get_entropy_isr(entropy,p+n,8-n,0);
    if (got<0 || got>(int)(8-n)) { id->result=got<0?got:-EIO; return; }
    n+=(size_t)got; if(n<8) k_sleep(K_MSEC(1));
  }
  id->result=n==8 && id->boot ? hwinfo_get_reset_cause(&id->reset_cause) : -ETIMEDOUT;
}
int r2_bootloader_request(void)
{
  const struct device *retained=DEVICE_DT_GET(DT_NODELABEL(gpregret1));
  BUILD_ASSERT(DT_REG_ADDR(DT_NODELABEL(gpregret1))==0x4000051c);
  BUILD_ASSERT(DT_REG_SIZE(DT_NODELABEL(gpregret1))==1);
  BUILD_ASSERT(!IS_ENABLED(CONFIG_RETAINED_MEM_MUTEXES));
  if(!device_is_ready(retained) || retained_mem_size(retained)!=1 || safe_result) return -ENODEV;
  unsigned key=irq_lock(); uint8_t value=0, magic=0x57; int rc=retained_mem_read(retained,0,&value,1);
  if (rc || value) { irq_unlock(key); return rc?rc:-ESTALE; }
  rc=retained_mem_write(retained,0,&magic,1);
  if (!rc) rc=retained_mem_read(retained,0,&value,1);
  if (!rc && value==magic) sys_reboot(SYS_REBOOT_WARM);
  uint8_t zero=0; int clear=retained_mem_write(retained,0,&zero,1);
  if(!clear) clear=retained_mem_read(retained,0,&value,1);
  irq_unlock(key);
  return clear?clear:rc?rc:-EIO;
}

/* Supported Zephyr System OFF; a released button becomes the wake source.
 * No off-state entry while a pressed wake pin would immediately reboot. */
#include <zephyr/sys/poweroff.h>
#include <zephyr/usb/usb_device.h>
#define R2_LOCK_UNIT 3u
#include "health_lock.h"
int r2_board_poweroff(void)
{
 const struct gpio_dt_spec wake=GPIO_DT_SPEC_GET(DT_NODELABEL(tft_button),gpios);
 int pressed=gpio_pin_get_dt(&wake);if(pressed<0)return pressed;if(pressed)return -EAGAIN;
 const struct gpio_dt_spec vext=GPIO_DT_SPEC_GET(DT_NODELABEL(vext_enable),gpios);
 int rc=gpio_pin_set_dt(&vext,0);if(rc)return rc;
 rc=gpio_pin_interrupt_configure_dt(&wake,GPIO_INT_LEVEL_ACTIVE);if(rc)return rc;
 rc=usb_disable();if(rc)return rc;
 sys_poweroff();return -EIO;
}

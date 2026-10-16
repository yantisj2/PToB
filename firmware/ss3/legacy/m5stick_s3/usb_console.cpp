#include "usb_console.hpp"
#include "usb_recovery_policy.hpp"
#include "driver/usb_serial_jtag.h"
#include "hal/usb_serial_jtag_ll.h"
#include "esp_private/periph_ctrl.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
namespace {
sticks3::USBRecoveryPolicy policy;
uint32_t before_conf=0,before_mux=0,last_error=0,max_wall_us=0;
bool attached=false;
bool reattach() {
 constexpr uint8_t target[]{0x14,0xc1,0x9f,0xd5,0x60,0xdc};uint8_t mac[6]{};
 if(esp_read_mac(mac,ESP_MAC_EFUSE_FACTORY)!=ESP_OK || std::memcmp(mac,target,6))return false;
 const int64_t began=esp_timer_get_time();
 before_conf=USB_SERIAL_JTAG.conf0.val;before_mux=RTCCNTL.usb_conf.val;
 // Stop/free the owned ISR before resetting its registers or discarding queues.
 // All RX consumers are in the calling application task; VFS output remains
 // IDF's nonblocking FIFO path. Never reset while a scan procedure is active.
 if(usb_serial_jtag_is_driver_installed())usb_serial_jtag_driver_uninstall();
 attached=false;
 const usb_serial_jtag_pull_override_vals_t detached{};
 usb_serial_jtag_ll_phy_enable_pull_override(&detached);
 vTaskDelay(pdMS_TO_TICKS(500)); // Host must observe a real disconnect.
 PERIPH_RCC_ATOMIC(){usb_serial_jtag_ll_enable_bus_clock(true);usb_serial_jtag_ll_reset_register();}
 usb_serial_jtag_ll_phy_enable_external(false);
 usb_serial_jtag_ll_phy_disable_pull_override();
 usb_serial_jtag_ll_phy_enable_pin_exchg(false);
 usb_serial_jtag_ll_phy_disable_vref_override();
 usb_serial_jtag_ll_phy_enable_pad(true);
 usb_serial_jtag_driver_config_t config{};config.rx_buffer_size=256;config.tx_buffer_size=2048;
 const esp_err_t rc=usb_serial_jtag_driver_install(&config);last_error=uint32_t(rc);attached=rc==ESP_OK;
 policy.attached(uint64_t(esp_timer_get_time())/1000);
 max_wall_us=std::max(max_wall_us,uint32_t(esp_timer_get_time()-began));
 return attached;
}
}
bool sticks3::usb_console_start(){return reattach();}
bool sticks3::usb_console_poll(bool radio_idle) {
 if(policy.due(uint64_t(esp_timer_get_time())/1000,usb_serial_jtag_is_connected(),radio_idle)){
  (void)reattach();return true;
 }
 return false;
}
unsigned sticks3::usb_console_epoch(){return policy.attempts();}
void sticks3::usb_console_status(){
 std::printf("SS3_USB epoch=%u driver=%u connected=%u last_error=%lu before_conf=%08lx before_mux=%08lx conf=%08lx mux=%08lx intr_ena=%08lx repair_wall_max_us=%lu retries_max=3 retry_min_ms=10000 maintenance=SCAN_IDLE_ONLY transport=FIXED_FUNCTION_USJ serial=FACTORY_MAC TX=NONE precision=UNQUALIFIED\n",
 policy.attempts(),unsigned(attached),unsigned(usb_serial_jtag_is_connected()),(unsigned long)last_error,
 (unsigned long)before_conf,(unsigned long)before_mux,(unsigned long)USB_SERIAL_JTAG.conf0.val,
 (unsigned long)RTCCNTL.usb_conf.val,(unsigned long)usb_serial_jtag_ll_get_intr_ena_status(),(unsigned long)max_wall_us);
}

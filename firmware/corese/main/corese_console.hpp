// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include "driver/usb_serial_jtag.h"
namespace n30_console {
inline bool init(){usb_serial_jtag_driver_config_t c{};c.rx_buffer_size=512;c.tx_buffer_size=1024;return usb_serial_jtag_driver_install(&c)==ESP_OK;}
inline int read(void* data,size_t n){return usb_serial_jtag_read_bytes(data,n,0);}
inline int write(const char* data,size_t n){return usb_serial_jtag_write_bytes(data,n,0);}
inline esp_err_t drained(){return usb_serial_jtag_wait_tx_done(0);}
}

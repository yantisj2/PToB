#pragma once
#include <cstddef>
#define ESP_OK 0
int usb_serial_jtag_write_bytes(const void*,size_t,unsigned);
int usb_serial_jtag_wait_tx_done(unsigned);

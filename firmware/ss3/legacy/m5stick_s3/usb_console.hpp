#pragma once
#include <cstdint>
namespace sticks3 {
bool usb_console_start();
bool usb_console_poll(bool radio_idle);
void usb_console_status();
unsigned usb_console_epoch();
}

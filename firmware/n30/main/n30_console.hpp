// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis.
#pragma once
#include "driver/uart.h"
namespace n30_console {
// CP2102-facing UART0, no GNSS UART. The sole bounded log worker writes the
// FIFO without waiting; its existing frame/line deadlines and drain check apply.
inline bool init(){
 uart_config_t c{};c.baud_rate=115200;c.data_bits=UART_DATA_8_BITS;
 c.parity=UART_PARITY_DISABLE;c.stop_bits=UART_STOP_BITS_1;
 c.flow_ctrl=UART_HW_FLOWCTRL_DISABLE;c.source_clk=UART_SCLK_DEFAULT;
 return uart_param_config(UART_NUM_0,&c)==ESP_OK&&
   uart_set_pin(UART_NUM_0,43,44,UART_PIN_NO_CHANGE,UART_PIN_NO_CHANGE)==ESP_OK&&
   uart_driver_install(UART_NUM_0,512,0,0,nullptr,0)==ESP_OK;
}
inline int read(void* data,size_t n){return uart_read_bytes(UART_NUM_0,data,n,0);}
inline int write(const char* data,size_t n){return uart_tx_chars(UART_NUM_0,data,n);}
inline esp_err_t drained(){return uart_wait_tx_done(UART_NUM_0,0);}
}

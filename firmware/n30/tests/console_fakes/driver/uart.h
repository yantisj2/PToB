// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Jonathan Yantis. Host UART boundary only.
#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t=int;
constexpr int ESP_OK=0,UART_NUM_0=0,UART_DATA_8_BITS=8,UART_PARITY_DISABLE=0,
 UART_STOP_BITS_1=1,UART_HW_FLOWCTRL_DISABLE=0,UART_SCLK_DEFAULT=1,UART_PIN_NO_CHANGE=-1;
struct uart_config_t {int baud_rate,data_bits,parity,stop_bits,flow_ctrl,source_clk;};
inline int uart_param_config(int,const uart_config_t*){return ESP_OK;}
inline int uart_set_pin(int,int,int,int,int){return ESP_OK;}
inline int uart_driver_install(int,int,int,int,void*,int){return ESP_OK;}
inline int uart_read_bytes(int,void*,size_t,unsigned){return 0;}
int uart_tx_chars(int,const char*,uint32_t);
int uart_wait_tx_done(int,unsigned);

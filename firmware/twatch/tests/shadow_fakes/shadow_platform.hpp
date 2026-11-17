// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <string>
#include <deque>
#include <cassert>
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
constexpr int ESP_OK=0,UART_NUM_1=1,UART_PIN_NO_CHANGE=-1,UART_DATA_8_BITS=8,UART_PARITY_DISABLE=0,UART_STOP_BITS_1=1,UART_HW_FLOWCTRL_DISABLE=0,UART_SCLK_XTAL=1;
constexpr unsigned MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
namespace shadow_fake {
inline uint64_t now=1000000;inline bool power=false,uart=false,capture=false,healthy=true;
inline bool fail_power=false,fail_alloc=false,fail_uart_start=false,fail_uart_stop=false,fail_capture_start=false,fail_capture_stop=false,fail_uart_read=false;
inline unsigned power_calls=0,uart_starts=0,uart_stops=0,capture_starts=0,capture_stops=0;
inline void (*during_power)()=nullptr;inline std::string bytes;
struct Edge {uint64_t tick=0,at=0;uint32_t number=0,incarnation=1,enclosure=50;bool valid=true;};
inline std::deque<Edge> edges;
}
struct uart_config_t {int baud_rate=0,data_bits=0,parity=0,stop_bits=0,flow_ctrl=0,source_clk=0;};
inline bool uart_is_driver_installed(int p){assert(p==UART_NUM_1);return shadow_fake::uart;}
inline int uart_param_config(int p,const uart_config_t* c){assert(p==1&&c->baud_rate==38400&&c->source_clk==UART_SCLK_XTAL);return 0;}
inline int uart_set_pin(int p,int tx,int rx,int rts,int cts){assert(p==1&&tx==-1&&rx==44&&rts==-1&&cts==-1);return 0;}
inline int uart_driver_install(int p,int rx,int tx,int queue,void* q,int flags){assert(p==1&&rx==2048&&!tx&&!queue&&!q&&!flags);++shadow_fake::uart_starts;if(shadow_fake::fail_uart_start)return -1;shadow_fake::uart=true;return 0;}
inline int uart_driver_delete(int p){assert(p==1);++shadow_fake::uart_stops;if(shadow_fake::fail_uart_stop)return -1;shadow_fake::uart=false;return 0;}
inline int uart_get_buffered_data_len(int p,size_t* n){assert(p==1);*n=shadow_fake::bytes.size();return shadow_fake::fail_uart_read?-1:0;}
inline int uart_flush_input(int p){assert(p==1);shadow_fake::bytes.clear();return 0;}
inline int uart_read_bytes(int p,uint8_t* b,size_t capacity,unsigned ticks){assert(p==1&&!ticks);if(shadow_fake::fail_uart_read)return -1;const auto n=std::min(capacity,shadow_fake::bytes.size());std::memcpy(b,shadow_fake::bytes.data(),n);shadow_fake::bytes.erase(0,n);return int(n);}
inline void* heap_caps_malloc(size_t n,unsigned flags){assert(flags==(MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));return shadow_fake::fail_alloc?nullptr:std::malloc(n);}

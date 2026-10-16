/* SPDX-License-Identifier: Apache-2.0 -- public driver boundary test only */
#include <stdint.h>
#include "rc_shadow.h"
bool r2_shadow_enabled(void){return true;}
void r2_shadow_edge(struct r2_capture c,int64_t arrival){(void)c;(void)arrival;}
#include <stdbool.h>
#include <stddef.h>
#define BUILD_ASSERT(x) _Static_assert(x,#x)
#define DT_PROP(node,prop) 1
#define DT_NODELABEL_(name) NODE_##name
#define DT_NODELABEL(name) DT_NODELABEL_(name)
#define NODE_uart0 0
#define NODE_gnss_enable 1
#define NODE_vext_enable 2
#define NODE_pps 3
#define DEVICE_DT_GET(node) (&fake_uart)
#define GPIO_DT_SPEC_GET(node,prop) {.port=&fake_uart,.pin=node}
#define BIT(n) (1u<<(n))
#define MIN(a,b) ((a)<(b)?(a):(b))
#define GPIO_INPUT 1
#define GPIO_INT_DISABLE 0
#define GPIO_INT_EDGE_TO_ACTIVE 1
struct device { int unused; };static struct device fake_uart;
struct gpio_dt_spec {const struct device *port;unsigned pin;};
struct gpio_callback {void (*handler)(const struct device*,struct gpio_callback*,uint32_t);};
typedef int atomic_t;
static inline int atomic_get(atomic_t *p){return *p;}
static inline int atomic_set(atomic_t *p,int v){int old=*p;*p=v;return old;}
static inline void atomic_clear(atomic_t *p){*p=0;}
static inline bool atomic_cas(atomic_t *p,int old,int v){if(*p!=old)return false;*p=v;return true;}
enum {UART_RX_RDY,UART_RX_BUF_REQUEST,UART_RX_BUF_RELEASED,UART_RX_STOPPED,UART_RX_DISABLED,UART_TX_DONE,UART_TX_ABORTED};
struct uart_event {int type;union {struct {uint8_t *buf;size_t offset,len;} rx;struct {uint8_t *buf;} rx_buf;struct {const uint8_t *buf;size_t len;} tx;} data;};
static int64_t now;
static unsigned init_calls,enable_calls,disable_calls,received_bytes,faults,pps_calls;
static int levels[4],start_rc;
static bool ready;
static void (*rx_callback)(const struct device*,struct uart_event*,void*);
static inline bool gpio_is_ready_dt(const struct gpio_dt_spec *p){return true;}
static inline bool device_is_ready(const struct device *d){return ready;}
static inline int device_init(const struct device *d){++init_calls;ready=true;return 0;}
static inline int gpio_pin_set_dt(const struct gpio_dt_spec *p,int level){levels[p->pin]=level;return 0;}
static inline int gpio_pin_configure_dt(const struct gpio_dt_spec *p,int mode){return 0;}
static inline int gpio_pin_interrupt_configure_dt(const struct gpio_dt_spec *p,int mode){return 0;}
static inline void gpio_init_callback(struct gpio_callback *p,void (*cb)(const struct device*,struct gpio_callback*,uint32_t),uint32_t mask){p->handler=cb;}
static inline int gpio_add_callback(const struct device *d,struct gpio_callback *p){return 0;}
static inline int gpio_remove_callback(const struct device *d,struct gpio_callback *p){return 0;}
static inline int uart_callback_set(const struct device *d,void (*cb)(const struct device*,struct uart_event*,void*),void *arg){rx_callback=cb;return 0;}
static inline int uart_rx_enable(const struct device *d,uint8_t *buf,size_t n,int timeout){++enable_calls;return start_rc;}
static inline int uart_rx_buf_rsp(const struct device *d,uint8_t *buf,size_t n){return 0;}
static inline int uart_rx_disable(const struct device *d){++disable_calls;return 0;}

#define K_NO_WAIT 0
struct k_thread {int unused;};
#define K_THREAD_STACK_DEFINE(name,size) char name[size]
#define K_THREAD_STACK_SIZEOF(name) sizeof(name)
static void (*pending_worker)(void*,void*,void*);
static inline void k_thread_create(struct k_thread *t,char *stack,size_t size,void (*fn)(void*,void*,void*),void *a,void *b,void *c,int priority,int options,int delay){pending_worker=fn;}
static inline void k_thread_name_set(struct k_thread *t,const char *name){}

static unsigned tx_calls,abort_calls;static int tx_rc,abort_rc;static const uint8_t *tx_buf;static size_t tx_len;
static inline int uart_tx(const struct device *d,const uint8_t *b,size_t n,int32_t timeout){++tx_calls;tx_buf=b;tx_len=n;return tx_rc;}
static inline int uart_tx_abort(const struct device *d){++abort_calls;return abort_rc;}

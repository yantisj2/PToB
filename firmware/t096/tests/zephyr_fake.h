#ifndef R2_FAKE_H
#define R2_FAKE_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
struct k_mutex {pthread_mutex_t native;};
#define K_MUTEX_DEFINE(name) static struct k_mutex name={PTHREAD_MUTEX_INITIALIZER}
static inline int k_mutex_lock(struct k_mutex *m,int timeout){return pthread_mutex_lock(&m->native);}
static inline int k_mutex_unlock(struct k_mutex *m){return pthread_mutex_unlock(&m->native);}
static bool fake_isr;
static inline bool k_is_in_isr(void){return fake_isr;}
#define MIN(a,b) ((a)<(b)?(a):(b))
#define K_NO_WAIT 0
#define K_MSEC(n) (n)
#define K_THREAD_STACK_DEFINE(name,size) char name[size]
#define K_THREAD_STACK_SIZEOF(name) sizeof(name)
#define DEVICE_DT_GET(node) (&fake_device)
#define DT_NODELABEL(name) 0
struct device {int unused;}; static const struct device fake_device;
struct k_thread {int unused;};struct k_spinlock {int unused;};typedef int k_spinlock_key_t;
struct k_msgq {void *data;size_t size,capacity,head,count;};
#define K_MSGQ_DEFINE(name,size,cap,align) static char name##_data[size*cap];static struct k_msgq name={name##_data,size,cap,0,0}
static inline int k_msgq_put(struct k_msgq *q,const void *v,int timeout) {if(q->count==q->capacity)return -ENOBUFS;memcpy((char *)q->data+((q->head+q->count)%q->capacity)*q->size,v,q->size);q->count++;return 0;}
static inline int k_msgq_get(struct k_msgq *q,void *v,int timeout) {if(!q->count)return -EAGAIN;memcpy(v,(char *)q->data+q->head*q->size,q->size);q->head=(q->head+1)%q->capacity;q->count--;return 0;}
static inline void k_msgq_purge(struct k_msgq *q) {q->head=q->count=0;}
static inline k_spinlock_key_t k_spin_lock(struct k_spinlock *l) {return 0;}
static inline void k_spin_unlock(struct k_spinlock *l,k_spinlock_key_t k) {}
static inline bool device_is_ready(const struct device *d) {return true;}
static inline void k_thread_create(struct k_thread *t,char *stack,size_t size,void (*fn)(void *,void *,void *),void *a,void *b,void *c,int prio,int opts,int delay) {}
static inline void k_thread_name_set(struct k_thread *t,const char *name) {}
static inline void k_sleep(int n) {}
int64_t k_uptime_get(void);
int uart_irq_update(const struct device *);
int uart_irq_is_pending(const struct device *);
int uart_irq_rx_ready(const struct device *);
int uart_irq_tx_ready(const struct device *);
void uart_irq_tx_enable(const struct device *);
void uart_irq_tx_disable(const struct device *);
int uart_fifo_read(const struct device *,uint8_t *,size_t);
int uart_fifo_fill(const struct device *,const uint8_t *,size_t);
static inline void uart_irq_rx_enable(const struct device *d) {}
static inline int uart_irq_callback_user_data_set(const struct device *d,void (*cb)(const struct device *,void *),void *arg) {return 0;}
#define UART_LINE_CTRL_DTR 1
static inline int uart_line_ctrl_get(const struct device *d,int type,uint32_t *value) {*value=1;return 0;}
enum usb_dc_status_code {USB_DC_DISCONNECTED,USB_DC_RESET,USB_DC_CONFIGURED};
static inline int usb_enable(void (*cb)(enum usb_dc_status_code,const uint8_t *)) {return 0;}
#endif

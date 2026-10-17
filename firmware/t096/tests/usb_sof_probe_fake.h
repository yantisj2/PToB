/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#include "usb_sof_observer.h"
#define R2_USB_SOF_PROBE 1
#define R2_APP_H
struct r2_identity {char silicon[17],unit[6];uint64_t boot;int battery,result;uint32_t reset_cause;};
struct usb_cfg_data{int unused;};
struct usb_if_descriptor{uint8_t bInterfaceNumber;};
struct usb_setup_packet{uint8_t bmRequestType,bRequest;uint16_t wValue,wIndex,wLength;};
enum usb_dc_status_code{USB_DC_CONFIGURED,USB_DC_RESET,USB_DC_DISCONNECTED,USB_DC_SUSPEND,USB_DC_RESUME};
struct k_spinlock{int unused;};typedef int k_spinlock_key_t;
static unsigned test_critical_depth;
static inline int k_spin_lock(struct k_spinlock *p){(void)p;assert(!test_critical_depth);test_critical_depth=1;return 0;}
static inline void k_spin_unlock(struct k_spinlock *p,int key){(void)p;(void)key;assert(test_critical_depth==1);test_critical_depth=0;}
static inline void sys_put_le16(uint16_t v,uint8_t *p){for(unsigned i=0;i<2;i++)p[i]=(uint8_t)(v>>(8*i));}
static inline void sys_put_le32(uint32_t v,uint8_t *p){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static inline void sys_put_le64(uint64_t v,uint8_t *p){for(unsigned i=0;i<8;i++)p[i]=(uint8_t)(v>>(8*i));}
static struct r2_usb_sof_snapshot test_raw;
static bool test_available=true;
static inline bool r2_capture_usb_sample(struct r2_usb_sof_snapshot *out){*out=test_raw;return test_available;}

static inline uint16_t sys_get_le16(const uint8_t *p){return (uint16_t)p[0]|(uint16_t)p[1]<<8;}
static inline uint32_t sys_get_le32(const uint8_t *p){return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}

static inline uint64_t sys_get_le64(const uint8_t *p){uint64_t v=0;for(unsigned i=0;i<8;i++)v|=(uint64_t)p[i]<<(8*i);return v;}

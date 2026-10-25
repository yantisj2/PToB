/* SPDX-License-Identifier: Apache-2.0 */
#ifndef R2_HEALTH_H
#define R2_HEALTH_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
/* Native ARM observer only; host fixtures cannot establish ARM fault behavior. */
enum r2_health_lane { R2_HEALTH_FLASH, R2_HEALTH_PA, R2_HEALTH_LANES };
enum r2_health_op { R2_HEALTH_ERASE=1, R2_HEALTH_WRITE, R2_HEALTH_COMMIT, R2_HEALTH_STORAGE_CHECK,
 R2_HEALTH_ADV_CREATE=10, R2_HEALTH_ADV_PARAM, R2_HEALTH_ADV_DATA,
 R2_HEALTH_PA_START, R2_HEALTH_EXT_START };
#if defined(CONFIG_ARM)
void r2_health_lock_enter(unsigned site,unsigned saved_mask);
void r2_health_lock_leave(unsigned saved_mask);
void r2_health_init(uint64_t boot);
void r2_health_beat(void);
void r2_health_operation(unsigned lane,unsigned operation,int result);
void r2_health_console(void);
/* Single retained512B record; normal boot writer only. Never clear on export.
 * saved() marks this boot's pending handoff after actual flash verification. */
const volatile uint32_t *r2_health_crash_pending(void);
void r2_health_crash_saved(uint32_t retained_crc);
int r2_health_crash_read(unsigned chunk,uint8_t out[64]);
#else
static inline void r2_health_init(uint64_t boot){(void)boot;}
static inline void r2_health_beat(void){}
static inline void r2_health_operation(unsigned lane,unsigned operation,int result)
{(void)lane;(void)operation;(void)result;}
static inline void r2_health_console(void){}
static inline const volatile uint32_t *r2_health_crash_pending(void){return NULL;}
static inline void r2_health_crash_saved(uint32_t c){(void)c;}
static inline int r2_health_crash_read(unsigned c,uint8_t out[64]){(void)c;(void)out;return -2;}
#endif
#endif

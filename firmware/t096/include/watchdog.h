/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
struct r2_identity;
struct r2_service_status;
#if defined(R2_WATCHDOG)
int r2_watchdog_boot(const struct r2_identity *);
void r2_watchdog_enable(bool);
void r2_watchdog_observe(const struct r2_service_status *,bool allowed);
void r2_watchdog_main_beat(void);
unsigned r2_watchdog_take_restart(void);
void r2_watchdog_restart_result(int);
void r2_watchdog_fatal(unsigned);
void r2_watchdog_lora(int error,uint32_t stage,uint32_t generation,uint32_t closure,int journal_result);
void r2_watchdog_console(char *,size_t);
bool r2_watchdog_restore_rc(bool *);
int r2_watchdog_bootloader(uint64_t original_deadline_ms);
unsigned r2_watchdog_reset_kind(void);
#else
static inline int r2_watchdog_boot(const struct r2_identity *id){(void)id;return 0;}
static inline void r2_watchdog_enable(bool on){(void)on;}
static inline void r2_watchdog_observe(const struct r2_service_status *s,bool allowed){(void)s;(void)allowed;}
static inline void r2_watchdog_main_beat(void){}
static inline unsigned r2_watchdog_take_restart(void){return 0;}
static inline void r2_watchdog_restart_result(int result){(void)result;}
static inline void r2_watchdog_fatal(unsigned reason){(void)reason;}
static inline void r2_watchdog_lora(int e,uint32_t s,uint32_t g,uint32_t c,int j)
{(void)e;(void)s;(void)g;(void)c;(void)j;}
static inline void r2_watchdog_console(char *line,size_t size){(void)line;(void)size;}
static inline bool r2_watchdog_restore_rc(bool *rc){(void)rc;return false;}
static inline int r2_watchdog_bootloader(uint64_t deadline){(void)deadline;return 0;}
static inline unsigned r2_watchdog_reset_kind(void){return 0;}
#endif

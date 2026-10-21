/* MIT. Copyright (c) 2026 Jonathan Yantis. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#define R2_CONTROLLER_DIAG_WORDS 23u
#if defined(R2_CONTROLLER_DIAGNOSTICS)
void r2_controller_diag_init(void);
void r2_controller_diag_count(unsigned word);
bool r2_controller_diag_failure(uintptr_t callback,uintptr_t param);
void r2_controller_diag_entry(unsigned index,uintptr_t callback,uintptr_t param,unsigned flags);
void r2_controller_diag_snapshot(uint32_t out[R2_CONTROLLER_DIAG_WORDS]);
void r2_controller_diag_console(uint64_t boot,uint64_t now);
#endif

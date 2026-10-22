/* MIT. Copyright (c) 2026 Jonathan Yantis. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#define R2_CONTROLLER_DIAG_WORDS 23u
#define R2_CONTROLLER_DIAG_FAILURE_ONLY (UINT32_C(1)<<30)
#define R2_CONTROLLER_DIAG_UNKNOWN (UINT32_C(1)<<31)
#if defined(R2_CONTROLLER_FAULT_CAPTURE) || defined(R2_CONTROLLER_DIAGNOSTICS)
/* Pure existing diagnostic phase read; caller already holds IRQ mask. */
bool r2_controller_diag_ready_locked(void);
void r2_controller_diag_init(void);

bool r2_controller_diag_failure(uintptr_t callback,uintptr_t param);
void r2_controller_diag_entry(unsigned index,uintptr_t callback,uintptr_t param,unsigned flags);
void r2_controller_diag_complete(bool known);
void r2_controller_diag_snapshot(uint32_t out[R2_CONTROLLER_DIAG_WORDS]);
#if defined(R2_CONTROLLER_DIAGNOSTICS)
void r2_controller_diag_count(unsigned word);
void r2_controller_diag_console(uint64_t boot,uint64_t now);
#endif
#endif

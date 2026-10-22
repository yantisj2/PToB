/* MIT. Copyright (c) 2026 Jonathan Yantis. */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#define R2_CONTROLLER_DIAG_WORDS 23u
#define R2_CONTROLLER_DIAG_FAILURE_ONLY (UINT32_C(1)<<30)
#define R2_CONTROLLER_DIAG_UNKNOWN (UINT32_C(1)<<31)
#define R2_CONTROLLER_DIAG_CONTEXT_TAG UINT32_C(0xd0000000)
#if defined(R2_CONTROLLER_FAULT_CAPTURE) || defined(R2_CONTROLLER_DIAGNOSTICS)
/* Pure existing diagnostic phase read; caller already holds IRQ mask. */
bool r2_controller_diag_ready_locked(void);
void r2_controller_diag_init(void);

/* Failure writers share the caller's one continuous IRQ mask. No locks,
 * allocation, callbacks or controller mutation inside these accessors. */
bool r2_controller_diag_failure_locked(uintptr_t callback,uintptr_t param);
void r2_controller_diag_entry_locked(unsigned index,uintptr_t callback,uintptr_t param,unsigned flags);
void r2_controller_diag_complete_locked(bool known);
#if !defined(R2_CONTROLLER_DIAGNOSTICS)
void r2_controller_diag_context_locked(uint32_t owner,uint32_t abort_cb,
                                      uint32_t ticker,uint32_t flags);
/* Defined only by the ordinary native pinned LLL diagnostic translation unit. */
void r2_controller_diag_lll_context_locked(void);
#endif
void r2_controller_diag_snapshot(uint32_t out[R2_CONTROLLER_DIAG_WORDS]);
#if defined(R2_CONTROLLER_DIAGNOSTICS)
void r2_controller_diag_count(unsigned word);
void r2_controller_diag_console(uint64_t boot,uint64_t now);
#endif
#endif

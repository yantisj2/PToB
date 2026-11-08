// Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
#ifndef ESP32_REF_SHUTDOWN_H
#define ESP32_REF_SHUTDOWN_H
#include <stdbool.h>
#include <stdint.h>
typedef enum { REF_SHUTDOWN_RUN, REF_SHUTDOWN_DRAIN, REF_SHUTDOWN_READY,
               REF_SHUTDOWN_RELEASE, REF_SHUTDOWN_SLEEP, REF_SHUTDOWN_FAULT } RefShutdownStage;
typedef struct {
    uint64_t deadline_ms;
    RefShutdownStage stage;
    bool requested, power_off, fault, board_failure;
} RefShutdown;
// Repeated requests preserve the original two-second deadline. Failure is
// sticky until reset; a late SDK acknowledgment cannot turn failure into success.
void ref_shutdown_request(RefShutdown *shutdown, bool power_off, uint64_t now_ms);
RefShutdownStage ref_shutdown_poll(RefShutdown *shutdown, uint64_t now_ms,
                                    bool owners_done, uint64_t completed_ms,
                                    bool owner_failed, bool button_released);
void ref_shutdown_board_failed(RefShutdown *shutdown);
const char *ref_shutdown_stage_name(RefShutdownStage stage);
#endif

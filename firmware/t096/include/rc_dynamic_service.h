/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "service_backends.h"
int r2_dynamic_service_configure(const struct r2_identity *,
                                 const struct r2_enrollment *);
void r2_dynamic_service_clock(const struct r2_clock *);
int r2_dynamic_service_run(uint64_t);
int r2_dynamic_service_start(uint32_t, uint64_t);
int r2_dynamic_service_preflight(uint32_t, uint64_t);
void r2_dynamic_service_stop(void);
int r2_dynamic_service_close_result(void);
void r2_dynamic_service_snapshot(struct r2_service_status *);
void r2_dynamic_service_transport_snapshot(uint32_t out[13]);
void r2_dynamic_service_acquisition_snapshot(uint32_t out[20]);
void r2_dynamic_service_gm_admission_snapshot(uint32_t out[7]);
void r2_dynamic_service_connection_snapshot(uint32_t out[20]);
void r2_dynamic_service_ui(struct r2_ui_model *);
void r2_dynamic_service_supervise(void);
int r2_dynamic_service_name(const char *);
int r2_dynamic_service_parent_target(uint64_t);
uint32_t r2_dynamic_service_stack_unused(void);
int r2_service_restart(uint64_t arrival_ms);

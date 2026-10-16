/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include "ble_service.h"
int r2_normal_service_configure(const struct r2_identity *,const struct r2_enrollment *);
void r2_normal_service_clock(const struct r2_clock *);
int r2_normal_service_run(uint64_t);
int r2_normal_service_start(uint32_t,uint64_t);
int r2_normal_service_preflight(uint32_t,uint64_t);
void r2_normal_service_stop(void);
int r2_normal_service_close_result(void);
void r2_normal_service_snapshot(struct r2_service_status *);
void r2_normal_service_ui(struct r2_ui_model *);
void r2_normal_service_supervise(void);
int r2_normal_service_name(const char *);
uint32_t r2_normal_service_stack_unused(void);
int r2_field_service_configure(const struct r2_identity *,const struct r2_enrollment *);
void r2_field_service_clock(const struct r2_clock *);
int r2_field_service_run(uint64_t);
int r2_field_service_start(uint32_t,uint64_t);
int r2_field_service_preflight(uint32_t,uint64_t);
void r2_field_service_stop(void);
int r2_field_service_close_result(void);
void r2_field_service_snapshot(struct r2_service_status *);
void r2_field_service_ui(struct r2_ui_model *);
void r2_field_service_supervise(void);
int r2_field_service_name(const char *);
uint32_t r2_field_service_stack_unused(void);
int r2_normal_service_resume_gm(uint64_t,uint32_t);
int r2_normal_service_profile(unsigned,uint64_t);
void r2_normal_service_profile_status(uint32_t[5]);

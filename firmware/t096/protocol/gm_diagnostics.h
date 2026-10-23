/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "gm_standby.h"
struct r2_gm5_diagnostic {
 struct r2_gm5 state;
 uint64_t at,boot,source_until,root_health_at,output_not_before;
 uint32_t source_epoch,mesh_epoch,refusal,pair_generation;
 bool physical_eligible,local_hot,global_hot,global_hot_known,map_valid,locked,serving;
};
/* Bounded owner-published RAM snapshot. No query starts controller work. */
void r2_gm_diagnostic_snapshot(struct r2_gm5_diagnostic *);
int r2_gm_diagnostic_format(char *,size_t,const struct r2_gm5_diagnostic *,unsigned page,const char *source,uint64_t physical_boot);

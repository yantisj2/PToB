/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT */
#pragma once
#include "lora_passive_core.h"
/* Dedicated app-owned tail, never credential/diagnostic/recovery storage.
 * Initial provisioning is an explicit installer operation, not a boot repair. */
#define LP_STORE_START 0xd7000u
#define LP_STORE_BYTES 0x8000u
#define LP_STORE_RECORD_BYTES 576u
#define LP_STORE_RECORDS_PER_PAGE 7u
#define LP_STORE_MAX_COMMITS 50000u
#define LP_STORE_ACTIVE 1u
#define LP_STORE_INITIAL_QUIET 2u
struct lp_store {uint64_t factory;uint32_t sequence;uint8_t latest,next,prepared_page,ready;};
bool lp_store_load(struct lp_store *,uint64_t factory,struct lp_checkpoint *);
bool lp_store_save(void *,const struct lp_checkpoint *);
/* Erase the next retired page only during a quiescent interval. Missing spare
 * space refuses admission; a reservation never erases a page on its RF path. */
bool lp_store_prepare(struct lp_store *);

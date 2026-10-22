/* Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
 * Fake native LOW_LAT-off ABI/HAL; production bodies generated at test time. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define CONFIG_BT_TICKER_PRIORITY_SET 1
#define CONFIG_BT_TICKER_REMAINDER_SUPPORT 1
#define CONFIG_BT_TICKER_EXT 1
#define CONFIG_BT_TICKER_EXT_EXPIRE_INFO 1
#define IS_ENABLED(x) Z_ENABLED_##x
#define Z_ENABLED_CONFIG_BT_TICKER_LOW_LAT 0
#define Z_ENABLED_CONFIG_BT_TICKER_UPDATE 1
#define BIT(n) (1UL<<(n))
#define ARG_UNUSED(x) (void)(x)
#define LL_ASSERT assert
#define DEBUG_TICKER_TASK(x) ((void)0)
#define DOUBLE_BUFFER_SIZE 2
#define TICKER_NULL 255
#define TICKER_STATUS_SUCCESS 0
#define TICKER_STATUS_FAILURE 1
#define TICKER_PRIORITY_CRITICAL -128
#define TICKER_LAZY_MUST_EXPIRE 65535
#define TICKER_CALL_ID_WORKER 1
#define TICKER_CALL_ID_JOB 2
#define TICKER_RESCHEDULE_STATE_NONE 0
#define TICKER_RESCHEDULE_STATE_PENDING 1
#define HAL_TICKER_CNTR_MSBIT 23
#define HAL_TICKER_CNTR_MASK 0xffffff
#define HAL_TICKER_REMAINDER_RANGE 30517578u
#define HAL_TICKER_RESCHEDULE_MARGIN 5
#define TICKER_HAS_SLOT_WINDOW(t) ((t)->ext_data && (t)->ext_data->ticks_slot_window)
#define TICKER_RESCHEDULE_PENDING(t) ((t)->ext_data && (t)->ext_data->reschedule_state==1)
typedef void (*ticker_timeout_func)(uint32_t,uint32_t,uint32_t,uint16_t,uint8_t,void*);
struct ticker_expire_info_internal {uint8_t outdated,found;};
struct ticker_ext_context {void *context,*other_expire_info;};
struct ticker_ext {uint32_t ticks_slot_window,ticks_drift;uint8_t reschedule_state,is_drift_in_window,expire_info_id; ticker_timeout_func ext_timeout_func;struct ticker_expire_info_internal *other_expire_info;};
struct ticker_node {uint8_t next,req,ack,force,must_expire;int8_t priority;uint32_t ticks_periodic,ticks_to_expire,ticks_to_expire_minus,ticks_slot,remainder_current,remainder_periodic;uint16_t lazy_current,lazy_periodic;ticker_timeout_func timeout_func;void *context;void (*fp_op_func)(uint32_t,void*);void *op_context;struct ticker_ext *ext_data;};
struct ticker_instance {struct ticker_node *nodes; struct ticker_user *users; uint8_t count_user,count_node; bool expire_infos_outdated; uint8_t (*caller_id_get_cb)(uint8_t);uint32_t ticks_current,ticks_slot_previous,ticks_elapsed[2];uint8_t ticks_elapsed_first,ticks_elapsed_last,ticker_id_head,ticker_id_slot_previous,job_guard,worker_trigger;void (*sched_cb)(uint8_t,uint8_t,uint8_t,void*);};

typedef void (*ticker_op_func)(uint32_t,void*);
#define TICKER_USER_OP_TYPE_NONE 0
#define TICKER_USER_OP_TYPE_PRIORITY_SET 3
#define TICKER_USER_OP_TYPE_START 4
#define TICKER_USER_OP_TYPE_UPDATE 5
#define TICKER_USER_OP_TYPE_YIELD_ABS 6
#define TICKER_USER_OP_TYPE_STOP 7
#define TICKER_USER_OP_TYPE_STOP_ABS 8
#define TICKER_STATUS_BUSY 2
#define TICKER_LAZY_MUST_EXPIRE_KEEP 65534
#define DEBUG_TICKER_JOB(x) ((void)0)
#define cpu_dmb() ((void)0)
#define CONFIG_BT_PERIPHERAL 1
#define CONFIG_BT_CENTRAL 1
#define CONFIG_BT_CTLR_ULL_HIGH_PRIO 1
#define CONFIG_BT_CTLR_ULL_LOW_PRIO 1
#define TICKER_INSTANCE_ID_CTLR 0
#define TICKER_ID_CONN_BASE 0
#define TICKER_USER_ID_ULL_HIGH 1
#define TICKER_USER_ID_ULL_LOW 2
#define BT_HCI_ROLE_PERIPHERAL 1
#define HAL_TICKER_US_TO_TICKS(x) ticks(x)
#define HAL_TICKER_REMAINDER(x) remainder(x)
#include "ticker_fairness_types.inc"

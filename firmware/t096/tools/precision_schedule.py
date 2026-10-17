#!/usr/bin/env python3
"""Materialize reviewed timing-first scheduling against pinned Zephyr sources.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
from pathlib import Path
import hashlib,json,sys
PIN={'ll_sw/ull_adv_sync.c':'7fa54342d280c418ee85d7eddca75d678eeb3b539e99ef629c52919b3f8dee45',
     'll_sw/ull_sync.c':'65665ad8f84058300a8887d4fdb30659452a87c743168343f4f9ef8f1f8294c1'}
def replace(s,a,b,n=1):
    assert s.count(a)==n,(a,s.count(a),n)
    return s.replace(a,b)
def generate(zephyr,out):
    root=zephyr/'subsys/bluetooth/controller';out.mkdir(parents=True,exist_ok=True)
    manifest={}
    for name,digest in PIN.items():
        s=(root/name).read_text();assert hashlib.sha256(s.encode()).hexdigest()==digest,name
        s=replace(s,'#include <zephyr/kernel.h>','#include <zephyr/kernel.h>\n#include <errno.h>\n#include "precision_schedule.h"')
        if name.endswith('ull_adv_sync.c'):
            # Thread caller can wait for the stock ticker operation completion.
            s=replace(s,'\treturn ret;\n}\n\nuint8_t ull_adv_sync_time_update',
                '\tif (ret == TICKER_STATUS_SUCCESS) {\n'
                '\t\tret_cb = TICKER_STATUS_BUSY;\n'
                '\t\tret = ticker_priority_set(TICKER_INSTANCE_ID_CTLR, TICKER_USER_ID_THREAD,\n'
                '\t\t\tTICKER_ID_ADV_SYNC_BASE + sync_handle, -1,\n'
                '\t\t\tull_ticker_status_give, (void *)&ret_cb);\n'
                '\t\tret = ull_ticker_status_take(ret, &ret_cb);\n\t}\n'
                '\treturn ret;\n}\n\nuint8_t ull_adv_sync_time_update')
            s+='''
/* PToB: pool membership is bounded, no callback-owned pointer is retained. */
extern struct lll_sync *ull_sync_lll_is_valid_get(struct lll_sync *lll);
bool r2_precision_radio(void *owner)
{
    if (!owner) return false;
    if (ull_sync_lll_is_valid_get(owner)) return true;
    for (unsigned i = 0; i < CONFIG_BT_CTLR_ADV_SYNC_SET; ++i)
        if (owner == &ll_adv_sync_pool[i].lll && ll_adv_sync_pool[i].is_started)
            return true;
    return false;
}
extern uint8_t r2_ticker_phase_step(uint8_t instance, uint8_t user,
                                  uint8_t ticker_id, int8_t us,
                                  ticker_op_func cb, void *context);
int r2_precision_phase_step(uint8_t handle, int step)
{
    struct ll_adv_set *adv = ull_adv_is_created_get(handle);
    if (!adv || !adv->lll.sync || (step != 1 && step != -1)) return -EINVAL;
    struct ll_adv_sync_set *sync = adv->lll.sync->hdr.parent;
    if (!sync->is_enabled || !sync->is_started || sync->interval != 50) return -EAGAIN;
    uint32_t volatile done = TICKER_STATUS_BUSY;
    uint32_t ret = r2_ticker_phase_step(
        TICKER_INSTANCE_ID_CTLR, TICKER_USER_ID_THREAD,
        TICKER_ID_ADV_SYNC_BASE + sync_handle_get(sync), step * 8,
        ull_ticker_status_give, (void *)&done);
    ret = ull_ticker_status_take(ret, &done);
    return ret == TICKER_STATUS_SUCCESS ? 0 : -EAGAIN;
}
'''
        else:
            needle='\tLL_ASSERT((ret == TICKER_STATUS_SUCCESS) ||\n\t\t  (ret == TICKER_STATUS_BUSY));\n}'
            # Only the two PA sync ticker-start sites, not unrelated updates.
            for anchor in ['ticks_anchor,\n\t\t\t   HAL_TICKER_US_TO_TICKS(sync_offset_us)',
                           'ftr->ticks_anchor - ticks_slot_offset,']:
                begin=s.index(anchor);end=s.index(needle,begin)
                insert='''\tret = ticker_priority_set(TICKER_INSTANCE_ID_CTLR, TICKER_USER_ID_ULL_HIGH,
            TICKER_ID_SCAN_SYNC_BASE + sync_handle, -1,
            ticker_start_op_cb, (void *)__LINE__);
'''
                end+=len(needle)-1
                s=s[:end]+insert+'\tLL_ASSERT((ret == TICKER_STATUS_SUCCESS) ||\n\t\t  (ret == TICKER_STATUS_BUSY));\n'+s[end:]
        dest=out/Path(name).name;dest.write_text(s)
        manifest[name]={'original':digest,'generated':hashlib.sha256(s.encode()).hexdigest()}
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
if __name__=='__main__':generate(Path(sys.argv[1]),Path(sys.argv[2]))

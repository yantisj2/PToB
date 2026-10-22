#!/usr/bin/env python3
"""Pinned failure-only LLL cut; no healthy hook or new controller state.
Copyright (c) 2026 Jonathan Yantis. MIT additions; Zephyr retains Apache-2.0.
"""
import hashlib,json,sys
from pathlib import Path
APP=Path(__file__).resolve().parents[1]
NAME='subsys/bluetooth/controller/ll_sw/nordic/lll/lll.c'

def transform(old):
    s=old
    def change(a,b):
        nonlocal s
        assert s.count(a)==1,repr(a)
        s=s.replace(a,b,1)
    change('#include "hal/debug.h"', '''#include "hal/debug.h"
/* Original PToB diagnostics: Copyright (c)2026 Jonathan Yantis. MIT.
 * Surrounding Zephyr source retains Apache-2.0. Failure path only. */
#include "controller_diag.h"
#include "controller_snapshot.h"
#if !defined(CONFIG_ARM) || defined(CONFIG_SMP) || defined(CONFIG_ZERO_LATENCY_IRQS) || \\
    defined(CONFIG_BT_CTLR_ZLI) || defined(CONFIG_BT_CTLR_LOW_LAT) || \\
    defined(CONFIG_BT_CTLR_LOW_LAT_ULL_DONE) || \\
    CONFIG_BT_CTLR_LLL_PRIO != 0 || CONFIG_BT_CTLR_ULL_HIGH_PRIO != 1 || \\
    CONFIG_BT_CTLR_ULL_LOW_PRIO != 1 || defined(R2_CONTROLLER_DIAGNOSTICS)
#error "Prepare fault context requires the pinned ordinary UP/IRQ controller"
#endif
BUILD_ASSERT(sizeof(uintptr_t) == 4, "Prepare context needs 32-bit addresses");''')
    change('static void preempt(void *param);', '''static void preempt(void *param);
/* The same two pre-existing callback-local objects, moved without duplication.
 * Only a failed enqueue reads their state under the caller's IRQ mask. */
static memq_link_t r2_preempt_link;
static struct mayfly r2_preempt_mfy = {0, 0, &r2_preempt_link, NULL, preempt};''')
    local='''	static memq_link_t link;
	static struct mayfly mfy = {0, 0, &link, NULL, preempt};
	uint32_t ret;'''
    change(local,'\tuint32_t ret;')
    start=s.index('static void preempt_ticker_cb(',s.index('static uint32_t preempt_ticker_stop(void)\n{'))
    end=s.index('\nstatic void preempt(void *param)\n{',start)
    part=s[start:end]
    assert part.count('mfy.param = param;')==1 and part.count('&mfy);')==1
    s=s[:start]+part.replace('mfy.param = param;','r2_preempt_mfy.param = param;').replace('&mfy);','&r2_preempt_mfy);')+s[end:]
    s+='''
/* Caller holds the SAME outer mask as the seven-entry ULL failure cut.
 * No arbitrary owner dereference, wait, clock sample or controller mutation. */
void r2_controller_diag_lll_context_locked(void)
{
    struct ptob_ticker_snapshot t;
    ptob_ticker_snapshot_locked(TICKER_INSTANCE_ID_CTLR, TICKER_ID_LLL_PREEMPT,
                                r2_preempt_mfy.param, &t);
    uint32_t flags = (uint32_t)t.job_guard | ((uint32_t)t.worker_trigger << 8) |
        ((uint32_t)t.valid << 16) |
        ((uint32_t)(preempt_start_req != preempt_start_ack) << 17) |
        ((uint32_t)(preempt_stop_req != preempt_stop_ack) << 18) |
        ((uint32_t)(preempt_req != preempt_ack) << 19) |
        ((uint32_t)((r2_preempt_mfy._req - r2_preempt_mfy._ack) & 3u) << 20) |
        ((uint32_t)(event.curr.abort_cb != NULL) << 22) |
        ((uint32_t)(event.curr.param != NULL) << 23) |
        ((uint32_t)(r2_preempt_mfy.param == event.curr.param) << 24) |
        ((uint32_t)t.context_matches << 25) |
        ((uint32_t)is_done_sync() << 26) | R2_CONTROLLER_DIAG_CONTEXT_TAG;
    r2_controller_diag_context_locked((uint32_t)(uintptr_t)event.curr.param,
        (uint32_t)(uintptr_t)event.curr.abort_cb,
        (uint32_t)t.lazy_current | ((uint32_t)t.req << 16) | ((uint32_t)t.ack << 24),
        flags);
}
'''
    return s

def generate(zephyr,out):
    manifest=json.loads((APP/'cmake/controller-prepare-context.json').read_text())
    assert manifest['schema']==1 and manifest['format_tag']==13
    assert hashlib.sha256((APP/'cmake/controller-prepare-context.patch').read_bytes()).hexdigest()==manifest['patch_sha256']
    old=(zephyr/NAME).read_text()
    assert hashlib.sha256(old.encode()).hexdigest()==manifest['input_sha256'],'Unexpected LLL input'
    body=transform(old)
    assert hashlib.sha256(body.encode()).hexdigest()==manifest['output_sha256'],'Unexpected LLL context output'
    out.mkdir(parents=True,exist_ok=True)
    dest=out/'lll.c'
    if not dest.exists() or dest.read_text()!=body:dest.write_text(body)
    (out/'lll-context-manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')

if __name__=='__main__':
    assert len(sys.argv)==3
    generate(Path(sys.argv[1]),Path(sys.argv[2]))

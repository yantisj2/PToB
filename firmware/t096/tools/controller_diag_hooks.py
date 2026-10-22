#!/usr/bin/env python3
"""Pinned failure-only prepare capture, with explicit optional verbose hooks.
MIT. Copyright (c) 2026 Jonathan Yantis.
"""
from pathlib import Path
import hashlib,json,sys
APP=Path(__file__).resolve().parents[1]
def transform(old,mode):
    assert mode in ('failure-only','verbose')
    s='#include "controller_diag.h"\n'+old
    def change(a,b):
        nonlocal s
        assert s.count(a)==1,repr(a)
        s=s.replace(a,b,1)
    helper='''
/* Original PToB diagnostic addition: Copyright (c) 2026 Jonathan Yantis. MIT.
 * Surrounding Zephyr source retains Apache-2.0. Failure path only; no mutation
 * of the prepare FIFO. Caller failure/strict assertion remain unchanged. */
static __noinline struct lll_event *r2_prepare_failure(lll_prepare_cb_t prepare_cb,
                                                     struct lll_prepare_param *prepare_param)
{
    unsigned key = irq_lock();
    if (!r2_controller_diag_failure((uintptr_t)prepare_cb,
                                    (uintptr_t)prepare_param->param)) {
        irq_unlock(key);
        return NULL;
    }
    unsigned copied = 0;
    uint8_t cursor = UINT8_MAX;
    /* Validate every index source before the iterator can form a pointer.
     * IRQs stay masked across this complete bounded cut. A partial/corrupt
     * cut remains UNKNOWN; it never justifies an unsafe extra read. */
    if (mfifo_prep.n == 8 && mfifo_prep.s == MROUND(sizeof(struct lll_event)) &&
        mfifo_fifo_prep.f < mfifo_prep.n && mfifo_fifo_prep.l < mfifo_prep.n) {
        for (unsigned i = 0; i < 7; ++i) {
            struct lll_event *queued = MFIFO_DEQUEUE_ITER_GET(prep, &cursor);
            if (!queued) {
                break;
            }
            r2_controller_diag_entry(i, (uintptr_t)queued->prepare_cb,
                (uintptr_t)queued->prepare_param.param,
                queued->is_resume | (queued->is_aborted << 1));
            ++copied;
        }
    }
    r2_controller_diag_complete(copied == 7);
    irq_unlock(key);
    return NULL;
}

'''
    change('struct lll_event *ull_prepare_enqueue(',helper+'struct lll_event *ull_prepare_enqueue(')
    change('\tif (!e) {\n\t\treturn NULL;\n\t}\n\n\tmemcpy(&e->prepare_param',
           '\tif (!e) {\n\t\treturn r2_prepare_failure(prepare_cb, prepare_param);\n\t}\n\n\tmemcpy(&e->prepare_param')
    if mode=='verbose':
        change('\tMFIFO_ENQUEUE(prep, idx);','\tMFIFO_ENQUEUE(prep, idx);\n\tr2_controller_diag_count(0);')
        change('\t\tMFIFO_DEQUEUE(prep);','\t\tMFIFO_DEQUEUE(prep);\n\t\tr2_controller_diag_count(1);\n\t\tif (is_aborted) { r2_controller_diag_count(2); }')
        change('\t/* Process role dependent event done */','\tr2_controller_diag_count(3);\n\t/* Process role dependent event done */')
    return s

def generate(source,out,mode='failure-only'):
    old=source.read_text()
    manifest=json.loads((APP/'cmake/controller-prepare-diag.json').read_text())
    assert manifest['schema']==1 and manifest['capacity']==7
    assert set(manifest['output_sha256'])=={'failure-only','verbose'}
    assert hashlib.sha256((APP/'cmake/controller-prepare-fault.patch').read_bytes()).hexdigest()==manifest['failure_patch_sha256']
    assert hashlib.sha256(old.encode()).hexdigest()==manifest['input_sha256'],'Unexpected patched ULL input'
    s=transform(old,mode)
    assert hashlib.sha256(s.encode()).hexdigest()==manifest['output_sha256'][mode],'Unexpected diagnostic ULL output'
    out.mkdir(parents=True,exist_ok=True)
    if not (out/'ull.c').is_file() or (out/'ull.c').read_text()!=s:(out/'ull.c').write_text(s)
    (out/'manifest.json').write_text(json.dumps({'input_sha256':manifest['input_sha256'],
        'output_sha256':manifest['output_sha256'][mode],'class':'PREPARE_FIFO_OBSERVATION_ONLY',
        'mode':mode,'capacity':7,'assertion_unchanged':True,
        'manifest_sha256':hashlib.sha256((APP/'cmake/controller-prepare-diag.json').read_bytes()).hexdigest()},indent=2)+'\n')
if __name__=='__main__':
    assert len(sys.argv)==4
    generate(Path(sys.argv[1]),Path(sys.argv[2]),sys.argv[3])

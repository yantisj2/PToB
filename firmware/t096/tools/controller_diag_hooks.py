#!/usr/bin/env python3
"""Observation-only hooks after the reviewed pinned ULL teardown backport.
MIT. Copyright (c) 2026 Jonathan Yantis.
"""
from pathlib import Path
import hashlib,json,sys
source,out=map(Path,sys.argv[1:]);old=source.read_text();s=old
manifest=json.loads((Path(__file__).resolve().parents[1]/'cmake/ticker-backport.json').read_text())
assert hashlib.sha256(old.encode()).hexdigest()==manifest['files']['subsys/bluetooth/controller/ll_sw/ull.c']['fixed'],'Unexpected patched ULL input' 
def change(a,b):
 global s
 assert s.count(a)==1,repr(a)
 s=s.replace(a,b)
s='#include "controller_diag.h"\n'+s
change('\tif (!e) {\n\t\treturn NULL;\n\t}\n\n\tmemcpy(&e->prepare_param', '''\tif (!e) {
        /* Freeze actual FIFO ownership before the unchanged caller assertion.
         * Seven bounded entries, no formatting, waits or scheduling changes. */
        uint8_t cursor = UINT8_MAX;
        struct lll_event *queued;
        if (!r2_controller_diag_failure((uintptr_t)prepare_cb, (uintptr_t)prepare_param->param)) { return NULL; }
        for (unsigned i = 0; i < 7; ++i) {
            queued = MFIFO_DEQUEUE_ITER_GET(prep, &cursor);
            if (!queued) { break; }
            r2_controller_diag_entry(i, (uintptr_t)queued->prepare_cb,
                (uintptr_t)queued->prepare_param.param,
                queued->is_resume | (queued->is_aborted << 1));
        }
\t\treturn NULL;
\t}

\tmemcpy(&e->prepare_param''')
change('\tMFIFO_ENQUEUE(prep, idx);','\tMFIFO_ENQUEUE(prep, idx);\n\tr2_controller_diag_count(0);')
change('\t\tMFIFO_DEQUEUE(prep);','\t\tMFIFO_DEQUEUE(prep);\n\t\tr2_controller_diag_count(1);\n\t\tif (is_aborted) { r2_controller_diag_count(2); }')
change('\t/* Process role dependent event done */','\tr2_controller_diag_count(3);\n\t/* Process role dependent event done */')
out.mkdir(parents=True,exist_ok=True);(out/'ull.c').write_text(s)
(out/'manifest.json').write_text(json.dumps({'input_sha256':hashlib.sha256(old.encode()).hexdigest(),'output_sha256':hashlib.sha256(s.encode()).hexdigest(),'class':'PREPARE_FIFO_OBSERVATION_ONLY','capacity':7,'assertion_unchanged':True},indent=2)+'\n')

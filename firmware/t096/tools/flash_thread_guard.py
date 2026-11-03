#!/usr/bin/env python3
"""Pin flash THREAD ticker enqueues to the existing native GM/HCI producer lock.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
Generated Zephyr code retains its original Apache-2.0 terms and authorship.
"""
import hashlib,json,sys
from pathlib import Path
PIN='a0e08b87349ef1922f716319c9f6dd604a14c87cb91d48743bc40d12bb40acaa'
def generate(source,out):
 source=Path(source);out=Path(out);s=source.read_text();before=hashlib.sha256(s.encode()).hexdigest();assert before==PIN,('Unpinned flash ticker backend',before)
 assert s.count('#include "soc_flash_nrf.h"')==1
 s=s.replace('#include "soc_flash_nrf.h"','#include "soc_flash_nrf.h"\n#include "gm7_controller.h"')
 begin=s.index('int nrf_flash_sync_exe(struct flash_op_desc *op_desc)');end=s.index('\nbool nrf_flash_sync_is_required',begin);body=s[begin:end]
 needle='\tret = ticker_start(instance_index,';assert body.count(needle)==1
 body=body.replace(needle,'\t/* Share the single-producer THREAD queue with native GM and HCI.\n\t * Hold only through enqueue; ULL callbacks and sem_sync remain independent. */\n\tr2_gm7_thread_lock();\n'+needle,1)
 needle='\t\t\t   NULL);';assert body.count(needle)==1;body=body.replace(needle,needle+'\n\tr2_gm7_thread_unlock();',1)
 needle='\t\tret = ticker_stop(instance_index, 3U, ticker_id, NULL, NULL);';assert body.count(needle)==1
 body=body.replace(needle,'\t\tr2_gm7_thread_lock();\n'+needle+'\n\t\tr2_gm7_thread_unlock();')
 s=s[:begin]+body+s[end:];out.mkdir(parents=True,exist_ok=True);(out/'soc_flash_nrf_ticker.input').write_bytes(source.read_bytes());dest=out/source.name;dest.write_text(s)
 manifest=dict(input_sha256=before,output_sha256=hashlib.sha256(s.encode()).hexdigest(),scope='Native GM7 only; existing mutex, THREAD enqueue only; no timeout, callback/context lifetime, ULL or flash operation change')
 (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');return manifest
if __name__=='__main__':generate(*map(Path,sys.argv[1:]))

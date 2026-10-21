#!/usr/bin/env python3
"""Pinned actual enqueue/MFIFO and funded diagnostic writer, no hardware.
MIT. Copyright (c) 2026 Jonathan Yantis.
"""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
from ticker_backport import generate
p=argparse.ArgumentParser();p.add_argument('--deps',required=True,type=Path);p.add_argument('--out',required=True,type=Path);a=p.parse_args()
app=Path(__file__).resolve().parents[1];out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
generate(a.deps.resolve()/'zephyr',out/'backport')
subprocess.run([__import__('sys').executable,str(app/'tools/controller_diag_hooks.py'),str(out/'backport/ll_sw/ull.c'),str(out/'hook')],check=True)
s=(out/'hook/ull.c').read_text();start=s.index('struct lll_event *ull_prepare_enqueue(');end=s.index('\nvoid *ull_prepare_dequeue_get',start);enqueue=s[start:end]
for name,body in {'zephyr/irq.h':'unsigned irq_lock(void);void irq_unlock(unsigned);','zephyr/kernel.h':'#include <stdint.h>\nuint32_t k_uptime_get_32(void);','ble_service.h':'#include <stddef.h>\n#include <stdint.h>\nint r2_console_note(const char *,size_t,uint64_t);','build_identity.h':'#define R2_SOURCE_SHA "synthetic-source"'}.items():
 f=out/name;f.parent.mkdir(parents=True,exist_ok=True);f.write_text(body+'\n')
test=r'''
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "controller_diag.h"
#include "lora_watchdog.h"
union r2_diag_scratch r2_diag_scratch;
static unsigned depth,notes;static uint32_t ms=1234;
unsigned irq_lock(void){unsigned old=depth;depth++;return old;}
void irq_unlock(unsigned old){assert(depth==old+1);depth=old;}
uint32_t k_uptime_get_32(void){return ms;}
int r2_console_note(const char *line,size_t n,uint64_t until){assert(!depth&&n<320&&until==ms+150);assert(strstr(line,"synthetic-source")&&strstr(line,"0000000000000009"));notes++;return -ENOBUFS;}
typedef void (*lll_prepare_cb_t)(void);typedef void (*lll_abort_cb_t)(void);typedef void (*lll_is_abort_cb_t)(void);
struct lll_prepare_param {void *param;};
struct lll_event {struct lll_prepare_param prepare_param;lll_prepare_cb_t prepare_cb;lll_abort_cb_t abort_cb;lll_is_abort_cb_t is_abort_cb;unsigned is_resume,is_aborted;};
#define MROUND(x) (((x)+7)&~7)
#define MALIGN(x) __attribute__((aligned(8)))
static void cpu_dmb(void){}
#include "mfifo.h"
MFIFO_DEFINE(prep,sizeof(struct lll_event),7);
'''+enqueue+r'''
int main(void){
 memset(&r2_diag_scratch,0xa5,sizeof r2_diag_scratch);
 struct r2_lora_watchdog_state saved=r2_diag_scratch.lora;
 r2_controller_diag_init();assert(!memcmp(&saved,&r2_diag_scratch.lora,sizeof saved));
 assert(sizeof r2_diag_scratch==512);MFIFO_INIT(prep);
 for(unsigned i=0;i<7;i++){
  struct lll_prepare_param p={(void *)(uintptr_t)(0x100+i)};
  struct lll_event *e=ull_prepare_enqueue(NULL,NULL,&p,(lll_prepare_cb_t)(uintptr_t)(0x200+i),i&1);
  assert(e);e->is_aborted=(i>>1)&1;
 }
 uint32_t w[23];r2_controller_diag_snapshot(w);assert(w[0]==7&&!w[6]);
 struct lll_prepare_param p={(void *)0x999};
 assert(!ull_prepare_enqueue(NULL,NULL,&p,(lll_prepare_cb_t)0x888,0));
 r2_controller_diag_snapshot(w);assert(w[0]==7&&w[4]==0x888&&w[5]==0x999&&w[6]==8&&w[21]==ms);
 for(unsigned i=0;i<7;i++){assert(w[7+i*2]==0x200+i&&w[8+i*2]==0x100+i);assert(((w[22]>>(2*i))&3)==((i&1)|(((i>>1)&1)<<1)));}
 uint32_t frozen[23];memcpy(frozen,w,sizeof w);p.param=(void *)0x777;
 assert(!ull_prepare_enqueue(NULL,NULL,&p,(lll_prepare_cb_t)0x666,0));
 for(unsigned i=0;i<4;i++)r2_controller_diag_count(i);
 r2_controller_diag_snapshot(w);assert(!memcmp(frozen,w,sizeof w));
 r2_controller_diag_console(9,ms);assert(notes==1);r2_controller_diag_console(9,ms);assert(notes==1);
 ms+=1000;r2_controller_diag_console(9,ms);assert(notes==2);
 assert(!memcmp(&saved,&r2_diag_scratch.lora,sizeof saved));
 r2_controller_diag_init();for(unsigned i=0;i<4;i++){r2_diag_scratch.controller.words[i]=UINT32_MAX;r2_controller_diag_count(i);assert(r2_diag_scratch.controller.words[i]==UINT32_MAX);}
 puts("PASS actual pinned7-entry enqueue/full FIFO, complete immutable failure snapshot, callback/param/flags, bounded console outsideIRQ, saturation and liveLoRa512B isolation");
}
'''
(out/'test.c').write_text(test)
cmd=[os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-Wno-unused-function','-fsanitize=address,undefined','-fno-sanitize-recover=all','-DR2_CONTROLLER_DIAGNOSTICS=1','-I'+str(out),'-I'+str(app/'include'),'-I'+str(a.deps.resolve()/'zephyr/subsys/bluetooth/controller/util'),str(out/'test.c'),str(app/'src/controller_diag.c'),'-o',str(out/'test')]
passed=False
try:
 with (out/'compile.log').open('x')as log:subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True)
 with (out/'run.log').open('x')as log:subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True)
 passed=True
finally:
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'command':cmd,'extracted_sha256':hashlib.sha256(enqueue.encode()).hexdigest()},indent=2)+'\n')
print('PASS bounded controller diagnostic production bodies')

#!/usr/bin/env python3
"""Pinned controller completion race and exact GM/HCI serialization; offline.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse, hashlib, json, os, re, shutil, subprocess
from pathlib import Path
from gm7_controller_hooks import generate
from ticker_backport import generate as ticker
from controller_diag_hooks import generate as diag
from radio_capture_hooks import generate as radio
from precision_schedule import generate as precision
p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
a.deps=a.deps.resolve();out=a.out.resolve();out.mkdir(parents=True,exist_ok=False)
app=Path(__file__).resolve().parents[1];z=a.deps/'zephyr'
ticker(z,out/'ticker',True);diag(out/'ticker/ll_sw/ull.c',out/'diag');radio(z,out/'radio');precision(z,out/'precision')
generate(out/'diag/ull.c',out/'radio/lll_scan.c',out/'radio/lll_scan_aux.c',out/'precision/ull_adv_sync.c',out/'gm',z/'subsys/bluetooth/controller/hci/hci_driver.c')
def fn(body,name):
 m=re.search(r'^.*?\b'+name+r'\([^;]*?\)\s*\{',body,re.M);assert m,name
 start=body.index('{',m.start());depth=1;i=start+1
 while depth:
  if body[i]=='{':depth+=1
  if body[i]=='}':depth-=1
  i+=1
 return body[m.start():i]
ull=(out/'gm/ull.c').read_text();native=(app/'src/gm7_controller.c').read_text();sync=(out/'gm/ull_adv_sync.c').read_text();hci=(out/'gm/hci_driver.c').read_text()
# Use the exact command critical section, with transport delivery outside it.
section=re.search(r'\tr2_gm7_thread_lock\(\);\n\tevt = hci_cmd_handle\(buf, \(void \*\*\) &node_rx\);\n\tr2_gm7_thread_unlock\(\);',hci);assert section
src=r'''
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#define K_FOREVER -1
#define TICKER_STATUS_BUSY 2
#define K_MUTEX_DEFINE(n) static pthread_mutex_t n = PTHREAD_MUTEX_INITIALIZER
static pthread_mutex_t state = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed = PTHREAD_COND_INITIALIZER;
static bool attempted, entered;
static _Thread_local bool hci_thread, held;
static unsigned sem_ticker_api_cb;
static void k_mutex_lock(pthread_mutex_t *m,int timeout) {
 (void)timeout;
 if(hci_thread){pthread_mutex_lock(&state);attempted=true;pthread_cond_signal(&changed);pthread_mutex_unlock(&state);}
 assert(!pthread_mutex_lock(m));assert(!held);held=true;
}
static void k_mutex_unlock(pthread_mutex_t *m){assert(held);held=false;assert(!pthread_mutex_unlock(m));}
static void k_sem_give(unsigned *s){*s=1;}
static void k_sem_take(unsigned *s,int timeout){(void)timeout;assert(*s);*s=0;}
'''+fn(ull,'ull_ticker_status_give')+'\n'+fn(ull,'ull_ticker_status_take')+'''
K_MUTEX_DEFINE(gm7_thread_api);
'''+fn(ull,'r2_gm7_thread_lock')+'\n'+fn(ull,'r2_gm7_thread_unlock')+r'''
static int hci_cmd_handle(void *buf,void **node) {
 (void)buf;(void)node;assert(held);
 pthread_mutex_lock(&state);entered=true;pthread_mutex_unlock(&state);
 volatile uint32_t done=TICKER_STATUS_BUSY;
 ull_ticker_status_give(0,(void*)&done);
 assert(!ull_ticker_status_take(TICKER_STATUS_BUSY,&done));return 0;
}
static void *run_hci(void *arg){
 (void)arg;hci_thread=true;void *buf=NULL,*node_rx=NULL;int evt;
'''+section[0]+r'''
 assert(!held && evt==0);return NULL;
}
static pthread_t worker;
static int operation(void){
 assert(held);assert(!pthread_create(&worker,NULL,run_hci,NULL));
 pthread_mutex_lock(&state);
 while(!attempted)pthread_cond_wait(&changed,&state);
 assert(!entered);pthread_mutex_unlock(&state);
 /* HCI is waiting for the same lock: only this pending callback can finish
  * this operation. A real ISR completion does not acquire the thread lock. */
 volatile uint32_t done=TICKER_STATUS_BUSY;
 ull_ticker_status_give(0,(void*)&done);
 assert(!ull_ticker_status_take(TICKER_STATUS_BUSY,&done));
 return -EAGAIN;
}
struct gm7_scheduler { int unused; };
static int controller_submit(unsigned s,struct gm7_scheduler *j,const uint8_t a[6],uint64_t n){(void)s;(void)j;(void)a;(void)n;return operation();}
static int controller_drain(unsigned s,uint64_t n){(void)s;(void)n;return operation();}
static int gm7_precision_phase_step(uint8_t h,int s){(void)h;(void)s;return operation();}
'''+fn(native,'r2_gm7_controller_submit')+'\n'+fn(native,'r2_gm7_controller_drain')+'\n'+fn(sync,'r2_precision_phase_step')+r'''
int main(void){
 /* Reproduce the old failure with the pinned, unmodified completion code. */
 volatile uint32_t native=2,hci=2;
 ull_ticker_status_give(0,(void*)&hci);
 assert(ull_ticker_status_take(2,&native)==2 && native==2 && hci==0);
 puts("REPRODUCED old shared completion: caller returns BUSY with callback pending");
 for(unsigned i=0;i<300;i++){
  attempted=entered=false;int rc;
  if(i%3==0)rc=r2_gm7_controller_submit(0,NULL,NULL,0);
  else if(i%3==1)rc=r2_gm7_controller_drain(0,0);
  else rc=r2_precision_phase_step(0,1);
  assert(rc==-EAGAIN && !held);assert(!pthread_join(worker,NULL));
  assert(entered && !sem_ticker_api_cb);
 }
 puts("PASS exact submit/drain/phase/HCI guard: 300 contended transactions; callback remains live; errors unlock");
}
'''
(out/'test.c').write_text(src)
cc=os.environ.get('CC') or shutil.which('clang');cmd=[cc,'-std=c11','-Wall','-Wextra','-Werror','-pthread','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'test.c'),'-o',str(out/'test')]
passed=False
try:
 with (out/'test.log').open('w') as log:
  subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=40)
  subprocess.run([str(out/'test')],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=40)
 passed=True
finally:
 (out/'receipt.json').write_text(json.dumps({'passed':passed,'hardware':False,'command':cmd,'extracted_sha256':hashlib.sha256(src.encode()).hexdigest()},indent=2)+'\n')
print((out/'test.log').read_text())

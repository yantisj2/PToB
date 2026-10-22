#!/usr/bin/env python3
"""Actual health formatter + actual idle-note admission; synthetic host clocks/locks.
Copyright (c)2026 Jonathan Yantis. MIT harness; extracted Zephyr/PToB bodies retain Apache-2.0.
"""
from pathlib import Path
import argparse,hashlib,json,os,re,shutil,subprocess
APP=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('--out',type=Path,required=True);a=p.parse_args()
O=a.out.resolve();O.mkdir(parents=True,exist_ok=False)
H=APP/'src/health.c';C=APP/'src/console.c';A=APP/'include/app.h'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def fn(s,name):
 m=re.search(r'^(?:static )?(?:int|void|bool) '+name+r'\([^;]*?\)\n\{',s,re.M);assert m,name
 return s[m.start():s.index('\n}',m.end())+2] if '\n}' in s[m.end():] else s[m.start():s.index('}',m.end())+1]
h=H.read_text();c=C.read_text();candidate=fn(h,'r2_health_console')
commit=""" detail_due=now+60000;reported_drops=io.tx_drops;reported_expired=io.tx_expired;
 reported_flash_error=flash_error;reported_pa_error=pa_error;
"""
accepted=""" /* Keep the pending detail through best-effort admission refusal. The
  * existing one-second/alternating watchdog gates bound the next attempt. */
 if(!r2_console_note(line,(size_t)n,now+250)){
  detail_due=now+60000;reported_drops=io.tx_drops;reported_expired=io.tx_expired;
  reported_flash_error=flash_error;reported_pa_error=pa_error;
 }"""
assert candidate.count(accepted)==1
original=candidate.replace(accepted,' (void)r2_console_note(line,(size_t)n,now+250);')
needle=' if(!changed&&now<detail_due)return;\n'
assert original.count(needle)==1
original=original.replace(needle,needle+commit)
assert candidate!=original and candidate.count('detail_due=')==1
(O/'health-original.inc').write_text(original+'\n');(O/'health-candidate.inc').write_text(candidate+'\n')
globals=h[h.index('static uint64_t boot_id;'):h.index('static uint32_t crc(')]
note=fn(c,'r2_console_note').replace('r2_console_note(', 'production_console_note(',1)
snapshot=fn(c,'r2_console_snapshot')
io=re.search(r'^struct r2_io .*?;',A.read_text(),re.M).group(0)
# struct r2_io has fields separated by semicolons; take full single line.
io=next(x for x in A.read_text().splitlines()if x.startswith('struct r2_io '))
pre=r'''
/* MIT host harness; extracted production bodies retain Apache-2.0. */
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#define R2_HEALTH_LANES 2
#define FAULT_WORDS 64
#define FAULT_MAGIC 0x52324631u
#define R2_SOURCE_SHA "72bc42db26e0ff0d0ca75d18eed1d7f947d7f18d478b1bde245f8727be9ae1a9"
#define R2_REPLY_BYTES 3072
#define R2_TX_SLOTS 2
#define K_NO_WAIT 0
static uint64_t now_ms;
static uint64_t k_uptime_get(void){return now_ms;}
static unsigned depth;
static unsigned irq_lock(void){return depth++;}
static void irq_unlock(unsigned key){assert(depth==key+1);depth=key;}
typedef unsigned k_spinlock_key_t;
static int lock,reply_producer;
static bool mutex_busy;
static int k_mutex_lock(void *p,int wait){(void)p;(void)wait;return mutex_busy?-EBUSY:0;}
static int k_mutex_unlock(void *p){(void)p;return 0;}
static k_spinlock_key_t k_spin_lock(void *p){(void)p;return irq_lock();}
static void k_spin_unlock(void *p,k_spinlock_key_t key){(void)p;irq_unlock(key);}
static bool k_is_in_isr(void){return false;}
struct tx_slot {char data[R2_REPLY_BYTES];size_t length,offset;uint64_t expiry;bool note;};
static struct tx_slot tx[R2_TX_SLOTS];
static unsigned head,tail,count;
static bool reply_copying;
static uint32_t rx_epoch;
static size_t stack_unused=1024;
static uint32_t diag_unused=824;
static void *k_current_get(void){return NULL;}
static int k_thread_stack_space_get(void *p,size_t *s){(void)p;*s=stack_unused;return 0;}
static uint32_t r2_diag_stack_unused(void){return diag_unused;}
static unsigned wd_calls;
static void r2_watchdog_console(char *s,size_t n){(void)s;(void)n;++wd_calls;}
'''
pre+=io+'\nstatic struct r2_io stats;\n'+note+'\n'+snapshot+r'''
static unsigned attempts,accepted,rejected;
static size_t last_length;
static int r2_console_note(const char *s,size_t n,uint64_t expiry){
 ++attempts;last_length=n;assert(!depth);
 int rc=production_console_note(s,n,expiry);
 if(rc)++rejected;else ++accepted;
 return rc;
}
static uint32_t previous[FAULT_WORDS],latency[9];
'''+globals+r'''
#ifdef CANDIDATE
#include "health-candidate.inc"
#else
#include "health-original.inc"
#endif
static void drain(void){head=tail=count=0;memset(tx,0,sizeof tx);}
static void reset(void){
 memset(previous,0,sizeof previous);memset(latency,0,sizeof latency);memset(operations,0,sizeof operations);
 memset(&stats,0,sizeof stats);stats.dtr=true;drain();reply_copying=mutex_busy=false;
 now_ms=report_due=detail_due=0;reported_drops=reported_expired=0;reported_flash_error=reported_pa_error=0;
 boot_id=9;beats=beat_ms=0;stack_unused=1024;diag_unused=824;attempts=accepted=rejected=wd_calls=0;
#ifdef R2_WATCHDOG
 watchdog_report_due=false;
#endif
}
static void turn(uint64_t t){now_ms=t;r2_health_console();assert(!depth);}
static void next_health(uint64_t t){
#ifdef R2_WATCHDOG
 turn(t); /* Existing watchdog opportunity, no health submission. */
 turn(t+1000);
#else
 turn(t);
#endif
}
static void lost_then_idle(bool busy_mutex){
 reset();stats.tx_drops=5;stats.tx_expired=7;operations[0].result=-EIO;operations[1].result=-EINVAL;
 if(busy_mutex)mutex_busy=true;else count=1;
 turn(1000);assert(attempts==1&&rejected==1&&accepted==0);
#ifdef CANDIDATE
 assert(!detail_due&&!reported_drops&&!reported_expired&&!reported_flash_error&&!reported_pa_error);
#else
 assert(detail_due==61000&&reported_drops==5&&reported_expired==7&&reported_flash_error==-EIO&&reported_pa_error==-EINVAL);
#endif
 mutex_busy=false;drain();next_health(2000);
#ifdef CANDIDATE
 assert(attempts==2&&accepted==1&&reported_drops==5&&reported_expired==7&&reported_flash_error==-EIO&&reported_pa_error==-EINVAL);
#else
 assert(attempts==1&&accepted==0);
#endif
 printf("rejected_%s_then_idle attempts=%u admitted=%u detail_due=%llu PASS\n",busy_mutex?"mutex":"occupied_queue",attempts,accepted,(unsigned long long)detail_due);
}
static void exact_boundary(void){
 reset();turn(1000);assert(accepted==1&&detail_due==61000);drain();
#ifdef R2_WATCHDOG
 turn(2000);turn(59000);turn(60000);turn(60999);
#else
 turn(60000);turn(60999);
#endif
 assert(attempts==1&&accepted==1);turn(61000);assert(attempts==2&&accepted==2&&detail_due==121000);
 puts("successful_note exact_original_60000ms_boundary PASS");
}
static void repeated_phase_collision(void){
 reset();for(uint64_t t=1000;t<=241000;t+=1000){drain();count=(t%60000==1000);turn(t);}
#ifdef CANDIDATE
 assert(accepted>=4&&rejected==1);
#else
 assert(accepted==0&&rejected==5&&attempts==5);
#endif
 printf("synthetic_same_phase_collision elapsed=241000 attempts=%u admitted=%u rejected=%u PASS_CONDITIONAL\n",attempts,accepted,rejected);
}
static void maximum_formatter(const char *path){
 reset();now_ms=UINT64_MAX-60001;boot_id=UINT64_MAX;beats=beat_ms=UINT32_MAX;
 stack_unused=diag_unused=stats.tx_drops=stats.tx_expired=UINT32_MAX;
 for(unsigned i=0;i<9;i++)latency[i]=UINT32_MAX;
 for(unsigned i=0;i<2;i++)operations[i]=(struct operation){UINT32_MAX,INT32_MIN,UINT32_MAX};
 for(unsigned i=0;i<64;i++)previous[i]=UINT32_MAX;previous[0]=FAULT_MAGIC;
 r2_health_console();assert(accepted==1&&count==1&&last_length<sizeof line&&last_length<=1536);
 assert(tx[0].length==last_length&&!memcmp(tx[0].data,line,last_length));
 FILE *f=fopen(path,"wb");assert(f&&fwrite(line,1,last_length,f)==last_length&&!fclose(f));
 printf("maximum_word_formatter bytes=%zu capacity=%zu remaining=%zu PASS\n",last_length,sizeof line,sizeof line-last_length);
}
static void dtr_and_throttle(void){
 reset();stats.dtr=false;turn(1000);assert(!attempts&&!detail_due&&report_due==2000);
 stats.dtr=true;count=1;turn(1500);assert(!attempts);turn(2000);assert(attempts==1&&rejected==1);
 drain();turn(2001);turn(2999);assert(attempts==1);
#ifdef R2_WATCHDOG
 turn(3000);turn(3999);assert(attempts==1);turn(4000);
#else
 turn(3000);
#endif
#ifdef CANDIDATE
 assert(attempts==2&&accepted==1);
#else
 assert(attempts==1&&!accepted);
#endif
 puts("DTR_false_and_existing_1000ms_alternating_gate PASS");
}
int main(int argc,char **argv){assert(argc==2);dtr_and_throttle();lost_then_idle(false);lost_then_idle(true);exact_boundary();repeated_phase_collision();maximum_formatter(argv[1]);return 0;}
'''
(O/'test.c').write_text(pre)
results=[]
for candidate in [False,True]:
 for wd in [False,True]:
  name=('candidate'if candidate else'original')+('-watchdog'if wd else'-no-watchdog')
  cmd=[os.environ.get('CC') or ('/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').is_file() else shutil.which('clang')),'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-function','-Wno-unused-variable','-fsanitize=address,undefined','-fno-sanitize-recover=all',*(['-DCANDIDATE=1']if candidate else[]),*(['-DR2_WATCHDOG=1']if wd else[]),str(O/'test.c'),'-o',str(O/name)]
  with (O/(name+'.log')).open('x')as log:
   subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=30)
   subprocess.run([str(O/name),str(O/(name+'-max.txt'))],stdout=log,stderr=subprocess.STDOUT,check=True,timeout=10)
  text=(O/(name+'-max.txt')).read_text();payload=json.loads(text.removeprefix('R2_HEALTH '));assert len(payload['previous_fault'])==64 and payload['flash'][1]==-(1<<31)
  results.append({'name':name,'passed':True,'max_bytes':len(text.encode()),'command':cmd})
assert len({(O/(x['name']+'-max.txt')).read_bytes() for x in results})==1
(O/'receipt.json').write_text(json.dumps({'hardware':False,'firmware_edits':False,'source':'ACTUAL_CHECKED_OUT_HEALTH_AND_CONSOLE_BODIES','results':results,'inputs':{str(p):sha(p)for p in[H,C,A]},'production_bodies':['r2_health_console','r2_console_note','r2_console_snapshot'],'candidate':'Only commit existing detail_due/reported fields after note admission succeeds','limitations':['Fake time/locks/queue drain/WD function boundaries','Original console admission body is executed, but no USB delivery or physical concurrent scheduling','Repeated collision is assigned by harness, not established as field cause','Successful admission is not delivery acknowledgement; accepted notes may later expire','Maximum-word synthetic capsule stresses scalar bounds, not semantic validity of a real retained capsule']},indent=2)+'\n')
print('PASS original loss + candidate retry; actual note admission; unchanged60s success boundary; maximumword formatter fits1536')

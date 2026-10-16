#!/usr/bin/env python3
"""Exact generated upstream function bodies: idle/busy/error teardown, no hardware."""
from pathlib import Path
import tempfile,subprocess,argparse
p=argparse.ArgumentParser();p.add_argument("--deps",type=Path,required=True);args=p.parse_args()
from ticker_backport import generate,APP
with tempfile.TemporaryDirectory(prefix='r2-ticker-test-') as tmp:
    out=Path(tmp);generate(args.deps/'zephyr',out)
    ticker=(out/'ticker/ticker.c').read_text();fn=ticker[ticker.index('int ticker_deinit('):ticker.index('/**\n * @brief Check if ticker instance is initialized')]
    ull=(out/'ll_sw/ull.c').read_text();end=ull.index('\nvoid ll_reset(',ull.index('int ll_deinit('));deinit=ull[ull.index('int ll_deinit('):end]
    source='''#include <stdint.h>
#include <errno.h>
#include <assert.h>
#define TICKER_INSTANCE_MAX 1
#define TICKER_INSTANCE_ID_CTLR 0
#define TICKER_NULL 255
struct ticker_instance { unsigned ticker_id_head,count_node; };
static struct ticker_instance _instance[1];
static int lll_error,resets;
static void ll_reset(void){resets++;}
static int lll_deinit(void){return lll_error;}
'''+fn+deinit+'''
int main(void){
 _instance[0]=(struct ticker_instance){TICKER_NULL,5};
 assert(ticker_deinit(1)==-EINVAL&&_instance[0].count_node==5);
 _instance[0].ticker_id_head=0;assert(ticker_deinit(0)==-EBUSY&&_instance[0].count_node==5);
 _instance[0].ticker_id_head=TICKER_NULL;lll_error=-EIO;
 assert(ll_deinit()==-EIO&&_instance[0].count_node==5&&resets==1);
 lll_error=0;assert(!ll_deinit()&&!_instance[0].count_node&&resets==2);
}
'''
    (out/'test.c').write_text(source)
    subprocess.run([(__import__('os').environ.get('CC') or __import__('shutil').which('clang')),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-sanitize-recover=all',str(out/'test.c'),'-o',str(out/'test')],check=True,timeout=30)
    subprocess.run([str(out/'test')],check=True,timeout=10)
    print('Exact upstream controller/ticker teardown bodies: out-of-range/busy/LLL failure refuse; idle success clears stale initialized state PASS')

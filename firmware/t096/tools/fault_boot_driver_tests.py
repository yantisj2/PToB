#!/usr/bin/env python3
"""Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT.
Exact pinned dispatcher/pre-radio source-order proof. No hardware operation.
Generated fragments retain original Zephyr Apache-2.0 terms and authorship.
"""
import argparse,hashlib,json,os,re,subprocess
from pathlib import Path
from flash_thread_guard import generate
APP=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def fn(s,name):
    m=re.search(r'^(?:static )?(?:int|bool)\s+'+name+r'\([^;]*?\)\s*\{',s,re.M);assert m,name
    i=s.index('{',m.start());level=1;j=i+1
    while level:level+=(s[j]=='{')-(s[j]=='}');j+=1
    return s[m.start():j]
def main():
    p=argparse.ArgumentParser();p.add_argument('--deps',type=Path,required=True);p.add_argument('--build',type=Path,required=True);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    out=a.out.resolve();out.mkdir(parents=True,exist_ok=False);z=a.deps.resolve()/'zephyr'
    driver=z/'drivers/flash/soc_flash_nrf.c';ticker=z/'subsys/bluetooth/controller/flash/soc_flash_nrf_ticker.c'
    manifest=generate(ticker,out/'generated');generated=out/'generated/soc_flash_nrf_ticker.c'
    body=fn(driver.read_text(),'flash_nrf_write');required=fn(generated.read_text(),'nrf_flash_sync_is_required')
    assert required==fn(ticker.read_text(),'nrf_flash_sync_is_required')and'return ticker_is_initialized(0);'in required
    assert body.index('if (nrf_flash_sync_is_required())')<body.index('write_synchronously(')<body.index('ret = write(')
    assert '#define SYNC_LOCK() k_sem_take(&sem_lock, K_FOREVER)'in driver.read_text()
    assert 'while (!nrfx_nvmc_write_done_check())'in driver.read_text()
    # Actual boot order: diag init invokes fault boot before platform launches
    # the sole bt_enable caller; HCI open initializes the controller ticker.
    main=(APP/'src/main.c').read_text();main=main[main.index('int main(void)'):]
    assert main.index('r2_diag_init(')<main.index('r2_lora_init(')<main.index('r2_platform_start(')
    diagnostic=(APP/'src/diagnostic.c').read_text();assert'r2_fault_boot(id,package);'in diagnostic
    platform=(APP/'src/platform.c').read_text();assert'bt_enable(bluetooth_ready)'in platform
    callers=[p for p in(APP/'src').glob('*.c')if re.search(r'\bbt_enable\s*\(',p.read_text())]
    assert callers==[APP/'src/platform.c'],callers
    boot=(APP/'src/fault_boot.c').read_text()
    assert boot.index('if(nrf_flash_sync_is_required()){')<boot.index('fault_boot_state.write_open=true;')
    assert 'if(!fault_boot_state.write_open||nrf_flash_sync_is_required()'in boot
    config=(a.build/'target/zephyr/.config').read_text().splitlines()
    assert all(q in config for q in ('CONFIG_SOC_FLASH_NRF_RADIO_SYNC_TICKER=y','CONFIG_FLASH=y','CONFIG_MULTITHREADING=y'))
    assert 'CONFIG_BT_SETTINGS=y'not in config
    compiled=json.loads((a.build/'target/compile_commands.json').read_text())
    selected=[Path(q['file'])for q in compiled if q['file'].endswith('soc_flash_nrf_ticker.c')];assert len(selected)==1
    assert selected[0].read_bytes()==generated.read_bytes()
    (out/'fault_boot_driver_actual.inc').write_text(required+'\n'+body+'\n')
    cc=os.environ.get('CC')or('/opt/homebrew/opt/llvm/bin/clang'if Path('/opt/homebrew/opt/llvm/bin/clang').is_file()else'clang')
    command=[cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wno-unused-parameter','-fsanitize=address,undefined','-fno-sanitize-recover=all','-I'+str(out),str(APP/'tests/fault_boot_driver_test.c'),'-o',str(out/'test')]
    passed=False
    try:
        with(out/'test.log').open('x')as log:
            subprocess.run(command,check=True,stdout=log,stderr=subprocess.STDOUT,timeout=45)
            subprocess.run([str(out/'test')],check=True,stdout=log,stderr=subprocess.STDOUT,timeout=45)
        passed=True
    finally:
        paths=[Path(__file__),APP/'tests/fault_boot_driver_test.c',APP/'tools/flash_thread_guard.py',
            APP/'src/fault_boot.c',APP/'src/diagnostic.c',APP/'src/main.c',APP/'src/platform.c',
            driver,ticker,selected[0],a.build/'target/zephyr/.config',
            z/'subsys/bluetooth/controller/hci/hci_driver.c',z/'subsys/bluetooth/controller/ll_sw/ull.c']
        (out/'receipt.json').write_text(json.dumps(dict(status='PASS'if passed else'FAILED_RETAINED',hardware=False,
            manifest=manifest,inputs={str(p):sha(p)for p in paths},limits=[
            'Exact public dispatch/source order; semaphore/NVMC/ticker-state boundaries are modeled.',
            'Source-order plus fail-closed ticker gate excludes THREAD ticker enqueue/sem_sync during startup append.',
            'Public driver has K_FOREVER semaphore and NVMC-ready polling.500ms is checked around calls, not a forcibly interruptible driver timeout.',
            'Existing observer/parent watchdog recovery remains; total delay depends on the stall path and is not a hard30s limit. No physical flash latency/acceptance claim.']),indent=2)+'\n')
    print((out/'test.log').read_text())
if __name__=='__main__':main()

#!/usr/bin/env python3
"""Focused GM supervisor -> unchanged watchdog production-body composition.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import argparse, hashlib, json, os, shutil, subprocess
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--out', required=True, type=Path)
a = p.parse_args()
app = Path(__file__).resolve().parents[1]
repo = app.parents[1]
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=False)
cc = os.environ.get('CC') or ('/opt/homebrew/opt/llvm/bin/clang' if Path('/opt/homebrew/opt/llvm/bin/clang').exists() else shutil.which('clang'))
def sha(f):
    return hashlib.sha256(f.read_bytes()).hexdigest()
def write_receipt():
    (out/'receipt.json').write_text(json.dumps(receipt, indent=2)+'\n')

source = (app/'src/gm7_service.c').read_text()
stop = source[source.index('void r2_service_stop(void) {'):source.index('void r2_service_supervise(void) {')]
supervise = source[source.index('void r2_service_supervise(void) {'):source.index('int r2_service_close_result(void) {')]
legacy = (app/'tests/gm7_supervision178.h').read_text()
(out/'production-supervision.inc').write_text(stop+legacy+supervise.replace('void r2_service_supervise(void)', 'static void current_supervise(void)'))
start = source[source.index('  uint64_t cycle_started = kernel_now();'):source.index('  struct gm7_bridge b;', source.index('static void cycle('))]
end = source[source.index('  (void)k_mutex_lock(&lock, K_FOREVER);', source.index('finished:\n')):source.index('K_WORK_DEFINE(service_work, cycle);')]
(out/'production-worker-boundaries.inc').write_text('static uint64_t worker_begin(void) {\n'+start+'  return cycle_started;\n}\nstatic void worker_finish(uint64_t cycle_started) {\n'+end)
fixture = (app/'tests/watchdog_test.c').read_text().replace('int main(int argc,char **argv)', 'int historical_watchdog_main(int argc,char **argv)')
assert fixture.endswith('}\n')
fixture = fixture[:-2]+' return 0;\n}\n'
(out/'gm7_watchdog_fixture.inc').write_text(fixture)
for name in ['zephyr/kernel.h', 'zephyr/device.h', 'zephyr/drivers/watchdog.h', 'zephyr/drivers/hwinfo.h', 'zephyr/sys/reboot.h', 'zephyr/sys/atomic.h', 'cmsis_core.h', 'build_identity.h']:
    f = out/name
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_text('/* Fake public platform boundary. */\n')
inputs = [app/'src'/n for n in ['gm7_service.c', 'watchdog.c', 'watchdog_policy.c']]
inputs += [app/'tests'/n for n in ['gm7_supervision_test.c', 'gm7_supervision_watchdog_test.c', 'gm7_supervision178.h', 'gm7_service_fake.h', 'gm7_resume_progress.h', 'watchdog_test.c', 'zephyr_fake.h']]
inputs += sorted((app/'include').glob('*.h'))+sorted((app/'protocol').glob('*.h'))+[Path(__file__).resolve()]
before = {str(f.relative_to(repo)): sha(f) for f in inputs}
receipt = {'status': 'INCOMPLETE', 'hardware': False, 'scope': 'Actual GM stop/supervise/worker boundary and unchanged watchdog bodies; explicit fake platform/clock schedules.', 'sources': before, 'commands': []}
flags = [cc, '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wno-unused-function', '-Wno-unused-variable', '-Wno-unused-parameter', '-Wno-default-const-init-var-unsafe', '-fsanitize=address,undefined', '-fno-sanitize-recover=all', *['-I'+str(x) for x in [out, app/'include', app/'protocol', app/'tests']]]
write_receipt()
try:
    for name, filename in [('supervise', 'gm7_supervision_test.c'), ('watchdog', 'gm7_supervision_watchdog_test.c')]:
        command = flags+[str(app/'tests'/filename)]+([str(app/'src/watchdog_policy.c')] if name=='watchdog' else [])+['-o', str(out/(name+'-test'))]
        receipt['commands'].append(command)
        with (out/(name+'.compile.log')).open('w') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=45)
        command = [str(out/(name+'-test')), str(out/'snapshots.bin')]
        receipt['commands'].append(command)
        with (out/(name+'.run.log')).open('w') as log:
            subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True, timeout=30)
    assert all(sha(repo/name)==value for name, value in before.items())
    receipt['status'] = 'PASS'
finally:
    receipt['files'] = {str(f.relative_to(out)): sha(f) for f in out.rglob('*') if f.is_file() and f.name!='receipt.json'}
    write_receipt()
print('PASS actual GM supervisor/worker -> unchanged watchdog composition; hardware UNKNOWN:', out)

#!/usr/bin/env python3
"""Actual queued ticker progress regression; fake HAL/LLCP boundaries, no hardware.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
Generated Zephyr functions retain their original Apache-2.0 license/authorship.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import ticker_backport

p = argparse.ArgumentParser()
p.add_argument('--deps', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
app = Path(__file__).resolve().parents[1]
out = a.out.resolve()
out.mkdir(parents=True, exist_ok=False)
z = a.deps / 'zephyr'
ticker_backport.generate(z, out / 'generated', True)
s = (out / 'generated/ticker/ticker.c').read_text()
conn = (z / 'subsys/bluetooth/controller/ll_sw/ull_conn.c').read_text()


def fn(source, name):
    m = re.search(r'^(?:static |void |uint8_t |uint32_t )[^\n]*\b' + name +
                  r'\([^;]*?\)\n\{', source, re.M)
    assert m, name
    return source[m.start():source.index('\n}', m.end()) + 2]


def sha(text):
    return hashlib.sha256(text.encode()).hexdigest()


begin = s.index('struct ticker_user_op_start {')
end = s.index('\n/* User data structure for operations', begin)
user = s.index('struct ticker_user {')
(out / 'ticker_fairness_types.inc').write_text(
    s[begin:end] + '\n' + s[user:s.index('\n};', user) + 3] + '\n')
names = ['ticker_next_elapsed', 'ticker_enqueue', 'ticker_resolve_collision',
         'ticker_worker', 'ticks_to_expire_prep', 'ticker_add_to_remainder',
         'ticker_remainder_inc', 'ticker_job_worker_bh', 'ticker_job_insert',
         'ticker_dequeue', 'ticker_remainder_dec', 'r2_ticker_remainder_step',
         'ticker_job_op_cb', 'ticker_job_node_update', 'ticker_job_node_manage',
         'ticker_job_op_start', 'ticker_job_list_manage', 'ticker_job_list_insert',
         'ticker_job_op_inquire', 'ticker_job_list_inquire', 'ticker_job']
bodies = {name: fn(s, name) for name in names}
actual = '\n\n'.join(bodies.values()) + '\n'
begin = s.index('#if defined(CONFIG_BT_TICKER_EXT)\nuint8_t ticker_start(')
end = s.index('\n#if defined(CONFIG_BT_TICKER_UPDATE)', begin)
actual += '''
uint8_t ticker_start_ext(uint8_t,uint8_t,uint8_t,uint32_t,uint32_t,uint32_t,uint32_t,uint16_t,uint32_t,ticker_timeout_func,void*,ticker_op_func,void*,struct ticker_ext*);
uint8_t ticker_update_ext(uint8_t,uint8_t,uint8_t,uint32_t,uint32_t,uint32_t,uint32_t,uint16_t,uint8_t,ticker_op_func,void*,uint8_t,uint8_t);
''' + s[begin:end]
begin = s.index('uint8_t ticker_update(uint8_t instance_index')
end = s.index('\n#endif /* CONFIG_BT_TICKER_UPDATE */', begin)
actual += s[begin:end] + '\n'
actual += '\n'.join(fn(s, name) for name in
                    ['ticker_stop', 'ticker_priority_set', 'ticker_stop_abs'])
producer = fn(conn, 'ull_conn_update_ticker')
(out / 'ticker_fairness_connection.inc').write_text(producer + '\n')
condition = '(equal_force && next_has_priority && !current_is_older)'
prior = '(next_has_priority && !current_is_older)'
assert actual.count(condition) == 1
legacy = bodies['ticker_resolve_collision'].replace(condition, prior)
(out / 'ticker_fairness_legacy.inc').write_text(
    legacy.replace('ticker_resolve_collision', 'legacy_resolve') + '\n')
compiler = os.environ.get('CC') or shutil.which('clang')
flags = [compiler, '-std=c11', '-O1', '-g', '-fsanitize=address,undefined',
         '-fno-sanitize-recover=all', '-I' + str(out), '-I' + str(app / 'tests')]
results = {}
for label, body, expected in [('candidate', actual, 0),
                               ('prior-condition', actual.replace(condition, prior), 1)]:
    (out / 'ticker_fairness_actual.inc').write_text(body + '\n')
    cmd = [*flags, *(['-DLEGACY_POLICY=1'] if expected else []),
           str(app / 'tests/ticker_fairness_test.c'), '-o', str(out / label)]
    subprocess.run(cmd, check=True, timeout=60)
    run = subprocess.run([str(out / label)], text=True, capture_output=True, timeout=30)
    (out / (label + '.log')).write_text(run.stdout + run.stderr)
    assert run.returncode == expected, (label, run.returncode, run.stdout, run.stderr)
    assert ('six_second_starvation_cases=2/4' if expected else
            'six_second_starvation_cases=0/4') in run.stdout
    results[label] = {'returncode': run.returncode, 'command': cmd, 'body_sha256': sha(body)}
(out / 'ticker_fairness_actual.inc').write_text(actual + '\n')
receipt = {'status': 'PASS', 'hardware': False, 'results': results,
           'generated_ticker_sha256': sha(s), 'ull_conn_sha256': sha(conn),
           'producer_sha256': sha(producer),
           'test_sources': {str(f.relative_to(app)): sha(f.read_text()) for f in
                            [app / 'tests/ticker_fairness_test.c', app / 'tests/ticker_fairness_fake.h', Path(__file__)]},
           'actual_functions': {k: sha(v) for k, v in bodies.items()},
           'limits': ['Fake counter/compare HAL, sequential mayfly/JOB dispatch and synthetic LLCP/LLL callback boundaries.',
                      'Two individually plausible DLE reservations exceed available period; tests progress, not capacity or field causation.',
                      'Critical/equal-force/cross-class precision controls; same-class precision force ordering can change.']}
(out / 'receipt.json').write_text(json.dumps(receipt, indent=2) + '\n')
print(json.dumps({'status': 'PASS', 'candidate': '0/4 starved', 'prior_condition': '2/4 starved'}))

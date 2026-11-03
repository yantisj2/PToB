"""Independent Python replay of actual C-owner records and falsification cases.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import copy,json,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from holdover_replay import replay
rows=[json.loads(x)for x in Path(sys.argv[1]).read_text().splitlines()]
r=replay(rows)['groups'][0]
assert r['supported_ms']==120000 and r['all_witness_replay_complete']
assert r['onboard_replay']['checks']>1000 and not r['onboard_replay']['mismatches']
assert r['rate_replay']['checks']>100 and not r['rate_replay']['rate_disagreements']and not r['rate_replay']['guard_disagreements']
missing=rows[:1000]+rows[1001:]
f=replay(missing)['groups'][0];assert f['missing_witness_records']==1 and not f['all_witness_replay_complete']
corrupt=copy.deepcopy(rows);corrupt[1100]['payload']['raw_pair'][1]+=1500
f=replay(corrupt)['groups'][0];assert f['onboard_replay']['mismatches']and not f['all_witness_replay_complete']
assert any(x['candidate']['failed_or_unknown']>1 for x in f['ages'])
print('PASS independent raw-witness/C-shadow replay, floating rate oracle,100 prospective prefixes, missing records and corrupt-witness negative controls')

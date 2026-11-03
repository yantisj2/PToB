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

batched=[]
for row in rows:
 p=row['payload'];tick,ns,available,seq=p['raw_pair']
 batched.append(dict(source=row['source'],boot=row['boot'],payload=dict(schema=3,records=[[seq,seq,tick,ns,available,p['channel'],int(p['pair'][0]==tick),p['epoch'],p['resets']]])))
 batched.append(row)
b=replay(batched)['groups'][0]
assert b['supported_ms']==r['supported_ms']and b['all_witness_replay_complete']
assert b['rate_replay']==r['rate_replay']and b['onboard_replay']==r['onboard_replay']
# USB can return a batch after a later full snapshot; availability ordering is
# causal and must preserve the same independent replay.
for i in range(0,len(batched)-4,4):batched[i:i+4]=batched[i+1:i+4]+batched[i:i+1]
assert replay(batched)['groups'][0]['onboard_replay']==r['onboard_replay']
print('PASS bounded batch expansion and USB reply reordering with no future fit inputs')
# First eligible entry is an explicit prospective rule, never chosen by a
# future residual. A short entry wait may settle; a missed deadline stays unknown.
origin=next(x['payload']['now_tick']for x in rows if x['payload'].get('available'))
late=copy.deepcopy(rows)
for row in late:
 p=row['payload'];age=p['now_tick']-origin
 if 32*16000000<=age<(32*16000000+12000000):p['available']=False
immediate=replay(late)['groups'][0]
deferred=replay(late,entry_wait_ms=1000)['groups'][0]
assert immediate['ages'][0]['unknown']==1 and deferred['ages'][0]['unknown']==0
assert deferred['supported_ms']==120000
late=copy.deepcopy(rows)
for row in late:
 p=row['payload'];age=p['now_tick']-origin
 if any((32+2*i)*16000000<=age<(32+2*i)*16000000+20000000 for i in (0,10,20)):p['available']=False
expired=replay(late,entry_wait_ms=1000)['groups'][0]
assert expired['ages'][0]['unknown']==3 and expired['supported_ms']is None
print('PASS prospective entry wait, first eligible model, explicit missed deadlines and unchanged failure denominators')

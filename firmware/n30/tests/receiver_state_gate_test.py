"""Reject stale, partial or hardware-derived evidence at the offline gate."""
import importlib.util,json,tempfile
from pathlib import Path
p=Path(__file__).resolve().parents[1]/'tools/receiver_state_tests.py'
s=importlib.util.spec_from_file_location('receiver_gate',p);m=importlib.util.module_from_spec(s);s.loader.exec_module(m)
m.sources=lambda repo=m.REPO:{'synthetic-source.cpp':'a'*64}
valid=dict(status='PASS',hardware_access=False,sources=m.sources(),checks=[dict(name=x,returncode=0) for x in ('source-ui-sequences','native-n30-coupling','native-w12-recovery','w12-production-holdover')])
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory)/'receipt.json';p.write_text(json.dumps(valid));assert m.validate_receipt(p)==valid
 for change in ({'status':'FAIL'},{'hardware_access':True},{'sources':{'synthetic-source.cpp':'b'*64}},{'checks':valid['checks'][:-1]},{'checks':[dict(name=x['name'],returncode=1) for x in valid['checks']]}):
  p.write_text(json.dumps({**valid,**change}))
  try:m.validate_receipt(p)
  except ValueError:pass
  else:raise AssertionError('Invalid gate admitted: '+str(change))
print('PASS source-bound state-machine gate refuses stale/failed/partial/hardware evidence')

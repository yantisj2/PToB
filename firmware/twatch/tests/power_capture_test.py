# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from power_capture import continuity
def f(t,utc,valid=True,asleep=False,at=None):
 return {'begin':{'request_us':str(t)},'rows':{'TWATCH_UI':{'clock':str(int(valid)),'utc_us':str(utc),'ready':'1','usb':'1','asleep':str(int(asleep)),'sampled_us':str(t if at is None else at)}}}
assert continuity([f(1,0,False),f(2,100),f(3,101)])['passed']
assert continuity([f(1,100),f(2,0,False)])['missing_after_acquisition']==1
assert continuity([f(1,100),f(2,99)])['backward_steps']==1
assert not continuity([f(1,100),f(4000000,100,at=1)])['passed']
assert continuity([f(1,100),f(4000000,100,asleep=True,at=1)])['passed']
assert not continuity([f(1,0,False)])['passed']
print('PASS clock continuity: acquisition, retained-clock loss, backward step, stale awake display, sleeping display and absent evidence')

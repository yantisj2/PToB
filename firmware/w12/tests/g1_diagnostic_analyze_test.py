import importlib.util
from pathlib import Path
path=Path(__file__).resolve().parents[1]/'tools/g1_diagnostic_analyze.py'
spec=importlib.util.spec_from_file_location('diagnostic',path);module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
rows=[]
for event,at,relative in [('CONNECT_REQUEST',1000000,0),('ACL_DESCRIPTOR',1300000,300000),('TERMINATE_ACK',1301000,301000),('CONNECT',1302000,302000),('DISCONNECT',1522000,522000)]:
    rows.append(f'W12_V12_LOG capture_id=0123456789abcdef log_seq=1 enqueue_us=1 record=W12_G1_LINK at_us={at} gen=1 peer=0000000000000001 event={event} rc=0 window_us={relative}')
result=module.analyze(('\n'.join(rows)+'\n').encode())
assert result['quiet_violations']==1
assert result['latency_max_us']['terminate_to_DISCONNECT_us']==221000
assert result['latency_max_us']['descriptor_to_CONNECT_us']==2000
assert module.analyze(b'W12_G1_LINK at_us=2 gen=1 peer=1 event=CONNECT rc=0 window_us=1\n')['attempts'][0]['DISCONNECT_window_us'] is None
assert module.analyze(b'W12_V12_LOG wrong=1\n')['counts']['malformed_envelope']==1
drain=module.analyze(b'W12_G1_LINK at_us=2 gen=1 peer=1 event=CONNECT rc=0 owners=0x25 window_us=1\n')['attempts'][0]
assert drain['events'][0]['event']=='CONNECT_DRAIN_ACK' and drain['request_to_CONNECT_us'] is None
pa=module.analyze(b'W12_G1_LINK at_us=2 gen=0 peer=1 event=SYNC_CANCEL_ACK rc=524 window_us=450000\nW12_G1_LINK at_us=3 gen=0 peer=1 event=SYNC_ESTABLISHED rc=0 window_us=451000 PA_phy=1 PA_clock_accuracy=7\nW12_G1_LINK at_us=4 gen=0 peer=0 event=PERMISSION_PHASE phase_us=125000 window_us=452000\n')
assert not pa['attempts'] and len(pa['sync_transitions'])==2 and len(pa['phase_changes'])==1
print('PASS original quiet failure, missing stages, exact envelopes')

stall=module.analyze(b'W12_G1_LINK at_us=100000 gen=0 peer=1 event=SYNC_REQUEST rc=0 window_us=100000\nW12_G1_LINK at_us=2200000 gen=0 peer=1 event=SYNC_CANCEL_ACK rc=19 window_us=2200000\nW12_G1_LINK at_us=2201000 gen=0 peer=1 event=SYNC_ESTABLISHED rc=0 window_us=201000\nW12_G1_LINK at_us=2202000 gen=0 peer=1 event=SYNC_LOST rc=0 window_us=202000\n')
assert stall['quiet_violations_sync']==1 and stall['quiet_violations_acl']==0 and stall['quiet_violations']==1
assert stall['sync_attempts'][0]['closure_window_us']==2202000

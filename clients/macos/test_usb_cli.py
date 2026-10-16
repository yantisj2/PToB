#!/usr/bin/env python3
"""USB-only product checks. Never enumerates hardware, opens USB or starts BLE."""
from pathlib import Path
import json,subprocess,tempfile
H=Path(__file__).resolve().parent;B=H/'.build/ptob-receiver'
checks=0
def check(ok,label):
 global checks
 assert ok,label;checks+=1
r=subprocess.run([str(B),'--self-test'],text=True,capture_output=True);check(r.returncode==0,r.stderr)
check('USB-only' in r.stdout,'USB-only self test')
libs=subprocess.check_output(['/usr/bin/otool','-L',str(B)],text=True)
check('CoreBluetooth' not in libs,'CoreBluetooth framework must not be linked')
for args in [['--transport','gatt'],['--transport','passive'],['--relay','TC-02'],['--credentials','missing.json']]:
 r=subprocess.run([str(B),*args],text=True,capture_output=True);check(r.returncode!=0 and 'Unsupported USB-only option' in r.stderr,'reject unsupported BLE controls before any access')
for args in [['--device','TC-02'],['--device','TC-00-0000000000000000'],['--chrony-only'],['--chrony-sock','/tmp/test.sock'],['--chrony-command-socket','relative.sock'],['--chronyc','relative'],['--chrony-command-socket','/tmp/test.sock']]:
 r=subprocess.run([str(B),*args],text=True,capture_output=True);check(r.returncode!=0,'incomplete or malformed chrony options rejected before USB access')
with tempfile.TemporaryDirectory(dir=H/'.build',prefix='usb-cli-') as temporary:
 p=Path(temporary);capture=p/'capture.jsonl';out=p/'report'
 rows=[{'type':'usb_start','tick_ns':'1000000000'}, {'type':'usb_status','tick_ns':'2000000000','host_wall_ns':'1790000000000000000','wall_sample_before_ns':'2000000000','wall_sample_after_ns':'2000000100','shadow_ns':None,'reported_device_utc_ns':'1790000000000000000','scope':'TC04','transport':'USB','source_snapshot':{'root':'0000000000000001','source_epoch':7,'map_generation':19}}, {'type':'usb_end','tick_ns':'3000000000'}]
 bounds={'n':1,'latest_low_ns':'-42000000','latest_high_ns':'-39000000','rms_lower_ns':39000000,'rms_upper_ns':42000000,'midpoint_jitter_diagnostic_ns':0}
 rows[1]['periodic_utc_offset_intervals']=bounds;rows[2]['periodic_utc_offset_intervals']=bounds
 rows[1]['chrony_user_utc_ns']='1790000000025000000'
 rows[1]['chrony']={'enabled':True,'fresh':True,'tracking':{'refid':'424C4500','stratum':1,'ntp_minus_system_s':.025,'rms_offset_s':.001,'root_delay_s':.2,'root_dispersion_s':.05}}
 rows.insert(1,{'type':'chrony_tracking','tick_ns':'1900000000','raw_csv':'retained fixture','tracking':{'refid':'424C4500'}})
 capture.write_text(''.join(json.dumps(r)+'\n' for r in rows))
 r=subprocess.run([str(B),'--report',str(capture),'--report-out',str(out)],text=True,capture_output=True);check(r.returncode==0,r.stderr)
 summary=json.loads((out/'summary.json').read_text());check(summary['transport']=='USB' and summary['accuracy_qualified'] is False,'report cannot claim precision')
 check(summary['comparison']['valid_status_samples']==0,'captured UTC cannot become a served accuracy observation')
 check(summary['chrony_user_clock']['comparison']['valid_status_samples']==1,'chrony comparison remains separate from precision shadow')
 check('1790000000025000000' in (out/'chrony-user-clock.csv').read_text() and 'retained fixture' in (out/'chrony-events.jsonl').read_text(),'chrony UTC, correction and raw tracking survive export')
 check('1790000000000000000,0000000000000001,7,19' in (out/'samples.csv').read_text(),'captured UTC and source fields survive export')
 check('served BLE source' not in (out/'report.html').read_text(),'USB report cannot claim a BLE source')
 check(summary['periodic_utc_offset_intervals']['rms_upper_ns']==42000000,'request interval bounds survive summary export')
 check('UTC RMS bounds: 39.000 ms' in (out/'report.html').read_text() and 'offset intervals, not served-clock' in (out/'report.html').read_text(),'USB interval chart and qualified semantics')
print(f'PASS {checks} USB-only product checks')

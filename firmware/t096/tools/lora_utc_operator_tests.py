#!/usr/bin/env python3
"""Offline single-mutation operator checks; no ports. MIT."""
import copy
import io
import json
from types import SimpleNamespace
import unittest
import subprocess
import tempfile
from pathlib import Path
from unittest.mock import patch
from lora_utc import Console, Frames, main, operate, private_destination, require_closed
class Checks(unittest.TestCase):
    def query(self,answers):
        self.calls=[]
        def call(op):
            self.calls.append(op);answer=answers.pop(0)
            if isinstance(answer,Exception):raise answer
            return copy.deepcopy(answer)
        return call
    def test_query_is_read_only(self):
        q=self.query([dict(supported=True,result=0,floor='000000000000000a')]);operate(q);self.assertEqual(self.calls,['lora-utc'])
    def test_floor_plus_one_override_and_no_poll_rearm(self):
        base=dict(supported=True,result=0,floor='000000000000000a',token='000000000000000b',state=1)
        q=self.query([base,base,dict(base,state=2),dict(base,state=3)])
        with patch('lora_utc.time.sleep'):last=operate(q,request=True,delay_ms=10000,allow_over_cap=True)
        self.assertEqual(last['state'],3);self.assertEqual(self.calls,['lora-utc','lora-utc 000000000000000b 80002710','lora-utc','lora-utc'])
    def test_lost_response_never_resubmitted(self):
        q=self.query([dict(supported=True,result=0,floor='0'),TimeoutError('lost')])
        with self.assertRaises(TimeoutError):operate(q,request=True)
        self.assertEqual(len(self.calls),2)
    def test_floor_exhaustion_old_token_and_unsupported(self):
        for floor,token in [('ffffffffffffffff',None),('000000000000000a',9)]:
            q=self.query([dict(supported=True,result=0,floor=floor)])
            with self.assertRaises(ValueError):operate(q,request=True,token=token)
            self.assertEqual(self.calls,['lora-utc'])
        q=self.query([dict(supported=False,result=-95)])
        with self.assertRaises(ValueError):operate(q)
    def test_refusal_replacement_and_expiry(self):
        base=dict(supported=True,result=0,floor='0',token='0000000000000001',state=1)
        for second in (dict(base,result=-16),dict(base,token='0000000000000002')):
            q=self.query([base,second])
            with self.assertRaises(ValueError):operate(q,request=True)
        q=self.query([base,dict(base,state=4)]);self.assertEqual(operate(q,request=True)['state'],4);self.assertEqual(len(self.calls),2)
    def test_failed_close_cannot_report_clean_run(self):
        for state in (dict(closed=False,close_errors=['io']),dict(closed=False,exclusive_release_errors=['io'])):
            with self.assertRaises(OSError):require_closed(state)
        require_closed(dict(closed=True,close_errors=[],exclusive_release_errors=[]))
    def test_raw_destination_must_be_ignored_artifacts(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder).resolve();subprocess.run(['git','init','-q',str(root)],check=True)
            (root/'.gitignore').write_text('/artifacts/\n')
            self.assertEqual(private_destination(root/'artifacts'/'private-run'),root/'artifacts'/'private-run')
            for p in (root/'public'/'raw',root.parent/'outside'):
                with self.assertRaises(ValueError):private_destination(p)
            (root/'.gitignore').write_text('')
            with self.assertRaises(ValueError):private_destination(root/'artifacts'/'not-ignored')
    def test_actual_teardown_retains_exclusivity_error(self):
        with tempfile.TemporaryDirectory() as folder:
            c=object.__new__(Console);c.fd=17;c.out=Path(folder);c.lock=io.StringIO();c.raw=io.BytesIO();c.journal=io.StringIO();c.state={};c.buffer=b'';c.frames=Frames()
            with patch('lora_utc.close_serial',return_value=(['release failed'],[])) as close:c.__exit__()
            close.assert_called_once_with(17)
            self.assertFalse(c.state['closed']);self.assertEqual(c.state['exclusive_release_errors'],['release failed'])
            with self.assertRaises(OSError):require_closed(c.state)
            self.assertEqual(json.loads((c.out/'closure.json').read_text()),c.state)
    def test_expired_cli_returns_nonzero_with_receipt(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);config=root/'config.json';config.write_text(json.dumps(dict(devices={'TEST':dict(usb_serial='fake',silicon_id='1'*16,source='2'*64,commit='3'*40)})))
            fake=SimpleNamespace(state=dict(closed=True),query=None)
            class Bound:
                def __enter__(self):return fake
                def __exit__(self,*a):pass
            argv=['lora_utc.py','--config',str(config),'--unit','TEST','--out',str(root/'run'),'--request','--expected-boot','4'*16]
            with patch('sys.argv',argv),patch('lora_utc.private_destination',return_value=root/'run'),patch('lora_utc.Console',return_value=Bound()),patch('lora_utc.operate',return_value=dict(state=4)),patch('builtins.print'),patch('lora_utc.os.umask'):
                self.assertEqual(main(),2)
            receipt=json.loads((root/'run'/'result.json').read_text());self.assertEqual(receipt['status'],'JOB_EXPIRED');self.assertEqual(receipt['job']['state'],4);self.assertTrue(receipt['closure']['closed'])
if __name__=='__main__':unittest.main()

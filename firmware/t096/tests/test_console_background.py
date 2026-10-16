"""MIT. Copyright (c) 2026 Jonathan Yantis. Supervisor without hardware."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

tools=Path(__file__).resolve().parents[1]/'tools'
sys.path.insert(0,str(tools))
spec=importlib.util.spec_from_file_location('background',tools/'console_background.py')
m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)


class SupervisorTest(unittest.TestCase):
    def run_case(self,closed):
        root=Path(__file__).resolve().parents[3]/'artifacts'
        with tempfile.TemporaryDirectory(dir=root) as temp:
            folder=Path(temp);config=folder/'config.json';out=folder/'run'
            config.write_text(json.dumps({'devices':{'CLOCK':dict(silicon_id='1'*16,source='2'*64,commit='3'*40,usb_serial='synthetic')}}))
            clock=[0];calls=[]
            def collect(unit,device,path,seconds,rotate,stop):
                calls.append(unit);clock[0]+=60
                return dict(status='COMPLETE',closed=closed,boot='1'*16)
            args=['background','--config',str(config),'--unit','CLOCK','--out',str(out),'--duration','60','--segment','15']
            with patch.object(sys,'argv',args),patch.object(m,'capture',collect),patch.object(m.time,'monotonic',lambda:clock[0]),patch.object(m.signal,'signal'):
                result=m.main()
            self.assertEqual(calls,['CLOCK'])
            final=json.loads((out/'finished.json').read_text())
            self.assertEqual(result,0 if closed else 1)
            self.assertEqual(final['status'],'COMPLETE' if closed else 'INCOMPLETE')
            latest=json.loads((out/'CLOCK-latest.json').read_text())
            self.assertEqual(latest['closed'],closed)

    def test_finite_completion(self):self.run_case(True)
    def test_unknown_closure_refuses_new_segment(self):self.run_case(False)

    def test_later_unit_failure_stops_earlier_live_unit(self):
        root=Path(__file__).resolve().parents[3]/'artifacts'
        with tempfile.TemporaryDirectory(dir=root) as temp:
            folder=Path(temp);config=folder/'config.json';out=folder/'run'
            c=dict(silicon_id='1'*16,source='2'*64,commit='3'*40,usb_serial='A')
            config.write_text(json.dumps({'devices':{'A':c,'B':{**c,'usb_serial':'B'}}}))
            clock=[0];started=threading.Event();stopped=[]
            def collect(unit,device,path,seconds,rotate,stop):
                if unit=='A':
                    started.set();stopped.append(stop.wait(2));clock[0]=61
                    return dict(status='STOPPED',closed=True)
                self.assertTrue(started.wait(2))
                return dict(status='INCOMPLETE',closed=False)
            args=['background','--config',str(config),'--unit','A','--unit','B','--out',str(out),'--duration','60','--segment','15']
            with patch.object(sys,'argv',args),patch.object(m,'capture',collect),patch.object(m.time,'monotonic',lambda:clock[0]),patch.object(m.signal,'signal'):
                self.assertEqual(m.main(),1)
            self.assertEqual(stopped,[True])


if __name__=='__main__':unittest.main()

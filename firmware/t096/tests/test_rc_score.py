"""Phase-cycle duty regression: no floating threshold tolerances."""
import sys,unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'sim'))
from rc_score import client_cycles,owners,scoring_failed

def fixture(start=10500000000,period=1000000000,active=450000000):
 events=[];radio=[]
 for i in range(3):
  lo=start+i*period;hi=lo+active
  events.extend([f'RC_WINDOW active=1 truth_ns={lo}',f'RC_WINDOW active=0 truth_ns={hi}']);radio.append((lo,hi))
 return '\n'.join(events),radio
class Cycles(unittest.TestCase):
 def test_phase500(self):
  log,radio=fixture();r=client_cycles(log,radio,0)
  self.assertEqual([x['quiet_ns'] for x in r],[550000000]*2);self.assertTrue(all(x['passed'] for x in r))
 def test_drift_crosses_calendar(self):
  log,radio=fixture(start=19999999900000,period=1000050003,active=450022502)
  r=client_cycles(log,radio,-50);self.assertEqual(len(r),2);self.assertTrue(all(x['passed'] for x in r))
 def test_actual499ms_fails(self):
  log,radio=fixture(active=501000000);self.assertTrue(all(not x['passed'] for x in client_cycles(log,radio,0)))
 def test_owner_inside_quiet_fails(self):
  log,radio=fixture();radio.append((10950001000,10950002000))
  r=client_cycles(log,radio,0);self.assertFalse(r[0]['passed']);self.assertEqual(r[0]['declared_quiet_intrusion_ns'],1000);self.assertTrue(r[1]['passed'])
class Failures(unittest.TestCase):
 def test_scoring_exception_clears_prior_controller_pass(self):
  result={'passed':True,'captures':123}
  try:owners('RC_WINDOW active=1 truth_ns=10000000000','RC_WINDOW',True)
  except ValueError as error:scoring_failed(result,error)
  self.assertFalse(result['passed']);self.assertEqual(result['captures'],123)
  self.assertIn('unclosed active owner',result['error'])
 def test_terminal_fault_closes_only_partial_window(self):
  log='RC_WINDOW active=1 truth_ns=10000000000\nRC_CLOSE stage=1 truth_ns=10100000000\nRC_DONE closed=1'
  self.assertEqual(owners(log,'RC_WINDOW',True),[(10000000000,10100000000)])
  self.assertEqual(client_cycles(log,[(10000000000,10100000000)],0),[])
if __name__=='__main__':unittest.main()

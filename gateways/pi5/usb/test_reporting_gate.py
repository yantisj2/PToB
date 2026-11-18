"""Synthetic reporting-policy checks. Copyright 2026 Jonathan Yantis. MIT."""
import copy
import unittest

from gateways.pi5.usb import test_shadow as synthetic
from gateways.pi5.usb.analyse import analyse
from gateways.pi5.usb.contract import decode, scope
from gateways.pi5.usb.reporting_gate import ReportingGate, evaluate

wire=synthetic.wire;change=synthetic.change;SILICON=synthetic.SILICON


class ReportingGateTests(unittest.TestCase):
    def fixture(self,n=120):
        # Already-decoded source/association states exercise only presentation
        # readiness; they do not claim independent event-association truth.
        samples=[];rows=[];origin=8*10**15
        for i in range(n):
            at=origin+i*125_000_000;d=decode(wire(i),SILICON)
            sample=dict(index=i,source=dict(before=dict(raw_before_ns=at),after=dict(raw_after_ns=at+1000)))
            row=dict(index=i,source_record=d,source=dict(state='WARMUP' if i<64 else 'CAUSAL_RELATIVE_DIAGNOSTIC',
                scope=list(scope(d)),original_deadline_raw_ns=at+10_000_000),
                native_realtime_pair_shadow=dict(source_measurement_state='ADMITTED',
                    source_utc_minus_system_ns=25600+i%3,provisional_source_utc_minus_system_ns=i%3))
            if i%8==0:self.marker(sample,row,at)
            samples.append(sample);rows.append(row)
        return dict(device=dict(silicon=SILICON),samples=samples),dict(source=dict(minimum_training_samples=64),source_rows=rows)

    def marker(self,sample,row,at):
        sample['marker']=dict(in_after=dict(raw_after_ns=at-100))
        row['marker_record']=dict(row['source_record']);row['marker_identity']=dict(state='INTEGER_ORIGIN_CONSTRAINED')

    def test_initial_warmup_exact_settle_and_null_values(self):
        raw,a=self.fixture();pristine=copy.deepcopy((raw,a));r=evaluate(raw,a)
        self.assertEqual((raw,a),pristine)
        self.assertEqual(r['counts'],dict(SOURCE_WARMUP=64,RECOVERY_SETTLING=8,REPORT_READY=48))
        self.assertTrue(all(x['report_native_fixed_ns'] is None for x in r['rows'][:72]))
        self.assertTrue(r['rows'][72]['report_ready']);self.assertEqual(r['rows'][72]['consecutive_ready_observations'],9)
        self.assertEqual(r['cohorts'][0]['populations']['raw']['n'],120)
        self.assertEqual(r['cohorts'][0]['populations']['admitted']['n'],120)
        self.assertEqual(r['cohorts'][0]['populations']['ready']['n'],48)
        self.assertEqual(sum(x['n'] for x in r['runs']),120)
        self.assertFalse(r['qualification_start_allowed']);self.assertFalse(r['parity'])

    def test_future_changes_cannot_change_prefix_or_backfill(self):
        raw,a=self.fixture();prefix=evaluate(dict(device=raw['device'],samples=copy.deepcopy(raw['samples'][:90])),
            dict(source=a['source'],source_rows=copy.deepcopy(a['source_rows'][:90])))
        for row in a['source_rows'][90:]:row['source_record']['valid']=False
        full=evaluate(raw,a);self.assertEqual(prefix['rows'],full['rows'][:90])
        self.assertEqual(full['rows'][71]['report_native_fixed_ns'],None)

    def test_scope_changes_rearm_without_resetting_on_map_generation(self):
        raw,a=self.fixture();r=evaluate(raw,a)
        self.assertTrue(all(x['report_ready'] for x in r['rows'][72:]))
        self.assertEqual(sum('SOURCE_SCOPE_CHANGE' in x['reset_reasons'] for x in r['rows']),1)
        for field in ('boot','usb_epoch','capture_epoch','root','root_boot','source_epoch','path_scope'):
            raw,a=self.fixture()
            for i,row in enumerate(a['source_rows'][90:],90):
                old=row['source_record'][field];row['source_record'][field]=old+1 if type(old) is int else old+'changed'
                row['source']['scope']=list(scope(row['source_record']))
                if 'marker' in raw['samples'][i]:self.marker(raw['samples'][i],row,raw['samples'][i]['source']['before']['raw_before_ns'])
            r=evaluate(raw,a)
            with self.subTest(field=field):
                self.assertFalse(r['rows'][90]['report_ready']);self.assertIn('SOURCE_SCOPE_CHANGE',r['rows'][90]['reset_reasons'])
                self.assertTrue(any(x['report_ready'] for x in r['rows'][98:]))

    def test_outage_expiry_missing_and_clock_gap_rearm(self):
        for failure in ('outage','expiry','missing-analysis','missing-phase','gap','clock-boundary'):
            raw,a=self.fixture()
            if failure=='outage':a['source_rows'][90]['source_record']['valid']=False
            elif failure=='expiry':a['source_rows'][90]['source']['original_deadline_raw_ns']=raw['samples'][90]['source']['after']['raw_after_ns']
            elif failure=='missing-analysis':a['source_rows'].pop(90)
            elif failure=='missing-phase':a['source_rows'][90]['native_realtime_pair_shadow']={}
            elif failure=='clock-boundary':a['source_rows'][90]['host_boundary_reset']=True
            else:
                for s in raw['samples'][90:]:
                    s['source']['before']['raw_before_ns']+=1_000_000_000;s['source']['after']['raw_after_ns']+=1_000_000_000
                    if 'marker' in s:s['marker']['in_after']['raw_after_ns']+=1_000_000_000
                for row in a['source_rows'][90:]:row['source']['original_deadline_raw_ns']+=1_000_000_000
            r=evaluate(raw,a)
            with self.subTest(failure=failure):
                self.assertTrue(r['rows'][89]['report_ready']);self.assertFalse(r['rows'][90]['report_ready'])
                self.assertTrue(all(x['report_native_raw_ns'] is None for x in r['rows'][90:98]))
                self.assertEqual(len(r['rows']),120);self.assertEqual(sum(r['counts'].values()),120)

    def test_marker_unavailable_stale_future_and_physical_scope_are_fenced(self):
        for failure in ('unavailable','stale','future','scope'):
            raw,a=self.fixture()
            if failure=='stale':
                for s,row in zip(raw['samples'][80:],a['source_rows'][80:]):
                    s.pop('marker',None);row.pop('marker_record',None);row.pop('marker_identity',None)
                index=85
            else:
                index=88
                if failure=='unavailable':a['source_rows'][index]['marker_record']['valid']=False
                elif failure=='future':raw['samples'][index]['marker']['in_after']['raw_after_ns']+=10_000
                else:a['source_rows'][index]['marker_record']['usb_epoch']+=1
            r=evaluate(raw,a)
            with self.subTest(failure=failure):self.assertFalse(r['rows'][index]['report_ready'])

    def test_no_phase_filter_rezero_or_invented_disabled_correction(self):
        raw,a=self.fixture();a['source_rows'][100]['native_realtime_pair_shadow'].update(
            source_utc_minus_system_ns=-974400,provisional_source_utc_minus_system_ns=-1000000)
        r=evaluate(raw,a);self.assertTrue(r['rows'][100]['report_ready'])
        self.assertEqual(r['rows'][100]['report_native_fixed_ns'],-1000000)
        self.assertEqual(r['cohorts'][0]['populations']['ready']['fixed']['max_abs_ns'],1000000)
        for row in a['source_rows']:row['native_realtime_pair_shadow']['provisional_source_utc_minus_system_ns']=None
        r=evaluate(raw,a);self.assertTrue(r['rows'][100]['report_ready'])
        self.assertEqual(r['rows'][100]['report_native_raw_ns'],-974400);self.assertIsNone(r['rows'][100]['report_native_fixed_ns'])

    def test_repeated_coherent_interiors_remain_raw_tails_and_rearm_reporting(self):
        raw=synthetic.RobustHostTests().raw_fixture(n=300);raw['device'].update(driver='xhci-hcd',controller_compatible='snps,dwc3')
        raw['realtime_bracket_method']='native-pair'
        for i,sample in enumerate(raw['samples']):
            packet=wire(i)
            if i in (140,141,240,241):
                packet=change(packet,40,2,(int.from_bytes(packet[40:42],'little')+1)%2048)
                packet=change(packet,56,8,int.from_bytes(packet[48:56],'little')+15980+i%2)
            sample['source']['wire_hex']=packet.hex();c=sample['source']['before']
            c.update(realtime_bracket_method='native-pair',realtime_raw_before_ns=c['raw_before_ns']+70,realtime_raw_after_ns=c['raw_before_ns']+90)
        a=analyse(raw)
        for sample,row in zip(raw['samples'],a['source_rows']):
            if sample['index']%8==0:self.marker(sample,row,sample['source']['before']['raw_before_ns'])
        pristine=copy.deepcopy((raw,a));r=evaluate(raw,a);self.assertEqual((raw,a),pristine)
        self.assertTrue(r['rows'][139]['report_ready']);self.assertTrue(r['rows'][239]['report_ready'])
        for i in (141,241):
            self.assertEqual(a['source_rows'][i]['capture_continuity']['state'],'COHERENT')
            self.assertEqual(r['rows'][i]['historical_source_state'],'WARMUP')
            self.assertEqual(r['rows'][i]['historical_host_phase_state'],'ADMITTED')
            self.assertGreater(abs(r['rows'][i]['diagnostic_native_fixed_ns']),900000)
            self.assertIsNone(r['rows'][i]['report_native_fixed_ns'])
        self.assertEqual(a['source']['counts']['COUNTER_FRAME_DISCONTINUITY'],4)
        self.assertGreater(r['cohorts'][0]['populations']['admitted']['fixed']['max_abs_ns'],900000)

    def test_policy_and_duplicate_or_unknown_context_inputs_fail_closed(self):
        with self.assertRaises(ValueError):ReportingGate(dict(stable_ns=0))
        raw,a=self.fixture();a['source']['minimum_training_samples']=1
        with self.assertRaises(ValueError):evaluate(raw,a)
        raw,a=self.fixture();a['source_rows'].append(a['source_rows'][0])
        with self.assertRaises(ValueError):evaluate(raw,a)
        raw,a=self.fixture();a['source_rows'][90]['source_record']['silicon']='0000000000000009'
        self.assertEqual(evaluate(raw,a)['rows'][90]['reason'],'SOURCE_IDENTITY_MISMATCH')
        raw,a=self.fixture();a['source_rows'][90]['source_record']['contract']='UTE1'
        self.assertEqual(evaluate(raw,a)['rows'][90]['reason'],'SOURCE_CONTRACT_INVALID')


if __name__=='__main__':unittest.main()

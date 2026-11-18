"""Public synthetic checks; no USB enumeration, SSH or clock changes.

Copyright (c) 2026 Jonathan Yantis. MIT.
"""
import copy
import ctypes
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

from gateways.pi5.usb.contract import decode,permission_deadline,project,scope,silicon_for_serial
from gateways.pi5.usb.model import CausalAffine,HostSeries,SourceSeries,Unwrapper,coarse_interval,statistics,spec_frame_ordinal,MarkerIdentity,CaptureContinuity
from gateways.pi5.usb import live
from gateways.pi5.usb.interval import BoundedRate,AffineBrackets
from gateways.pi5.usb.analyse import settlement_ready,current_observation,analyse
from gateways.pi5.usb.calibration import DEFAULT as DEFAULT_CALIBRATION,validate as validate_calibration,metadata as calibration_metadata,corrected_utc,corrected_phase
from gateways.pi5.usb.host_filter import RobustHost
from gateways.pi5.usb.filter_recovery import scenario
from gateways.pi5.usb.local_diagnostic import margins as local_margins
from gateways.pi5.usb.host_probe_plan import validate as validate_probe_plan
from gateways.pi5.usb.clock_pair import realtime_interval,snapshot_summary
from gateways.pi5.usb.clock_pair_filters import evaluate as evaluate_native_filters
from gateways.pi5.usb.clock_pair_filters import markdown as native_filter_markdown
from gateways.pi5.usb.interval_windows import score as score_affine_windows,evaluate as evaluate_affine_windows
from gateways.pi5.usb.origin_bounds import snapshot_interval,OriginIntersection,evaluate as evaluate_origin_bounds,PROFILES as ORIGIN_PROFILES

SILICON='0000000000000002'


def wire(i=0,kind=1,ppb=0,phase=0,magic=b'UTS1'):
    b=bytearray(256)
    def put(o,n,v,signed=False):b[o:o+n]=int(v).to_bytes(n,'little',signed=signed)
    b[:4]=magic;put(4,2,1);put(6,2,256);put(8,8,2);put(16,8,3)
    for o,v in [(24,1),(28,1),(32,i+1),(36,1)]:put(o,4,v)
    put(40,2,(2040+32*i)%2048);put(42,2,1)
    tick=16_000_000+i*512_000;current=tick+(100 if kind==1 else 0)
    put(48,8,tick);put(56,8,current);b[72]=kind;put(80,8,4);put(88,8,5)
    put(96,4,1);put(100,4,i+1);put(104,8,tick+8_000_000);put(112,8,90_000_000_000)
    put(120,8,16_000_000);put(128,8,1_000_000_000);put(136,8,16_000_000)
    put(144,8,16_000_000);put(152,8,80_000_000_000)
    put(160,4,ppb,True);put(164,4,phase,True)
    for o in (172,176,180):put(o,4,0xffffffff)
    put(184,8,1_790_000_000_000_000_000,True);put(192,8,1);b[200:216]=bytes(range(16))
    nominal=(tick-16_000_000)*125//2;elapsed=nominal*1_000_000_000//(1_000_000_000+ppb)
    product=min(elapsed,1_000_000_000)*phase;slew=(abs(product)//1_000_000_000)*(-1 if product<0 else 1)
    put(64,8,1_790_000_001_000_000_000+elapsed+slew)
    if magic==b'UTE1':put(36,4,31);put(232,8,16_000_000);put(240,8,80_000_000_000);put(248,4,1)
    return bytes(b)


def change(b,o,n,value):
    b=bytearray(b);b[o:o+n]=value.to_bytes(n,'little');return bytes(b)


class ContractTests(unittest.TestCase):
    def test_valid_source_and_unknown_bounds(self):
        d=decode(wire(),SILICON)
        self.assertEqual(project(d,d['tick']),1_000_000_000)
        self.assertIsNone(d['bound_ns']);self.assertIsNone(d['rate_bound_ppb'])

    def test_exact_rate_phase_integer_map(self):
        for rate in (-300000,-1,0,1,300000):
            for phase in (-300000,-1,0,1,300000):
                for i in (1,15,80):
                    d=decode(wire(i,ppb=rate,phase=phase),SILICON)
                    self.assertEqual(project(d,d['tick'])+d['utc_offset_ns'],d['utc_ns'])

    def test_identity_serial(self):
        self.assertEqual(silicon_for_serial('Example-'+SILICON),SILICON)
        for bad in ('receiver-alias','0'*16,'x\n'+SILICON):
            with self.assertRaises(ValueError):silicon_for_serial(bad)
        with self.assertRaises(ValueError):decode(wire(),'0000000000000001')

    def test_strict_header_length_reserved(self):
        for bad in (wire()[:-1],change(wire(),4,2,2),change(wire(),44,1,1),change(wire(),73,1,1),change(wire(),232,1,1)):
            with self.assertRaises(ValueError):decode(bad,SILICON)

    def test_map_sample_mismatch(self):
        with self.assertRaises(ValueError):decode(change(wire(),64,8,1),SILICON)

    def test_invalid_authority_geometry(self):
        for o,n,v in ((80,8,0),(88,8,0),(96,4,0),(100,4,0),(104,8,1),(144,8,16_000_001),(112,8,1),(160,4,300001)):
            with self.subTest(offset=o),self.assertRaises(ValueError):decode(change(wire(),o,n,v),SILICON)

    def test_expiry_in_flight(self):
        d=decode(wire(),SILICON)
        self.assertGreater(permission_deadline(d,1_000_000_000,1_001_000_000),1_001_000_000)
        with self.assertRaises(ValueError):permission_deadline(d,0,500_000_000)

    def test_current_kind_counter_equal(self):
        decode(wire(kind=2),SILICON)
        with self.assertRaises(ValueError):decode(change(wire(),72,1,2),SILICON)

    def test_scope_fences_all_provenance(self):
        d=decode(wire(),SILICON)
        for key in ('silicon','boot','usb_epoch','capture_epoch','root','root_boot','source_epoch','path_scope'):
            other=dict(d);other[key]=str(d[key])+'x';self.assertNotEqual(scope(d),scope(other))

    def test_ute_presentation_cannot_be_source(self):
        d=decode(wire(magic=b'UTE1'),SILICON)
        self.assertFalse(d['measurement'])
        self.assertEqual(SourceSeries().observe(d,0,1000)['state'],'WITHHELD')
        with self.assertRaises(ValueError):decode(change(wire(magic=b'UTE1'),36,4,3),SILICON)

    def test_ute_retained_current_and_expired_flags_are_diagnostic_only(self):
        for flags in (6,14,22,30,31):
            packet=change(wire(magic=b'UTE1'),36,4,flags)
            d=decode(packet,SILICON)
            self.assertTrue(d['estimate_present']);self.assertTrue(d['counter_valid'])
            self.assertEqual(d['session_current'],bool(flags&8));self.assertEqual(d['root_envelope_current'],bool(flags&16))
            self.assertFalse(d['measurement'])
            self.assertEqual(SourceSeries().observe(d,0,1)['state'],'WITHHELD')
            with self.assertRaises(ValueError):permission_deadline(d,0,1)
        stale=change(change(wire(magic=b'UTE1'),36,4,6),240,8,16_000_050)
        self.assertFalse(decode(stale,SILICON)['session_current'])
        self.assertEqual(decode(change(stale,232,8,0),SILICON)['last_discipline_tick'],0)
        for bad in (change(wire(magic=b'UTE1'),240,8,16_000_050),
                    change(wire(magic=b'UTE1'),232,8,16_000_001),
                    change(wire(magic=b'UTE1'),56,8,16_016_000),
                    change(change(wire(magic=b'UTE1'),36,4,12),248,4,0)):
            with self.assertRaises(ValueError):decode(bad,SILICON)

    def test_ufs_ufa(self):
        for magic,size in ((b'UFS1',80),(b'UFA1',64)):
            b=bytearray(size);b[:4]=magic
            for o,n,v in ((4,2,1),(6,2,size),(8,8,2),(16,8,3),(24,4,1),(28,4,1),(32,4,1),(40,2,2047),(44,8,16000),(52,8,16100)):
                b[o:o+n]=v.to_bytes(n,'little')
            b[42 if size==80 else 36]=1
            if size==64:b[42]=17
            d=decode(bytes(b),SILICON);self.assertTrue(d['valid'])
            with self.assertRaises(ValueError):decode(change(bytes(b),40,2,2048),SILICON)

    def test_missing_sof_classification_cannot_install_map(self):
        b=change(change(change(wire(),36,4,0),64,8,0),220,4,7);b=bytearray(b)
        b[232:240]=b'USM1\x01\x00\x18\x00';b[248]=1
        d=decode(bytes(b),SILICON);self.assertFalse(d['valid'])
        self.assertEqual(SourceSeries().observe(d,0,1000)['state'],'WITHHELD')
        with self.assertRaises(ValueError):decode(change(bytes(b),248,4,0),SILICON)


class CalibrationTests(unittest.TestCase):
    def raw(self,packets=None,late=False):
        def clocks(t):return dict(raw_before_ns=t,raw_after_ns=t+1,monotonic_ns=t,boottime_ns=t,realtime_ns=1_790_000_000_000_000_000+t,tai_ns=1_790_000_037_000_000_000+t)
        samples=[]
        for i,packet in enumerate(packets or [wire()]):
            t=i*32_000_000;before=clocks(t);after=clocks(t+(500_000_000 if late else 1000))
            samples.append(dict(index=i,clocks=before,source=dict(before=before,after=after,wire_hex=packet.hex())))
        return dict(device=dict(silicon=SILICON,driver='xhci-hcd',controller_compatible='snps,dwc3,',speed_mbps='12',direct_root_port=True),
            samples=samples,status=[],pps=[],start=clocks(0),end=clocks(600_000_000),elapsed_monotonic_ns=600_000_000,
            process_cpu_ns=1000,errors=[],interface_released=True,rate=8,requested_seconds=1)

    def test_additive_sign_exact_integer_epoch_and_unchanged_SD(self):
        meta=calibration_metadata(self.raw());utc=1_790_000_000_000_000_001
        self.assertEqual(corrected_utc(utc,meta),1_789_999_999_999_974_401)
        self.assertAlmostEqual(corrected_phase(25590.433,meta),-9.567,places=9)
        a=[25000.,26000.,27000.];b=[corrected_phase(x,meta) for x in a]
        self.assertEqual(statistics(a)['sigma_ns'],statistics(b)['sigma_ns'])
        self.assertIsNone(meta['absolute_uncertainty_ns']);self.assertFalse(meta['chrony_eligible'])

    def test_frozen_config_rejects_refit_sign_units_precision_and_finite_bound(self):
        for field,value in [('correction_ns',25600),('correction_ns',-25600.),('automatic_refit',True),('absolute_uncertainty_ns',1),('operation','SUBTRACT_SECONDS')]:
            c=copy.deepcopy(DEFAULT_CALIBRATION);c[field]=value
            with self.assertRaises(ValueError):validate_calibration(c)

    def test_scope_method_and_explicit_disable(self):
        for method in ('seek-read','pread','native-pread'):
            raw=self.raw();raw['host_read_method']=method
            self.assertEqual(calibration_metadata(raw)['applied_correction_ns'],-25600)
        raw=self.raw();raw['device']['speed_mbps']='480'
        self.assertFalse(calibration_metadata(raw)['enabled'])
        self.assertIsNone(corrected_utc(10**18,calibration_metadata(raw)))
        self.assertFalse(calibration_metadata(self.raw(),None)['enabled'])

    def test_exact_private_controller_and_port_fences_do_not_rezero(self):
        config=copy.deepcopy(DEFAULT_CALIBRATION);s=config['scope']
        s.update(expected_silicon=SILICON,expected_controller='/sys/devices/example/xhci-hcd.1',expected_USB_path='3-1')
        raw=self.raw();raw['device'].update(controller=s['expected_controller'],path='3-1')
        self.assertEqual(calibration_metadata(raw,config)['applied_correction_ns'],-25600)
        for key,value in (('controller','/sys/devices/example/xhci-hcd.0'),('path','3-2'),('silicon','0000000000000003')):
            changed=copy.deepcopy(raw);changed['device'][key]=value
            self.assertFalse(calibration_metadata(changed,config)['enabled'])
        for key in ('capture','cohort'):
            changed=copy.deepcopy(config);changed['basis'][key]='replacement'
            with self.assertRaises(ValueError):validate_calibration(changed)

    def test_raw_maps_and_association_rejection_preserved(self):
        bad=change(wire(1),40,2,(2040+32+1)%2048);raw=self.raw([wire(),bad]);saved=copy.deepcopy(raw)
        a=analyse(raw);plain=analyse(raw,calibration=None)
        self.assertEqual(raw,saved);self.assertEqual(a['source']['counts'],plain['source']['counts'])
        row=a['source_rows'][1];self.assertEqual(row['source']['reason'],'COUNTER_FRAME_DISCONTINUITY')
        self.assertFalse(row['provisional_shadow']['grants_time_authority'])
        self.assertEqual(row['provisional_shadow']['derived_utc_ns'],row['source_record']['utc_ns']-25600)

    def test_expiry_is_not_changed_by_correction(self):
        raw=self.raw(late=True);a=analyse(raw);plain=analyse(raw,calibration=None)
        self.assertEqual(a['source']['counts'],plain['source']['counts'])
        self.assertEqual(a['source_rows'][0]['source']['reason'],'original permission expired in flight')

    def test_provenance_cannot_be_replaced_and_rejected_tail_is_not_zeroed(self):
        c=copy.deepcopy(DEFAULT_CALIBRATION);c['basis']['capture_sha256']='f'*64
        with self.assertRaises(ValueError):validate_calibration(c)
        meta=calibration_metadata(self.raw());tail=corrected_phase(-974556.0,meta)
        self.assertEqual(tail,-1000156.0);self.assertEqual(statistics([tail])['above_5us'],1)


class LocalDiagnosticTests(unittest.TestCase):
    def test_retained_margin_arithmetic_matches_serialized_rate_phase_contract(self):
        for rate in (-300000,-1,0,1,300000):
            for phase in (-300000,-1,0,1,300000):
                for i in (1,15,80):
                    d=decode(change(wire(i,magic=b'UTE1',ppb=rate,phase=phase),36,4,6),SILICON)
                    r=local_margins(d)
                    self.assertEqual(r['projected_event_source_ns']+d['utc_offset_ns'],d['utc_ns'])
                    self.assertEqual(r['projected_current_source_ns'],project(d,d['current_tick']))
                    self.assertFalse(r['grants_time_authority'])

    def test_arithmetic_root_expiry_distinct_from_future_session(self):
        d=decode(change(wire(20,magic=b'UTE1'),36,4,14),SILICON)
        d['source_expires_ns']=1_200_000_000
        r=local_margins(d)
        self.assertLess(r['expires_minus_current_ns'],0)
        self.assertTrue(r['session_tick_deadline_still_future'])
        self.assertEqual(r['estimate_is_current_inference'],'TRUE_FROM_SESSION_CURRENT_FLAG')
        self.assertTrue(r['root_flag_and_arithmetic_agree_when_current_gate_true'])
        self.assertFalse(r['grants_time_authority']);self.assertFalse(r['chrony_eligible'])

    def test_noncurrent_gate_with_future_deadline_is_not_tick_expiry(self):
        d=decode(change(wire(20,magic=b'UTE1'),36,4,6),SILICON)
        d['until_tick']=d['current_tick']-1
        r=local_margins(d)
        self.assertTrue(r['arithmetic_root_interval_contains_current'])
        self.assertEqual(r['until_minus_current_ticks'],-1)
        self.assertEqual(r['estimate_is_current_inference'],'FALSE_FROM_NONCURRENT_FLAG_WITH_FUTURE_SESSION_UNTIL')
        self.assertIsNone(r['root_flag_and_arithmetic_agree_when_current_gate_true'])
        self.assertFalse(r['measurement'])
        with self.assertRaises(ValueError):permission_deadline(d,0,1)
        self.assertEqual(SourceSeries().observe(d,0,1)['state'],'WITHHELD')

    def test_retained_geometry_and_integer_precision_cannot_become_a_timestamp(self):
        with self.assertRaises(ValueError):local_margins(decode(wire(),SILICON))
        d=decode(change(wire(magic=b'UTE1'),36,4,6),SILICON)
        d.update(anchor_ns=8*10**18,utc_offset_ns=-7*10**18,utc_ns=10**18,source_issued_ns=8*10**18-1,source_expires_ns=8*10**18+10**9)
        r=local_margins(d)
        self.assertIsInstance(r['projected_current_source_ns'],int)
        self.assertEqual(r['projected_current_source_ns'],8*10**18+6250)
        d['utc_ns']+=1
        with self.assertRaises(ValueError):local_margins(d)
        d['utc_ns']-=1;d['map_ppb']=300001
        with self.assertRaises(ValueError):local_margins(d)
        # C rejects anchor+elapsed overflow before applying a negative slew,
        # even if that later slew could bring the final sum belowUINT64_MAX.
        d=decode(change(wire(magic=b'UTE1'),36,4,6),SILICON)
        d.update(anchor_ns=(1<<64)-1_000_000_000+100,tick=32_000_000,current_tick=32_000_100,map_phase_ns=-300000)
        with self.assertRaises(ValueError):local_margins(d)


class ProbePlanTests(unittest.TestCase):
    def fixture(self):
        import hashlib
        data=struct.pack('<I',0xd503201f)*10+struct.pack('<IIIII',0xb9400063,0xd50331bf,0x2a0303e0,0xca000000,0xb5000000)+struct.pack('<I',0xd503201f)*3
        digest=hashlib.sha256(data).hexdigest()
        snapshot=dict(mode='OFFLINE_DISASSEMBLY_OF_NORMAL_BOOT_FILE_NO_INSTRUMENTATION',kernel='synthetic-kernel',
            running_build_id_matches_image=True,symbol_banner_mapping_verified=True,live_kernel_memory_read=False,
            fragments=[dict(symbol='debugfs_print_regs32',hex=data.hex(),sha256=digest)])
        return snapshot,dict(kernel='synthetic-kernel',function_sha256=digest,before_offset=40,after_offset=60)

    def test_shared_loop_budget_is_retained_without_claiming_safe_instrumentation(self):
        s,m=self.fixture();r=validate_probe_plan(s,m,174)
        self.assertEqual(r['probe_hits_per_show'],16);self.assertEqual(r['projected_probe_hits_per_s'],2784)
        self.assertEqual(r['hard_budget_probe_hits_per_s'],16384)
        self.assertFalse(r['record_filters_remove_global_hit_cost']);self.assertFalse(r['live_authorized'])
        self.assertFalse(r['instruction_probing_safety_qualified']);self.assertIsNone(r['absolute_uncertainty_ns'])
        self.assertAlmostEqual(r['added_CPU_percent_scenarios'][1]['observed_rate_one_core_percent'],.2784)

    def test_stale_build_or_code_and_unaligned_offsets_are_refused(self):
        for kind in ('kernel','hash','offset','sequence','provenance'):
            s,m=self.fixture()
            if kind=='kernel':s['kernel']='new-build'
            if kind=='hash':s['fragments'][0]['hex']='00'+s['fragments'][0]['hex'][2:]
            if kind=='offset':m['before_offset']+=1
            if kind=='sequence':m['after_offset']-=4
            if kind=='provenance':s['live_kernel_memory_read']=True
            with self.assertRaises(ValueError):validate_probe_plan(s,m,174)

    def test_probe_cost_scenarios_reject_bad_rates_and_never_become_measurements(self):
        s,m=self.fixture()
        for rate in (-1,1025,float('nan'),float('inf'),True):
            with self.assertRaises(ValueError):validate_probe_plan(s,m,rate)
        r=validate_probe_plan(s,m,1024)
        self.assertTrue(all(c['physically_measured'] is False for c in r['added_CPU_percent_scenarios']))
        self.assertFalse(r['installs_probe']);self.assertFalse(r['chrony_eligible'])


class ModelTests(unittest.TestCase):
    def test_spec_frame_wrap_and_incompatible_numbering(self):
        self.assertEqual(spec_frame_ordinal(0,16383,16420),16384)
        self.assertEqual(spec_frame_ordinal(2047,16380,16400),16376)
        with self.assertRaises(ValueError):spec_frame_ordinal(100,16383,16420)
        with self.assertRaises(ValueError):spec_frame_ordinal(0,0,8000)

    def test_conditional_source_requires_real_host_reference(self):
        s=SourceSeries(host_reference=True)
        self.assertEqual(s.observe(decode(wire(),SILICON),0,1000)['reason'],'HOST_REFERENCE_UNAVAILABLE')
        self.assertFalse(s.summary()['host_phase_qualified'])

    def test_causal_current_sample_cannot_train_own_prediction(self):
        f=CausalAffine(minimum=4)
        for i in range(4):self.assertIsNone(f.observe(i,100+i*3))
        self.assertEqual(f.observe(4,112+1000),1000)

    def test_epoch_integer_origin(self):
        f=CausalAffine(minimum=4)
        for i in range(4):f.observe(10**18+i,8*10**18+3*i)
        self.assertEqual(f.observe(10**18+4,8*10**18+12+1),1)

    def test_frame_wrap_unique_gap_ambiguous(self):
        u=Unwrapper(2048,1_000_000)
        self.assertEqual(u.observe(2047,0),2047);self.assertEqual(u.observe(0,1_000_000),2048)
        with self.assertRaises(ValueError):u.observe(1,1_100_000_000)

    def test_frame_discontinuity_not_inferred_from_arrival(self):
        u=Unwrapper(16384,125000);u.observe(100,0)
        with self.assertRaises(ValueError):u.observe(300,1000000)

    def test_source_warmup_predicts_then_updates(self):
        s=SourceSeries()
        for i in range(100):s.observe(decode(wire(i),SILICON),i*32_000_000,i*32_000_000+1_000_000)
        self.assertEqual(s.counts['warmup'],64);self.assertEqual(s.counts['predicted'],36)
        self.assertLess(max(abs(v) for v in s.errors),.01)

    def test_replay_withdraws(self):
        s=SourceSeries();d=decode(wire(),SILICON);s.observe(d,0,1000)
        self.assertEqual(s.observe(d,1000,2000)['reason'],'SEQUENCE_REPLAY');self.assertIsNone(s.context)

    def test_generation_and_immutable_map_fences(self):
        s=SourceSeries();s.observe(decode(wire(),SILICON),0,1000)
        d=decode(wire(1),SILICON);d['map_generation']=1;d['anchor_ns']+=1;d['utc_ns']+=1
        self.assertEqual(s.observe(d,32_000_000,33_000_000)['reason'],'IMMUTABLE_MAP_MUTATED')

    def test_sequence_and_map_history_survive_withholding(self):
        s=SourceSeries();d=decode(wire(),SILICON);s.observe(d,0,1000);s.fence('GAP')
        self.assertEqual(s.observe(d,1000,2000)['reason'],'SEQUENCE_REPLAY')
        d=decode(wire(1),SILICON);d['map_generation']=1;d['anchor_ns']+=1;d['utc_ns']+=1
        self.assertEqual(s.observe(d,32_000_000,33_000_000)['reason'],'IMMUTABLE_MAP_MUTATED')

    def test_generation_rollback_survives_source_gap(self):
        s=SourceSeries();d=decode(wire(),SILICON);d['map_generation']=4;s.observe(d,0,1000);s.fence('GAP')
        self.assertEqual(s.observe(decode(wire(1),SILICON),32_000_000,33_000_000)['reason'],'COUNTER_OR_MAP_ROLLBACK')

    def test_same_map_cannot_refresh_original_deadline_after_gap(self):
        s=SourceSeries();s.observe(decode(wire(),SILICON),0,1000);s.fence('UNAVAILABLE')
        d=decode(wire(17),SILICON);d['map_generation']=1
        self.assertEqual(s.observe(d,544_000_000,545_000_000)['reason'],'original permission expired in flight')

    def test_reobserved_map_cannot_move_first_RAW_deadline_later(self):
        s=SourceSeries();first=s.observe(decode(wire(),SILICON),0,1000)
        d=decode(wire(1),SILICON);d['map_generation']=1
        self.assertEqual(s.observe(d,499_000_000,500_000_000)['reason'],'original permission expired in flight')
        self.assertLess(first['original_deadline_raw_ns'],499_000_000)

    def test_first_expired_in_flight_map_still_records_original_limit(self):
        s=SourceSeries()
        self.assertEqual(s.observe(decode(wire(),SILICON),0,500_000_000)['reason'],'original permission expired in flight')
        d=decode(wire(1),SILICON);d['map_generation']=1
        self.assertEqual(s.observe(d,501_000_000,502_000_000)['reason'],'original permission expired in flight')

    def test_scope_changes_restart_training(self):
        for field in ('boot','usb_epoch','capture_epoch','root','root_boot','source_epoch','path_scope'):
            s=SourceSeries();s.observe(decode(wire(),SILICON),0,1000)
            d=decode(wire(1),SILICON);d[field]=str(d[field])+'changed'
            self.assertEqual(s.observe(d,32_000_000,33_000_000)['state'],'WARMUP')
            self.assertEqual(len(s.fit.points),1)

    def test_gap_stays_gap(self):
        s=SourceSeries();s.observe(decode(wire(),SILICON),0,1000)
        self.assertEqual(s.observe(decode(wire(20),SILICON),640_000_000,641_000_000)['state'],'WITHHELD')

    def test_request_jitter_is_not_device_frame_time(self):
        s=SourceSeries()
        for i in range(100):
            at=i*32_000_000+(3_000_000 if i%8==0 else 0)
            self.assertNotEqual(s.observe(decode(wire(i),SILICON),at,at+1_000_000)['state'],'WITHHELD')
        self.assertEqual(s.counts['predicted'],36)

    def test_host_observation_gap_fences_even_coherent_device_counter(self):
        s=SourceSeries();s.observe(decode(wire(),SILICON),0,1000)
        self.assertEqual(s.observe(decode(wire(1),SILICON),501_000_000,502_000_000)['reason'],'HOST_OBSERVATION_GAP')

    def test_valid_map_late_frame_old_capture_is_withheld(self):
        # Stable scalar reads and age<16000 alone cannot establish frame/CC
        # association. A one-frame-new label with an old SOF tick is retained
        # as invalid even though its immutable source map agrees exactly.
        s=SourceSeries();s.observe(decode(wire(),SILICON),0,1000)
        b=change(wire(1),40,2,(2040+32+1)%2048)
        b=change(b,56,8,16_000_000+512_000+15990)
        d=decode(b,SILICON);self.assertTrue(d['valid'])
        self.assertEqual(s.observe(d,32_000_000,33_000_000)['reason'],'COUNTER_FRAME_DISCONTINUITY')
        self.assertIsNone(s.context)
        self.assertEqual(s.observe(decode(wire(2),SILICON),64_000_000,65_000_000)['state'],'WARMUP')

    def test_consecutive_shifted_labels_have_coherent_interior_after_fence(self):
        # Synthetic label-shift hypothesis, not hardware endpoint attribution.
        # Two shifted labels agree with each other; the reset source model can
        # accept that interior tuple as WARMUP without certifying its event.
        packets=[]
        for i in range(76):
            packet=wire(i)
            if i in (70,71):
                packet=change(packet,40,2,(int.from_bytes(packet[40:42],'little')+1)%2048)
                packet=change(packet,56,8,int.from_bytes(packet[48:56],'little')+15980+i-70)
            packets.append(decode(packet,SILICON))
        pristine=copy.deepcopy(packets);source=SourceSeries();capture=CaptureContinuity()
        rows=[source.observe(d,i*32_000_000,i*32_000_000+1000) for i,d in enumerate(packets)]
        transitions=[capture.observe(d,i) for i,d in enumerate(packets)]
        self.assertEqual(packets,pristine)
        self.assertEqual([r['state'] for r in rows[69:74]],
            ['CAUSAL_RELATIVE_DIAGNOSTIC','WITHHELD','WARMUP','WITHHELD','WARMUP'])
        self.assertEqual([r['state'] for r in transitions[70:73]],
            ['COUNTER_FRAME_MISMATCH','COHERENT','COUNTER_FRAME_MISMATCH'])
        self.assertIsNone(rows[71]['causal_error_ns'])
        self.assertEqual(rows[71]['host_clock_association'],'UNRESOLVED')
        self.assertFalse(source.summary()['chrony_eligible'])
        # Returning to the synthetic correct label is also withheld; do not
        # silently select an endpoint or remove either disagreeing transition.
        self.assertEqual(source.counts['COUNTER_FRAME_DISCONTINUITY'],2)
        self.assertEqual(capture.counts['counter_frame_mismatch'],2)
        prefix=SourceSeries()
        self.assertEqual([prefix.observe(d,i*32_000_000,i*32_000_000+1000)
            for i,d in enumerate(packets[:72])],rows[:72])

    def test_capture_diagnostic_retains_both_transitions_without_source_permission(self):
        c=CaptureContinuity();s=SourceSeries()
        for i in range(3):
            d=decode(wire(i),SILICON);d.update(valid=False,reason=1)
            if i==1:d['frame']=(d['frame']+1)%2048
            c.observe(d,i)
            self.assertEqual(s.observe(d,i*32_000_000,i*32_000_000+1000)['state'],'WITHHELD')
        self.assertEqual(c.counts['compared_transitions'],2)
        self.assertEqual(c.counts['counter_frame_mismatch'],2)
        self.assertEqual([r['previous_index'] for r in c.rows[1:]],[0,1])
        self.assertEqual(s.counts['accepted'],0)

    def test_capture_scope_and_gap_do_not_infer_missing_frames(self):
        c=CaptureContinuity();c.observe(decode(wire(),SILICON),0)
        self.assertEqual(c.observe(decode(wire(20),SILICON),20)['state'],'COUNTER_GAP_OR_ROLLBACK')
        d=decode(wire(21),SILICON);d['capture_epoch']=2
        self.assertEqual(c.observe(d,21)['state'],'SEED')
        d['frame']=65535
        self.assertEqual(c.observe(d,22)['state'],'NO_USABLE_CAPTURE')

    def test_transition_bounds_use_outer_bracket(self):
        h=HostSeries();h.observe(dict(mfindex=10,before_raw_ns=0,after_raw_ns=10))
        h.observe(dict(mfindex=11,before_raw_ns=124000,after_raw_ns=126000))
        self.assertEqual(h.transitions[0]['low_raw_ns'],0);self.assertEqual(h.transitions[0]['high_raw_ns'],126000)
        self.assertFalse(h.summary()['atomic_frame_timestamp'])

    def test_transition_skips_counted(self):
        h=HostSeries();h.observe(dict(mfindex=10,before_raw_ns=0,after_raw_ns=10))
        h.observe(dict(mfindex=15,before_raw_ns=625000,after_raw_ns=625010))
        self.assertEqual(h.counts['unobserved_microframe_transitions'],4);self.assertEqual(h.transitions,[])

    def test_coarse_interval_retains_whole_request(self):
        d=decode(wire(kind=2),SILICON);utc=d['utc_ns']
        a=dict(raw_before_ns=0,raw_after_ns=100,realtime_ns=utc-1_000_000)
        b=dict(raw_before_ns=1_999_900,raw_after_ns=2_000_000,realtime_ns=utc+1_000_000)
        r=coarse_interval(d,a,b);self.assertEqual(r['low_ns'],-1_000_100);self.assertEqual(r['high_ns'],1_000_100)

    def test_current_unavailable_is_not_decode_rejection(self):
        d=decode(wire(kind=2),SILICON);d.update(valid=False,reason=1)
        self.assertEqual(current_observation(d,{},{}),dict(state='UNAVAILABLE',reason=1))
        d['valid']=True;a=dict(raw_before_ns=0,raw_after_ns=1,realtime_ns=100)
        b=dict(raw_before_ns=1000,raw_after_ns=1001,realtime_ns=100_000_000)
        self.assertEqual(current_observation(d,a,b)['state'],'WITHHELD')

    def test_clock_step_rejects_interval(self):
        d=decode(wire(kind=2),SILICON);a=dict(raw_before_ns=0,raw_after_ns=1,realtime_ns=100)
        b=dict(raw_before_ns=1000,raw_after_ns=1001,realtime_ns=100_000_000)
        with self.assertRaises(ValueError):coarse_interval(d,a,b)

    def test_tails_strict_and_no_missing_zero(self):
        s=statistics([0,1000,-1001,2000,2001,5000,5001]);self.assertEqual(s['above_1us'],5)
        self.assertEqual(s['above_2us'],3);self.assertEqual(s['above_5us'],1);self.assertEqual(s['max_abs_ns'],5001)
        self.assertIsNone(statistics([])['rms_ns'])


class TransportTests(unittest.TestCase):
    def test_register_methods_read_fresh_content_and_pread_preserves_offset(self):
        import os,time
        for method in ('seek-read','pread'):
            with tempfile.TemporaryDirectory() as directory:
                path=Path(directory)/'register';path.write_bytes(b'MFINDEX = 0x00000001\n')
                reader=live.RuntimeReader.__new__(live.RuntimeReader)
                reader.method=method;reader.fd=os.open(path,os.O_RDONLY)
                reader.reads=reader.used=0;reader.budget_start=time.monotonic_ns();reader.native_temp=None
                try:
                    os.lseek(reader.fd,7,os.SEEK_SET)
                    self.assertEqual(reader.sample()['mfindex'],1)
                    if method=='pread':self.assertEqual(os.lseek(reader.fd,0,os.SEEK_CUR),7)
                    path.write_bytes(b'MFINDEX = 0x00000002\n')
                    self.assertEqual(reader.sample()['mfindex'],2)
                    reader.used=1024
                    with self.assertRaises(ValueError):reader.sample()
                finally:reader.close()

    def test_native_wrapper_budget_and_partial_error_brackets(self):
        import errno,time
        reader=live.RuntimeReader.__new__(live.RuntimeReader)
        reader.method='native-pread';reader.fd=12;reader.reads=reader.used=0;reader.budget_start=time.monotonic_ns()
        def failed(fd,limit,duration,rows,count):
            rows[0].before=100;rows[0].after=200;count._obj.value=1;ctypes.set_errno(errno.EPROTO);return -1
        reader.native_function=mock.Mock(side_effect=failed)
        with self.assertRaises(ValueError) as error:reader.native_reads(24)
        self.assertEqual(error.exception.native_partial_reads[0]['before_raw_ns'],100)
        self.assertFalse(error.exception.native_partial_reads[0]['valid'])
        self.assertEqual((reader.reads,reader.used),(1,1))
        reader.used=1024
        with self.assertRaises(ValueError):reader.native_reads(24)
        self.assertEqual(reader.native_function.call_count,1)

    def test_discovery_exact_identity_duplicate_and_own_claim(self):
        with tempfile.TemporaryDirectory() as name:
            base=Path(name);root=base/'sysfs';root.mkdir();device=base/'device';device.mkdir()
            controller=base/'platform/xhci-hcd.1';controller.mkdir(parents=True)
            usbroot=controller/'usb3';usbroot.mkdir();(root/'usb3').symlink_to(usbroot)
            (controller.parent/'of_node').mkdir();(controller.parent/'of_node/compatible').write_bytes(b'snps,dwc3\0')
            driver=base/'drivers/xhci-hcd';driver.mkdir(parents=True);(controller/'driver').symlink_to(driver)
            serial='Example-'+SILICON
            for key,value in dict(idVendor='2fe3',idProduct='0100',serial=serial,busnum='3',devnum='2',speed='12').items():
                (device/key).write_text(value)
            (root/'3-1').symlink_to(device);interface=root/'3-1:1.0';interface.mkdir()
            for key,value in dict(bInterfaceClass='ff',bInterfaceSubClass='54',bInterfaceProtocol='01',bInterfaceNumber='00').items():
                (interface/key).write_text(value)
            info,_,_=live.discover(serial,root=root);self.assertEqual(info['interface'],0);self.assertTrue(info['direct_root_port'])
            usbfs=base/'drivers/usbfs';usbfs.mkdir();(interface/'driver').symlink_to(usbfs)
            with self.assertRaises(ValueError):live.discover(serial,root=root)
            live.discover(serial,owned=True,root=root)
            (interface/'driver').unlink();(root/'3-2').symlink_to(device)
            with self.assertRaises(ValueError):live.discover(serial,root=root)

    def test_no_unallowlisted_or_out_request(self):
        for request in (0x54,0x55,0x57,0x09):
            with self.assertRaises(ValueError):live.request(-1,0,request,256)

    def test_vendor_in_only_and_whole_clock_bracket(self):
        def fake(fd,num,arg):
            ctl=ctypes.cast(arg,ctypes.POINTER(live.Control)).contents
            self.assertEqual(ctl.request_type,0xc1);self.assertEqual(ctl.index,3);self.assertEqual(ctl.value,0)
            self.assertEqual(ctl.timeout,1000);ctypes.memmove(ctl.data,b'X'*256,256);return 256
        with mock.patch.object(live,'ioctl',side_effect=fake),mock.patch.object(live,'clocks',side_effect=[{'raw_before_ns':1},{'raw_after_ns':10}]):
            r=live.request(1,3,0x53,256)
        self.assertEqual(r['length'],256);self.assertEqual(r['before']['raw_before_ns'],1);self.assertEqual(r['after']['raw_after_ns'],10)

    def test_local_estimate_is_explicit_in_only_and_cannot_enable_marker_out(self):
        def fake(fd,num,arg):
            c=ctypes.cast(arg,ctypes.POINTER(live.Control)).contents
            self.assertEqual((c.request_type,c.request,c.value,c.index,c.length),(0xc1,0x57,0,3,256))
            ctypes.memmove(c.data,wire(magic=b'UTE1'),256);return 256
        with mock.patch.object(live,'ioctl',side_effect=fake),mock.patch.object(live,'clocks',return_value={}):
            result=live.request(1,3,0x57,256,allow_local_estimate=True)
        self.assertEqual(result['length'],256)
        for number in (0x54,0x55,0x58):
            with self.assertRaises(ValueError):live.request(1,3,number,64,allow_local_estimate=True)
        with self.assertRaises(ValueError):live.request(1,3,0x57,256,allow_local_estimate='yes')

    def test_marker_exact_transient_handshake_only(self):
        calls=[]
        def fake(fd,num,arg):
            ctl=ctypes.cast(arg,ctypes.POINTER(live.Control)).contents
            calls.append((ctl.request_type,ctl.request,ctl.value,ctl.index,ctl.length))
            return ctl.length
        with mock.patch.object(live,'ioctl',side_effect=fake),mock.patch.object(live,'clocks',return_value={}):
            row=live.marker(1,3,17)
        self.assertEqual(calls,[(0x41,0x54,17,3,0),(0xc1,0x55,17,3,64)])
        self.assertEqual(row['length'],64)

    def test_failed_marker_does_not_fetch_or_retry(self):
        with mock.patch.object(live,'ioctl',side_effect=OSError(19,'disconnected')) as call,mock.patch.object(live,'clocks',return_value={}):
            row=live.marker(1,0,1)
        self.assertEqual(call.call_count,1);self.assertEqual(row['error']['errno'],19)
        for token in (0,-1,65536,True):
            with self.assertRaises(ValueError):live.marker(-1,0,token)

    def test_short_source_response_is_transport_failure(self):
        with mock.patch.object(live,'ioctl',return_value=0),mock.patch.object(live,'clocks',return_value={}):
            self.assertIn('error',live.request(1,0,0x53,256))

    def test_remote_payload_contains_strict_decoder_without_private_checkout(self):
        folder=Path(live.__file__).parent
        source=(folder/'contract.py').read_text()+'\n'+(folder/'live.py').read_text()
        namespace={'__name__':'__main__'};exec(compile(source,'public-payload','exec'),namespace)
        self.assertEqual(namespace['decode'](wire(),SILICON)['silicon'],SILICON)


class MarkerTests(unittest.TestCase):
    def fixture(self,index=0):
        # Host boundary frame100+i is at10ms+i*1ms. The device's marker
        # sees this same frame200us into it; OUT is bracketed over400us.
        f=100+index;t=10_000_000+index*1_000_000
        a=dict(contract='UFA1',silicon=SILICON,boot='0000000000000003',usb_epoch=1,capture_epoch=1,
               sequence=index+1,valid=True,token=index+1,frame=f,tick=16000*index,current_tick=16000*index+3200)
        source=dict(a,contract='UTS1',frame=f+1,tick=a['tick']+16000)
        ref=dict(ordinal=f*8-1,low_raw_ns=t-125000-10000,high_raw_ns=t-125000+10000)
        return a,index+1,t,t+400000,ref,f*8,f*8+8,source

    def test_marker_causal_origin_and_qualification_boundary(self):
        m=MarkerIdentity()
        for i in range(16):row=m.observe(*self.fixture(i))
        self.assertEqual(row['state'],'INTEGER_ORIGIN_CONSTRAINED');self.assertEqual(m.candidates,{0})
        self.assertTrue(m.summary()['integer_origin_constrained']);self.assertFalse(m.summary()['host_timestamp_qualified'])
        self.assertFalse(m.summary()['chrony_eligible'])

    def test_marker_token_replay_epoch_and_bracket_fences(self):
        for mutation,reason in [('token','MARKER_INVALID_OR_TOKEN_MISMATCH'),('epoch','MARKER_SOURCE_EPOCH_MISMATCH'),
                                ('bracket','MARKER_BRACKET_AMBIGUOUS'),('tick','MARKER_SOURCE_COUNTER_FRAME_MISMATCH')]:
            args=list(self.fixture());args[0]=dict(args[0]);args[-1]=dict(args[-1])
            if mutation=='token':args[1]+=1
            if mutation=='epoch':args[-1]['usb_epoch']+=1
            if mutation=='bracket':args[3]=args[2]-1
            if mutation=='tick':args[-1]['tick']+=9000
            self.assertEqual(MarkerIdentity().observe(*args)['reason'],reason)
        m=MarkerIdentity();m.observe(*self.fixture());self.assertEqual(m.observe(*self.fixture())['reason'],'MARKER_SEQUENCE_REPLAY')

    def test_changed_marker_epoch_discards_previous_origin(self):
        m=MarkerIdentity();m.observe(*self.fixture());args=list(self.fixture(1));args[0]['usb_epoch']=2;args[-1]['usb_epoch']=2
        m.observe(*args);self.assertEqual(m.accepted,1);self.assertEqual(len(m.segments),1)

    def test_unavailable_marker_fences_training_without_inventing_contradiction(self):
        m=MarkerIdentity()
        for i in range(16):m.observe(*self.fixture(i))
        args=list(self.fixture(16));args[0].update(valid=False,reason=2,frame=65535,tick=0)
        self.assertEqual(m.observe(*args)['state'],'UNAVAILABLE')
        self.assertFalse(m.summary()['integer_origin_constrained']);self.assertFalse(m.blocked)
        self.assertEqual(m.observe(*self.fixture(17))['state'],'COLLECTING')
        for i in range(18,33):m.observe(*self.fixture(i))
        self.assertTrue(m.summary()['integer_origin_constrained'])
        self.assertEqual(m.counts['MARKER_UNAVAILABLE'],1)


class RobustHostTests(unittest.TestCase):
    def raw_fixture(self,n=180):
        origin=8*10**15;samples=[]
        def clock(at,utc):return dict(raw_before_ns=at,raw_after_ns=at+100,realtime_ns=utc,monotonic_ns=at,boottime_ns=at)
        for i in range(n):
            packet=wire(i)
            if i>=100:
                packet=change(packet,64,8,int.from_bytes(packet[64:72],'little')+1_000_000)
                packet=change(packet,184,8,int.from_bytes(packet[184:192],'little')+1_000_000)
                packet=change(packet,96,4,2)
            if i==90:packet=change(packet,40,2,(int.from_bytes(packet[40:42],'little')+1)%2048)
            event=origin+i*32_000_000;ordinal=16320+i*256
            reads=[dict(mfindex=(ordinal+4)%16384,before_raw_ns=event+615000,after_raw_ns=event+616000),
                   dict(mfindex=(ordinal+5)%16384,before_raw_ns=event+634000,after_raw_ns=event+635000)]
            # Host clock continues through the source's1ms UTC label change.
            utc=1_790_000_001_000_000_000+i*32_000_000
            before=clock(event+750000,utc+750000-25600)
            samples.append(dict(index=i,clocks=before,host_before=reads,
                host_after=[dict(mfindex=(ordinal+8)%16384,before_raw_ns=event+1000000,after_raw_ns=event+1001000)],
                source=dict(before=before,after=clock(event+800000,utc+800000-25600),wire_hex=packet.hex())))
        return dict(device=dict(silicon=SILICON,direct_root_port=True,speed_mbps='12'),samples=samples,status=[],pps=[],
            start=clock(origin,1_790_000_001_000_000_000),end=clock(origin+n*32_000_000,1_790_000_001_000_000_000+n*32_000_000),
            elapsed_monotonic_ns=n*32_000_000,process_cpu_ns=1,errors=[],interface_released=True,rate=32)

    def train(self,m,n=100,period=125001,origin=8*10**18):
        for i in range(n):m.observe(i,origin+i*period-10000,origin+i*period+10000)
        return origin+n*period

    def test_current_outlier_is_forecast_scored_and_cannot_train_its_prediction(self):
        m=RobustHost();truth=self.train(m);before=m.predict(100)
        r=m.observe(100,truth+40000,truth+60000)
        self.assertAlmostEqual(r['forecast_error_ns'],50000,delta=.001)
        self.assertFalse(r['training_accepted']);self.assertEqual(m.predict(100),before)
        self.assertEqual(m.counts['forecast_scored'],37);self.assertEqual(m.counts['training_withheld'],1)
        self.assertAlmostEqual(m.observe(101,truth+125001-10000,truth+125001+10000)['forecast_error_ns'],0,delta=.001)

    def test_prefix_predictions_do_not_change_with_a_future_tail(self):
        first=RobustHost();second=RobustHost();self.train(first);self.train(second)
        prefix=copy.deepcopy(first.rows)
        for i in range(100,120):second.observe(i,8*10**18+i*125001+50000,8*10**18+i*125001+70000)
        self.assertEqual(prefix,second.rows[:100])

    def test_wide_bracket_rejection_retains_zero_innovation_in_all_scores(self):
        m=RobustHost();truth=self.train(m)
        r=m.observe(100,truth-60000,truth+60000)
        self.assertEqual(r['rejection'],'WIDE_COMPLETE_BRACKET');self.assertFalse(r['training_accepted'])
        self.assertAlmostEqual(r['forecast_error_ns'],0,delta=.001)
        self.assertEqual(r['bracket_width_ns'],120000)

    def test_constant_drift_is_forecast_at_large_integer_epoch(self):
        m=RobustHost();truth=self.train(m,period=125006)
        prediction=m.predict(100);self.assertIsInstance(prediction[0],int)
        self.assertAlmostEqual((truth-prediction[0])-prediction[1],0,delta=.001)

    def test_step_delay_and_all_initial_failures_are_preserved(self):
        m=RobustHost();self.train(m);step=50000;rows=[]
        for i in range(100,180):
            truth=8*10**18+i*125001+step;rows.append(m.observe(i,truth-10000,truth+10000))
        self.assertEqual([r['training_accepted'] for r in rows[:4]],[False]*4)
        self.assertTrue(rows[3]['training_reset_after_score'])
        self.assertTrue(all(r['forecast_error_ns'] is None for r in rows[4:68]))
        self.assertAlmostEqual(rows[68]['forecast_error_ns'],0,delta=.001)
        self.assertEqual(m.counts['FOUR_CONSECUTIVE_HOST_FAILURES'],1)
        self.assertEqual(len(m.rows),180)

    def test_gap_and_rollback_force_fresh_warmup(self):
        for x in (50,5000):
            m=RobustHost();self.train(m);truth=8*10**18+x*125001
            r=m.observe(x,truth-10000,truth+10000)
            self.assertIsNone(r['forecast_error_ns']);self.assertEqual(len(m.points),1)
            self.assertEqual(m.counts['HOST_ORDINAL_GAP_OR_ROLLBACK'],1)

    def test_no_source_or_mhat_phase_input_and_bad_parameters_refused(self):
        for args in ((0,128),(64,32),(True,128)):
            with self.assertRaises(ValueError):RobustHost(*args)
        m=RobustHost();self.train(m);prediction=m.predict(100)
        # A source phase step remains fully present in the derived difference;
        # no source/UTC value is supplied to, or learned by, this host model.
        origin,delta=prediction;utc=8*10**18+100*125001
        self.assertAlmostEqual(((utc+1_000_000-origin)-delta)-((utc-origin)-delta),1_000_000)
        self.assertEqual(m.predict(100),prediction)

    def test_analysis_keeps_admission_raw_and_millisecond_failure_with_filter(self):
        raw=self.raw_fixture();untouched=copy.deepcopy(raw)
        baseline=analyse(raw,calibration=None);filtered=analyse(raw,calibration=None,host_filter=True)
        self.assertEqual(raw,untouched)
        self.assertEqual(baseline['source'],filtered['source'])
        self.assertEqual(baseline['attempts'],filtered['attempts'])
        self.assertEqual(baseline['conditional_spec_shadow'],filtered['conditional_spec_shadow'])
        failure=filtered['source_rows'][90]
        self.assertEqual(failure['source']['state'],'WITHHELD')
        self.assertEqual(failure['experimental_host_filter']['source_measurement_state'],'WITHHELD')
        self.assertGreater(abs(failure['experimental_host_filter']['source_utc_minus_system_ns']),900000)
        self.assertIsNone(failure['experimental_host_filter']['absolute_uncertainty_ns'])
        self.assertFalse(failure['experimental_host_filter']['chrony_eligible'])
        # Source epoch warmup/permission is independent of the host model.
        self.assertEqual(filtered['source_rows'][100]['source']['state'],'WARMUP')
        self.assertTrue(all(r['source']['state']=='WARMUP' for r in filtered['source_rows'][100:164]))
        self.assertEqual(filtered['source_rows'][164]['source']['state'],'CAUSAL_RELATIVE_DIAGNOSTIC')
        original_phase=filtered['source_rows'][80]['experimental_host_filter']['source_utc_minus_system_ns']
        for row in filtered['source_rows'][100:]:
            self.assertAlmostEqual(row['experimental_host_filter']['source_utc_minus_system_ns']-original_phase,1_000_000,delta=.001)

    def test_ute_diagnostic_does_not_restore_unavailable_source(self):
        raw=self.raw_fixture(n=1)
        strict=change(change(change(wire(),36,4,0),64,8,0),220,4,1)
        raw['samples'][0]['source']['wire_hex']=strict.hex()
        estimate=change(change(change(wire(magic=b'UTE1'),36,4,6),220,4,1),240,8,16_000_050)
        raw['samples'][0]['estimate']=dict(raw['samples'][0]['source'],wire_hex=estimate.hex())
        raw['local_estimate_diagnostic_hz']=1
        result=analyse(raw,calibration=None,host_filter=True)
        self.assertEqual(result['source']['counts'].get('accepted',0),0)
        self.assertEqual(result['denominators']['conditional_offset_admitted'],0)
        d=result['local_estimate_diagnostic'];self.assertFalse(d['measurement']);self.assertFalse(d['grants_time_authority'])
        state=d['states'][0];self.assertTrue(state['estimate_present']);self.assertFalse(state['session_current'])
        self.assertEqual(state['session_remaining_ticks'],-50)
        self.assertNotIn('provisional_shadow',result['source_rows'][0])

    def test_modest_cadence_recovery_reports_seconds_and_keeps_source_step(self):
        step=scenario('50us_host_phase_step')
        self.assertEqual(step['post_change_training_withheld'],4)
        self.assertEqual(step['post_change_warmup'],64)
        self.assertEqual(step['first_post_change_forecast_within_1us_delay_s'],4.25)
        self.assertEqual(step['recovery_after_first_failure_delay_from_change_s'],4.25)
        gap=scenario('2.5s_host_observation_gap')
        self.assertEqual(gap['first_post_change_forecast_within_1us_delay_s'],6.5)
        self.assertEqual(gap['post_change_warmup'],64)
        rate=scenario('100ppm_host_rate_step')
        self.assertEqual(rate['first_post_change_forecast_within_1us_delay_s'],0)
        self.assertEqual(rate['recovery_after_first_failure_delay_from_change_s'],4.375)
        source=scenario('1ms_source_phase_epoch_step')
        self.assertEqual(source['post_change_warmup'],0)
        self.assertEqual(source['derived_source_phase']['n'],260)
        self.assertAlmostEqual(source['derived_source_phase']['mean_ns'],1_000_000,delta=.001)

    def test_weighted_long_window_scores_tail_before_training_and_keeps_it(self):
        m=RobustHost(maximum=1024,gated=False);truth=self.train(m,n=1100)
        r=m.observe(1100,truth+40000,truth+60000)
        self.assertAlmostEqual(r['forecast_error_ns'],50000,delta=.001)
        self.assertTrue(r['training_accepted']);self.assertEqual(len(m.points),1024)
        self.assertEqual(m.counts['forecast_scored'],1037)
        self.assertEqual(m.counts['training_withheld'],0)
        self.assertLess(abs(m.observe(1101,truth+125001-10000,truth+125001+10000)['forecast_error_ns']),100)

    def test_weighted_step_retains_every_forecast_and_gap_warmup(self):
        step=scenario('50us_host_phase_step','weighted-host-v2')
        self.assertEqual(step['common_denominator'],356)
        self.assertEqual(step['post_change_training_withheld'],0)
        self.assertEqual(step['post_change_warmup'],0)
        self.assertEqual(step['counts']['training_accepted'],420)
        self.assertGreater(step['robust_all_forecasts']['above_5us'],0)
        self.assertEqual(step['rows'][160]['robust']['forecast_error_ns'],50000)
        gap=scenario('2.5s_host_observation_gap','weighted-host-v2')
        self.assertEqual(gap['post_change_warmup'],64)
        self.assertEqual(gap['first_post_change_forecast_within_1us_delay_s'],6.5)

    def test_weighted_analysis_preserves_source_authority_and_original_outputs(self):
        raw=self.raw_fixture();before=copy.deepcopy(raw)
        plain=analyse(raw,calibration=None);weighted=analyse(raw,calibration=None,host_filter='weighted-host-v2')
        self.assertEqual(raw,before);self.assertEqual(plain['source'],weighted['source'])
        self.assertEqual(plain['conditional_spec_shadow'],weighted['conditional_spec_shadow'])
        self.assertEqual(weighted['experimental_host_filter']['control_maximum_training_points'],1024)
        self.assertEqual(weighted['source_rows'][90]['experimental_host_filter']['source_measurement_state'],'WITHHELD')
        self.assertGreater(abs(weighted['source_rows'][90]['experimental_host_filter']['source_utc_minus_system_ns']),900000)
        original=weighted['source_rows'][80]['experimental_host_filter']['source_utc_minus_system_ns']
        self.assertAlmostEqual(weighted['source_rows'][100]['experimental_host_filter']['source_utc_minus_system_ns']-original,1_000_000,delta=.001)
        with self.assertRaises(ValueError):analyse(raw,host_filter='unreviewed')


class ClockPairTests(unittest.TestCase):
    def clock(self):
        return dict(raw_before_ns=100,raw_after_ns=200,realtime_ns=1_790_000_000_000_000_001,
            realtime_bracket_method='native-pair',realtime_raw_before_ns=160,realtime_raw_after_ns=180)

    def test_integer_containment_and_legacy_absence(self):
        self.assertEqual(realtime_interval(self.clock()),dict(method='native-pair',low_ns=160,high_ns=180,nested=True))
        c=self.clock()
        for k in ('realtime_bracket_method','realtime_raw_before_ns','realtime_raw_after_ns'):c.pop(k)
        self.assertEqual(realtime_interval(c),dict(method='legacy-wide',low_ns=100,high_ns=200,nested=False))

    def test_bad_nested_boundaries_types_and_labels_refused(self):
        for key,value in (('realtime_raw_before_ns',99),('realtime_raw_after_ns',201),('realtime_raw_before_ns',181),
                          ('realtime_raw_after_ns',True),('realtime_ns',1.79e18),('realtime_bracket_method','other'),
                          ('realtime_bracket_method','legacy-wide')):
            c=self.clock();c[key]=value
            with self.subTest(key=key,value=value),self.assertRaises(ValueError):realtime_interval(c)
        c=self.clock();c.pop('realtime_raw_after_ns')
        with self.assertRaises(ValueError):realtime_interval(c)

    def test_all_snapshot_denominator_includes_unavailable_and_invalid(self):
        c=self.clock();bad=self.clock();bad['realtime_raw_after_ns']=201
        r=snapshot_summary(dict(samples=[dict(clocks=c,source=dict(valid=False,before=c,after=bad))]))
        self.assertEqual(r['counts'],dict(snapshots=3,**{'native-pair':2},invalid=1))
        self.assertEqual(r['native_bracket_ns']['n'],2);self.assertEqual(r['native_minus_full_midpoint_ns']['mean_ns'],20)
        self.assertEqual(r['paired_full_bracket_ns']['n'],r['native_bracket_ns']['n'])
        self.assertEqual(r['invalid'][0]['path'],'raw.samples[0].source.after')

    def test_native_error_cleanup_and_callback_restore(self):
        def failure(pointer):ctypes.set_errno(5);return -1
        legacy=dict(realtime_ns=1,raw_before_ns=1,raw_after_ns=2)
        with mock.patch.object(live,'_realtime_pair_reader',failure):
            with mock.patch.object(live.time,'CLOCK_BOOTTIME',7,create=True),mock.patch.object(live.time,'clock_gettime_ns',return_value=1),mock.patch.object(live.time,'monotonic_ns',return_value=1):
                with self.assertRaisesRegex(ValueError,'errno=5'):live.clocks()
            errors=[]
            with mock.patch.object(live,'cleanup_clocks',return_value=legacy):self.assertEqual(live.final_clocks(errors),legacy)
            self.assertEqual(errors[0]['stage'],'final_clock_pair');self.assertIs(live._realtime_pair_reader,failure)
            with mock.patch.object(live.time,'CLOCK_BOOTTIME',7,create=True),mock.patch.object(live.time,'CLOCK_TAI',8,create=True),mock.patch.object(live.time,'clock_gettime_ns',return_value=1):
                self.assertEqual(live.cleanup_clocks()['realtime_bracket_method'],'legacy-wide')
                self.assertIs(live._realtime_pair_reader,failure)

    def test_native_snapshot_preserves_integer_and_full_containment(self):
        def reader(pointer):
            p=ctypes.cast(pointer,ctypes.POINTER(live.RealtimePair)).contents
            p.before=160;p.after=180;p.realtime=1_790_000_000_000_000_001;return 0
        with mock.patch.object(live,'_realtime_pair_reader',reader),mock.patch.object(live.time,'clock_gettime_ns',side_effect=[100,120,190,200]),mock.patch.object(live.time,'monotonic_ns',return_value=110),mock.patch.object(live.time,'CLOCK_BOOTTIME',7,create=True),mock.patch.object(live.time,'CLOCK_TAI',8,create=True):
            c=live.clocks()
        self.assertEqual(c['realtime_ns'],1_790_000_000_000_000_001)
        self.assertEqual(realtime_interval(c)['low_ns'],160)

    def test_legacy_read_order_and_active_context_guard(self):
        order=[]
        def get(clock):order.append(clock);return 100
        def monotonic():order.append('monotonic');return 100
        def realtime():order.append('realtime');return 1_790_000_000_000_000_001
        with mock.patch.object(live,'_realtime_pair_reader',None),mock.patch.object(live.time,'CLOCK_BOOTTIME',7,create=True),mock.patch.object(live.time,'CLOCK_TAI',8,create=True),mock.patch.object(live.time,'clock_gettime_ns',side_effect=get),mock.patch.object(live.time,'monotonic_ns',side_effect=monotonic),mock.patch.object(live.time,'time_ns',side_effect=realtime):
            c=live.clocks()
        self.assertEqual(order,[live.time.CLOCK_MONOTONIC_RAW,'monotonic',7,'realtime',8,live.time.CLOCK_MONOTONIC_RAW])
        self.assertEqual(c['realtime_bracket_method'],'legacy-wide')
        with mock.patch.object(live,'_realtime_pair_reader',object()),mock.patch.object(live,'discover') as discover:
            with self.assertRaisesRegex(ValueError,'already active'):live.capture('invalid')
            discover.assert_not_called()

    def test_common_phase_control_raw_authority_and_withheld_tail_preserved(self):
        raw=RobustHostTests().raw_fixture();raw['realtime_bracket_method']='native-pair'
        for sample in raw['samples']:
            a=sample['source']['before'];a.update(realtime_bracket_method='native-pair',
                realtime_raw_before_ns=a['raw_before_ns']+70,realtime_raw_after_ns=a['raw_before_ns']+90)
        pristine=copy.deepcopy(raw);a=analyse(raw,calibration=None)
        self.assertEqual(raw,pristine)
        native=a['native_realtime_pair_shadow'];self.assertGreater(native['common_predictions'],0)
        for row in a['source_rows']:
            if 'source_utc_minus_system_ns' not in row.get('native_realtime_pair_shadow',{}):continue
            paired=row['native_realtime_pair_shadow'];wide=row['conditional_spec_shadow']
            self.assertAlmostEqual(paired['source_utc_minus_system_ns']-wide['source_utc_minus_system_ns'],30,delta=1e-9)
            self.assertEqual(paired['full_bracket_control_source_utc_minus_system_ns'],wide['source_utc_minus_system_ns'])
        self.assertGreater(abs(a['source_rows'][90]['native_realtime_pair_shadow']['source_utc_minus_system_ns']),900000)
        broken=copy.deepcopy(raw);broken['samples'][90]['source']['before']['realtime_raw_after_ns']+=100
        b=analyse(broken,calibration=None)
        self.assertEqual(a['source'],b['source']);self.assertEqual(a['conditional_spec_shadow'],b['conditional_spec_shadow'])
        self.assertIn('withheld',b['source_rows'][90]['native_realtime_pair_shadow'])
        self.assertEqual(b['native_realtime_pair_shadow']['common_predictions'],native['common_predictions']-1)

    def test_fixed_offset_sign_and_disabled_outputs_with_native_pair(self):
        raw=RobustHostTests().raw_fixture();raw['realtime_bracket_method']='native-pair'
        raw['device'].update(driver='xhci-hcd',controller_compatible='snps,dwc3')
        for sample in raw['samples']:
            a=sample['source']['before'];a.update(realtime_bracket_method='native-pair',
                realtime_raw_before_ns=a['raw_before_ns']+70,realtime_raw_after_ns=a['raw_before_ns']+90)
        pristine=copy.deepcopy(raw);a=analyse(raw);disabled=analyse(raw,calibration=None)
        self.assertEqual(raw,pristine);self.assertEqual(a['source'],disabled['source'])
        for row in a['source_rows']:
            p=row.get('native_realtime_pair_shadow',{})
            if 'source_utc_minus_system_ns' in p:
                self.assertAlmostEqual(p['provisional_source_utc_minus_system_ns']-p['source_utc_minus_system_ns'],-25600,delta=1e-9)
                self.assertIsNone(disabled['source_rows'][row['index']]['native_realtime_pair_shadow']['provisional_source_utc_minus_system_ns'])
        self.assertEqual(a['provisional_calibration']['application_scope']['realtime_bracket_method'],'native-pair')

    def test_coherent_interior_warmup_keeps_admitted_host_diagnostic_tail(self):
        raw=RobustHostTests().raw_fixture(n=100);raw['realtime_bracket_method']='native-pair'
        raw['device'].update(driver='xhci-hcd',controller_compatible='snps,dwc3')
        for i,sample in enumerate(raw['samples']):
            packet=wire(i)  # Remove the fixture's separate single-label case.
            if i in (70,71):
                packet=change(packet,40,2,(int.from_bytes(packet[40:42],'little')+1)%2048)
                packet=change(packet,56,8,int.from_bytes(packet[48:56],'little')+15980+i-70)
            sample['source']['wire_hex']=packet.hex()
            c=sample['source']['before'];c.update(realtime_bracket_method='native-pair',
                realtime_raw_before_ns=c['raw_before_ns']+70,realtime_raw_after_ns=c['raw_before_ns']+90)
        pristine=copy.deepcopy(raw);a=analyse(raw);rows=a['source_rows'];interior=rows[71]
        self.assertEqual(raw,pristine)
        self.assertEqual([rows[i]['source']['state'] for i in (70,71,72)],['WITHHELD','WARMUP','WITHHELD'])
        self.assertEqual(interior['capture_continuity']['state'],'COHERENT')
        self.assertIsNone(interior['source']['causal_error_ns'])
        phase=interior['native_realtime_pair_shadow']
        self.assertEqual(phase['source_measurement_state'],'ADMITTED')
        self.assertGreater(abs(phase['provisional_source_utc_minus_system_ns']),900000)
        self.assertAlmostEqual(phase['provisional_source_utc_minus_system_ns']-phase['source_utc_minus_system_ns'],-25600,delta=1e-9)
        self.assertFalse(phase['chrony_eligible']);self.assertIsNone(phase['absolute_uncertainty_ns'])
        admitted=[c for c in a['native_realtime_pair_shadow']['phase_cohorts'] if c['measurement_state']=='ADMITTED']
        self.assertTrue(any(c['native_pair_provisional']['max_abs_ns']>900000 for c in admitted))
        self.assertEqual(a['source']['counts']['COUNTER_FRAME_DISCONTINUITY'],2)


class NativeFilterComparisonTests(unittest.TestCase):
    def fixture(self,n=180):
        raw=RobustHostTests().raw_fixture(n);raw['realtime_bracket_method']='native-pair'
        raw['device'].update(driver='xhci-hcd',controller_compatible='snps,dwc3')
        for sample in raw['samples']:
            c=sample['source']['before'];c.update(realtime_bracket_method='native-pair',
                realtime_raw_before_ns=c['raw_before_ns']+70,realtime_raw_after_ns=c['raw_before_ns']+90)
        return raw

    def test_common_event_sign_raw_preservation_and_withheld_anomaly(self):
        raw=self.fixture();pristine=copy.deepcopy(raw);a=evaluate_native_filters(raw)
        self.assertEqual(raw,pristine);self.assertFalse(a['source_permission_changed']);self.assertFalse(a['filter_parameters_changed'])
        for method,rows in a['retained_phase_forecasts'].items():
            bad=[r for r in rows if r['measurement_state']=='WITHHELD']
            self.assertEqual(len(bad),1);self.assertGreater(abs(bad[0]['native_filtered_provisional_ns']),900000)
            for r in rows:self.assertAlmostEqual(r['native_filtered_provisional_ns']-r['native_filtered_ns'],-25600,delta=1e-9)
        for c in a['phase_cohorts']:
            expected=set.intersection(*(set(r['index'] for r in rows if r['measurement_state']==c['measurement_state'] and tuple(r['scope'])==tuple(c['scope'])) for rows in a['retained_phase_forecasts'].values()))
            self.assertEqual(c['common_all_methods_indices'],sorted(expected))

    def test_future_observations_cannot_change_past_forecasts(self):
        short=evaluate_native_filters(self.fixture(150));long=evaluate_native_filters(self.fixture(180))
        for method in short['retained_phase_forecasts']:
            self.assertEqual(short['retained_phase_forecasts'][method],[r for r in long['retained_phase_forecasts'][method] if r['index']<150])

    def test_legacy_capture_is_not_reconstructed_as_native(self):
        with self.assertRaisesRegex(ValueError,'native-pair capture'):evaluate_native_filters(RobustHostTests().raw_fixture())

    def test_disabled_correction_retains_raw_forecasts_and_report_units(self):
        a=evaluate_native_filters(self.fixture(100),calibration=None)
        self.assertFalse(a['provisional_calibration']['enabled']);self.assertIn('raw phase (ns)',native_filter_markdown(a))
        for rows in a['retained_phase_forecasts'].values():
            for r in rows:self.assertIsNone(r['native_filtered_provisional_ns']);self.assertIsNotNone(r['native_filtered_ns'])

    def test_missing_filter_forecasts_remain_explicit_denominators(self):
        raw=self.fixture()
        for sample in raw['samples'][80:]:
            for name in ('host_before','host_after'):
                for read in sample[name]:
                    for key in ('before_raw_ns','after_raw_ns'):read[key]+=50000
        a=evaluate_native_filters(raw);missing=0
        for cohort in a['phase_cohorts']:
            for method,c in cohort['methods'].items():
                self.assertEqual(c['common_denominator']+len(c['missing_forecast_indices']),len(cohort['all_native_OLS_indices']))
                if method=='robust-host-v1':missing+=len(c['missing_forecast_indices'])
        self.assertGreater(missing,0)


class AffineWindowSensitivityTests(unittest.TestCase):
    def transitions(self,n=120,step=False):
        origin=8*10**18;out=[]
        for i in range(n):
            truth=origin+i*125003+(50000 if step and i>=80 else 0)
            out.append(dict(ordinal=i,low_raw_ns=truth-1000,high_raw_ns=truth+1000,acquisition=i>=30))
        return out

    def test_known_synthetic_truth_is_inside_preceding_interval_at_large_epoch(self):
        a=score_affine_windows(self.transitions(),(32,64))
        self.assertEqual(a['transitions'],120);self.assertEqual(a['acquisition_transitions'],90)
        for row in a['rows']:
            truth=8*10**18+row['ordinal']*125003
            for m in row['models'].values():
                if m['prediction'] is not None:self.assertLessEqual(m['prediction']['low_ns'],truth);self.assertGreaterEqual(m['prediction']['high_ns'],truth)
        for m in a['models'].values():self.assertEqual(m['all']['disjoint'],0)

    def test_current_step_miss_and_recovery_warmup_are_not_dropped(self):
        a=score_affine_windows(self.transitions(step=True),(32,64))
        for model in a['rows'][80]['models'].values():
            self.assertFalse(model['prediction']['overlaps_complete_read_bracket']);self.assertTrue(model['inconsistent_reset_after_score'])
        for m in a['models'].values():
            self.assertGreater(m['all']['disjoint'],0);self.assertGreater(m['all']['missing'],16)
            self.assertEqual(m['all']['predicted']+m['all']['missing'],120)

    def test_future_cannot_change_prefix_and_gap_starts_new_segment(self):
        short=score_affine_windows(self.transitions(100,True),(32,64));long=score_affine_windows(self.transitions(120,True),(32,64))
        self.assertEqual(short['rows'],long['rows'][:100])
        rows=self.transitions()
        for row in rows[80:]:
            row['ordinal']+=10000;row['low_raw_ns']+=1_250_030_000;row['high_raw_ns']+=1_250_030_000
        a=score_affine_windows(rows,(32,64));self.assertEqual(a['boundaries'],[80])
        self.assertTrue(all(m['prediction'] is None for m in a['rows'][80]['models'].values()))

    def test_parameters_and_host_suspend_population_fail_closed(self):
        for values in ((True,64,16),(10001,64,16),(1000,True,16),(1000,8,16),(1000,4097,16)):
            with self.assertRaises(ValueError):AffineBrackets(*values)
        with self.assertRaises(ValueError):score_affine_windows([],())
        with self.assertRaises(ValueError):score_affine_windows([],(64,64))
        raw=RobustHostTests().raw_fixture();pristine=copy.deepcopy(raw)
        a=evaluate_affine_windows(raw,(32,64));self.assertEqual(raw,pristine);self.assertFalse(a['chrony_eligible'])
        raw['samples'][90]['clocks']['boottime_ns']+=1_000_000
        with self.assertRaisesRegex(ValueError,'split host'):evaluate_affine_windows(raw,(32,64))


class OriginBoundTests(unittest.TestCase):
    def marker(self,origin=8*10**18):
        def clock(at):return dict(raw_before_ns=at,raw_after_ns=at+10,realtime_ns=1_790_000_000_000_000_001)
        anchor=dict(contract='UFA1',valid=True,token=1,tick=100000,current_tick=115999)
        marker=dict(token=1,out_length=0,length=64,out_before=clock(origin+900000),out_after=clock(origin+1100000),
            in_before=clock(origin+1200000),in_after=clock(origin+1300000))
        return anchor,marker

    def test_exact_half_ns_old_SOF_enclosure_and_full_exchange_is_wider(self):
        a,m=self.marker();full=snapshot_interval(a,m,ORIGIN_PROFILES[0]);out=snapshot_interval(a,m,ORIGIN_PROFILES[1])
        truth_twice=16*10**18+125
        self.assertLessEqual(full['low_raw_twice_ns'],truth_twice);self.assertGreaterEqual(full['high_raw_twice_ns'],truth_twice)
        self.assertIsInstance(full['low_raw_twice_ns'],int);self.assertEqual(full['high_raw_twice_ns']-out['high_raw_twice_ns'],400000)
        self.assertFalse(full['source_permission'])

    def test_token_lengths_order_age_and_profile_fail_closed(self):
        for name,mutate in (('token',lambda a,m:m.update(token=2)),('short',lambda a,m:m.update(length=63)),
                ('OUTlength',lambda a,m:m.update(out_length=1)),('age',lambda a,m:a.update(current_tick=116000)),
                ('invalid',lambda a,m:a.update(valid=False)),('clockrollback',lambda a,m:m['in_before'].update(raw_before_ns=0))):
            a,m=self.marker();mutate(a,m)
            with self.subTest(name=name),self.assertRaises(ValueError):snapshot_interval(a,m,ORIGIN_PROFILES[0])
        a,m=self.marker()
        with self.assertRaises(ValueError):snapshot_interval(a,m,'unknown')

    def test_constant_origin_prediction_is_past_only_and_contradiction_persists(self):
        model=OriginIntersection()
        for i in range(16):self.assertFalse(model.observe(-100,100)['prior_prediction_available'])
        row=model.observe(-50,150);self.assertEqual(row['prior_low_twice_ns'],-100);self.assertFalse(row['contains_complete_current_interval'])
        row=model.observe(1000,2000);self.assertFalse(row['overlaps_current_interval']);self.assertTrue(row['infeasible_after_observation'])
        row=model.observe(-50,50);self.assertTrue(row['infeasible_after_observation']);self.assertFalse(row['prior_prediction_available'])

    def fixture(self):
        raw=RobustHostTests().raw_fixture(100);origin=raw['start']['raw_before_ns']
        for j,i in enumerate(range(0,100,4)):
            token=j+1;tick=16_000_000+i*512000;packet=bytearray(64)
            def put(o,n,v):packet[o:o+n]=v.to_bytes(n,'little')
            packet[:4]=b'UFA1'
            for o,n,v in ((4,2,1),(6,2,64),(8,8,2),(16,8,3),(24,4,1),(28,4,1),(32,4,token),(36,4,1),(40,2,(2040+32*i)%2048),(42,2,token),(44,8,tick),(52,8,tick+12800)):put(o,n,v)
            def clock(at):return dict(raw_before_ns=at,raw_after_ns=at+100,realtime_ns=1_790_000_000_000_000_001)
            at=origin+i*32000000;raw['samples'][i]['marker']=dict(token=token,out_length=0,length=64,wire_hex=packet.hex(),
                out_before=clock(at+700000),out_after=clock(at+1100000),in_before=clock(at+1200000),in_after=clock(at+1800000))
        return raw

    def test_offline_raw_preservation_and_integer_origin_degeneracy(self):
        raw=self.fixture();original=copy.deepcopy(raw);a=evaluate_origin_bounds(raw);self.assertEqual(raw,original)
        self.assertEqual(a['counts']['marker_attempted'],25);self.assertFalse(a['source_permission_or_calibration_changed']);self.assertFalse(a['chrony_eligible'])
        for group in a['groups']:
            for s in group['scenarios']:
                if s['observations']:
                    bias_twice=-s['integer_frame_origin']*2_000_000
                    self.assertFalse(s['infeasible']);self.assertLessEqual(s['retrospective_intersection_low_twice_ns'],bias_twice);self.assertGreaterEqual(s['retrospective_intersection_high_twice_ns'],bias_twice)

    def test_stale_token_rejection_fences_later_markers(self):
        raw=self.fixture();raw['samples'][40]['marker']['token']=999;a=evaluate_origin_bounds(raw)
        bad=[r for r in a['rows'] if 'rejected' in r];self.assertEqual(bad[0]['index'],40)
        self.assertTrue(all(not r.get('scenarios') for r in bad));self.assertGreater(len(bad),1)

    def test_future_cannot_change_scored_prefix_and_USB_epoch_has_fresh_set(self):
        raw=self.fixture();short=copy.deepcopy(raw);short['samples']=short['samples'][:60]
        a=evaluate_origin_bounds(short);b=evaluate_origin_bounds(raw)
        self.assertEqual(a['rows'],[r for r in b['rows'] if r['index']<60])
        for sample in raw['samples'][60:]:
            if 'marker' not in sample:continue
            packet=bytearray.fromhex(sample['marker']['wire_hex']);packet[24:28]=(2).to_bytes(4,'little')
            sample['marker']['wire_hex']=packet.hex()
        c=evaluate_origin_bounds(raw);self.assertEqual(len(c['groups']),2)
        for row in c['rows']:
            if row['index']>=60:
                self.assertTrue(all(not s['constant_origin_prior']['prior_prediction_available'] for s in row.get('scenarios',[])))


class HostPreparationTests(unittest.TestCase):
    def test_bounded_host_only_reads_audits_and_complete_cost_boundary(self):
        now=[0]
        def sleep(seconds):now[0]+=round(seconds*1e9)
        runtime=mock.Mock();runtime.burst.return_value=[dict(mfindex=1,before_raw_ns=1,after_raw_ns=2)]
        result={};audit=mock.Mock()
        with mock.patch.object(live.time,'monotonic_ns',side_effect=lambda:now[0]),mock.patch.object(live.time,'sleep',side_effect=sleep),mock.patch.object(live,'clocks',return_value={}),mock.patch.object(live,'request') as request:
            live.prepare_host(runtime,5,result,audit);request.assert_not_called()
        self.assertEqual(len(result['bursts']),40);self.assertEqual(audit.call_count,5)
        self.assertEqual(result['elapsed_monotonic_ns'],5_000_000_000);self.assertEqual(result['source_requests'],0)
        self.assertEqual(result['errors'],[])

    def test_partial_failure_retains_attempted_burst_and_stops(self):
        now=[0]
        runtime=mock.Mock();runtime.burst.side_effect=[[dict(mfindex=1)],ValueError('test read failure')]
        result={}
        with mock.patch.object(live.time,'monotonic_ns',side_effect=lambda:now[0]),mock.patch.object(live.time,'sleep',side_effect=lambda s:now.__setitem__(0,now[0]+round(s*1e9))),mock.patch.object(live,'clocks',return_value={}):
            with self.assertRaisesRegex(ValueError,'test read failure'):live.prepare_host(runtime,5,result,mock.Mock())
        self.assertEqual(len(result['bursts']),2);self.assertNotIn('reads',result['bursts'][1]);self.assertIn('end',result)
        for seconds in (-1,11,True,1.5):
            with self.assertRaises(ValueError):live.prepare_host(runtime,seconds,{},mock.Mock())
        with self.assertRaises(ValueError):live.prepare_host(None,1,{},mock.Mock())

    def test_preceding_host_warmup_scores_first_source_without_source_training(self):
        raw=RobustHostTests().raw_fixture();plain=analyse(raw,calibration=None);origin=raw['start']['raw_before_ns'];reads=[]
        for ordinal in range(16240,16320):
            at=origin+(ordinal-16320)*125000
            reads.extend([dict(mfindex=(ordinal-1)%16384,before_raw_ns=at-1000,after_raw_ns=at-900),
                dict(mfindex=ordinal%16384,before_raw_ns=at+900,after_raw_ns=at+1000)])
        raw['host_warmup_seconds']=5;raw['host_preparation_warmup']=dict(source_requests=0,bursts=[dict(reads=reads)],errors=[])
        a=analyse(raw,calibration=None)
        self.assertIn('source_utc_minus_system_ns',a['source_rows'][0]['conditional_spec_shadow'])
        self.assertIn('withheld',plain['source_rows'][0]['conditional_spec_shadow'])
        self.assertEqual(a['source'],plain['source']);self.assertEqual(a['source_rows'][0]['source']['state'],'WARMUP')
        self.assertEqual(a['host_preparation_warmup']['source_model_training_points'],0)
        raw['host_preparation_warmup']['bursts'][0]['reads'][-1]['after_raw_ns']=origin+1
        with self.assertRaisesRegex(ValueError,'strictly preceding'):analyse(raw,calibration=None)
        raw['host_preparation_warmup']['source_requests']=1
        with self.assertRaisesRegex(ValueError,'host-only'):analyse(raw,calibration=None)


class IntervalTests(unittest.TestCase):
    def test_rate_interval_is_predicted_before_intersection(self):
        m=BoundedRate(ppm=1000);self.assertIsNone(m.observe(0,1000,2000))
        r=m.observe(1,126000,127000)
        self.assertEqual((r['low_ns'],r['high_ns']),(125874,127126))
        self.assertEqual(m.last,(1,125999,127001))

    def test_rate_miss_retains_failure_and_reseeds_next_prediction(self):
        m=BoundedRate();m.observe(0,0,10);r=m.observe(1,999000,999010)
        self.assertFalse(r['overlaps_complete_read_bracket']);self.assertGreater(r['disjoint_distance_ns'],800000)
        self.assertEqual(m.counts['inconsistent_reset'],1);self.assertEqual(m.last,(1,998999,999011))

    def test_rate_outward_epoch_arithmetic_and_reverse_projection(self):
        m=BoundedRate(ppm=1);origin=8*10**18;m.observe(100,origin,origin+1)
        lo,hi=m.predict(99);self.assertEqual(lo,origin-125002);self.assertEqual(hi,origin-124997)
        self.assertIsInstance(lo,int)

    def test_affine_contains_known_clock_and_causal_outlier_fails(self):
        m=AffineBrackets(minimum=4)
        for i in range(10):
            truth=8*10**18+i*125001;m.observe(i,truth-10,truth+10)
        lo,hi=m.predict(10);self.assertLessEqual(lo,8*10**18+1250010);self.assertGreaterEqual(hi,8*10**18+1250010)
        r=m.observe(10,8*10**18+1250010+100000,8*10**18+1250010+100010)
        self.assertFalse(r['overlaps_complete_read_bracket']);self.assertEqual(m.counts['inconsistent_reset'],1)

    def test_affine_arbitrary_rate_change_is_trial_failure_not_trimmed_tail(self):
        m=AffineBrackets(minimum=4);y=0;failures=0
        for i in range(40):
            y+=125000+(100 if i>=20 else -100)
            r=m.observe(i,y-1,y+1)
            failures+=bool(r is not None and not r['overlaps_complete_read_bracket'])
        self.assertGreater(failures,0);self.assertGreater(m.counts['inconsistent_reset'],0)

    def test_interval_coverage_distinguishes_overlap_from_full_containment(self):
        m=BoundedRate(ppm=0);m.observe(0,0,10);r=m.observe(1,125005,125020)
        self.assertTrue(r['overlaps_complete_read_bracket']);self.assertFalse(r['contains_complete_read_bracket'])


class SettlementTests(unittest.TestCase):
    def fixture(self):
        raw=dict(elapsed_monotonic_ns=60_000_000_001,rate=8,errors=[],interface_released=True)
        a=dict(attempts=dict(attempted=480,source_valid=480),source=dict(counts=dict(accepted=480)),
               simultaneous_GPIO=dict(status_observations=60,eligible_status_observations=60,new_edges=dict(n=60),source_eligible_edges=dict(n=59),gaps=[]),
               sampled_lineage_join=dict(MATCHING_SAMPLED_GM_ROOT_BOOT_EPOCH=480),
               marker_identity=dict(integer_origin_constrained=True,blocked=False))
        return raw,a

    def test_settlement_uses_continuity_never_residual_magnitude(self):
        raw,a=self.fixture();self.assertTrue(settlement_ready(raw,a)['ready'])
        a['simultaneous_GPIO']['new_edges']['rms_ns']=1e9
        self.assertTrue(settlement_ready(raw,a)['ready'])
        self.assertFalse(settlement_ready(raw,a)['selection_uses_error_magnitude'])

    def test_source_outage_and_failed_control_preclude_settlement(self):
        raw,a=self.fixture();a['source']['counts']['accepted']=479;a['simultaneous_GPIO']['eligible_status_observations']=0
        r=settlement_ready(raw,a);self.assertFalse(r['ready']);self.assertIn('USB_SOURCE_NOT_CONTINUOUSLY_ADMITTED',r['reasons'])
        self.assertIn('MHAT_NOT_CONTINUOUSLY_ADMITTED',r['reasons'])


if __name__=='__main__':unittest.main()

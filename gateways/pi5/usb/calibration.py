"""Frozen dependent calibration of derived shadow outputs only.

Copyright (c) 2026 Jonathan Yantis. MIT. Raw maps and authority are unchanged.
"""
import copy
import json
import math
from pathlib import Path
import re

DEFAULT=json.loads(Path(__file__).with_name('provisional-calibration.json').read_text())


def validate(value):
    c=copy.deepcopy(value)
    if type(c.get('schema')) is not int or c['schema']!=1 or type(c.get('correction_ns')) is not int or c['correction_ns']!=-25600:
        raise ValueError('this frozen provisional experiment requires integer correction_ns=-25600')
    if c.get('operation')!='ADD_TO_DERIVED_USB_SHADOW_UTC_AND_PHASE' or c.get('provisional') is not True or c.get('frozen') is not True or c.get('automatic_refit') is not False or c.get('absolute_uncertainty_ns') is not None:
        raise ValueError('invalid frozen/provisional/unknown-bound calibration contract')
    b=c.get('basis',{});s=c.get('scope',{})
    if b.get('admitted_phase_samples')!=648 or b.get('uncorrected_mean_ns')!='25590.43285965478' or b.get('trained_read_method')!='seek-read' or any(b.get(k)!=DEFAULT['basis'][k] for k in ('capture','cohort','capture_sha256')):
        raise ValueError('calibration basis must preserve the original cohort provenance')
    if c.get('dependent_reference')!='M-HAT-disciplined Pi CLOCK_REALTIME' or s.get('driver')!='xhci-hcd' or s.get('controller_compatible')!='snps,dwc3' or s.get('speed_mbps')!='12' or s.get('direct_root_port') is not True or s.get('read_methods')!=['seek-read','pread','native-pread'] or s.get('transfer_between_methods_or_sources_is_qualified') is not False:
        raise ValueError('calibration controller/method/dependency scope mismatch')
    if s.get('expected_silicon') is not None and (not re.fullmatch('[0-9a-f]{16}',s['expected_silicon']) or not int(s['expected_silicon'],16)):raise ValueError('invalid exact calibration silicon binding')
    if s.get('expected_controller') is not None and (not isinstance(s['expected_controller'],str) or not 1<=len(s['expected_controller'])<=512 or any(not 32<=ord(c)<127 for c in s['expected_controller'])):raise ValueError('invalid exact controller binding')
    if s.get('expected_USB_path') is not None and (not isinstance(s['expected_USB_path'],str) or not re.fullmatch(r'\d+-\d+',s['expected_USB_path'])):raise ValueError('invalid exact USB path binding')
    return c


def metadata(raw,value=DEFAULT):
    if value is None:return dict(enabled=False,reason='EXPLICITLY_DISABLED',absolute_uncertainty_ns=None,chrony_eligible=False)
    c=validate(value);device=raw['device'];s=c['scope'];method=raw.get('host_read_method','seek-read')
    matches=(device.get('driver')==s['driver'] and device.get('controller_compatible','').rstrip(',')==s['controller_compatible'] and
        device.get('speed_mbps')==s['speed_mbps'] and device.get('direct_root_port') is True and method in s['read_methods'] and
        (s['expected_silicon'] is None or s['expected_silicon']==device.get('silicon')) and
        (s.get('expected_controller') is None or s['expected_controller']==device.get('controller')) and
        (s.get('expected_USB_path') is None or s['expected_USB_path']==device.get('path')))
    return dict(enabled=matches,reason='PROVISIONAL_DEPENDENT_FIXED_OFFSET' if matches else 'CALIBRATION_SCOPE_MISMATCH',
        configuration=c,applied_correction_ns=c['correction_ns'] if matches else None,
        application_scope=dict(silicon=device.get('silicon'),controller=device.get('controller'),USB_path=device.get('path'),read_method=method,
            realtime_bracket_method=raw.get('realtime_bracket_method','legacy-wide'),clock_pair_method_transfer_qualified=False,
            exact_identity_guard_required=True),absolute_uncertainty_ns=None,chrony_eligible=False,
        source_permission_or_expiry_changed=False,raw_records_changed=False)


def corrected_utc(value,meta):
    if not meta['enabled']:return None
    if type(value) is not int:raise ValueError('UTC must remain integer nanoseconds')
    result=value+meta['applied_correction_ns']
    if not 0<result<(1<<63):raise ValueError('derived UTC outside signed64 range')
    return result


def corrected_phase(value,meta):
    if not meta['enabled']:return None
    if type(value) not in (int,float) or not math.isfinite(value):raise ValueError('invalid derived phase')
    return value+meta['applied_correction_ns']

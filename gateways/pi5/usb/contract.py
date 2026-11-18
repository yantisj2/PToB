"""Strict public UTS1/UFS1/UFA1/UTE1 decoding; no transport or clock output.

Copyright (c) 2026 Jonathan Yantis. MIT.
Integer map arithmetic follows the existing macOS USBMapGeometry contract.
"""
import re

UNKNOWN = 0xffffffff
I64_MAX = (1 << 63)-1


def silicon_for_serial(serial):
    if not isinstance(serial, str) or not 16 <= len(serial) <= 128 or any(not 33 <= ord(c) <= 126 for c in serial):
        raise ValueError('invalid full USB serial')
    silicon = serial[-16:].lower()
    if not re.fullmatch('[0-9a-f]{16}', silicon) or int(silicon, 16) == 0:
        raise ValueError('invalid silicon suffix')
    return silicon


def decode(data, expected_silicon):
    if not isinstance(data, bytes) or len(data) < 8:
        raise ValueError('short wire record')
    u = lambda offset, n: int.from_bytes(data[offset:offset+n], 'little')
    signed = lambda offset, n: int.from_bytes(data[offset:offset+n], 'little', signed=True)
    magic = data[:4].decode('ascii', errors='strict')
    sizes = {'UTS1': 256, 'UTE1': 256, 'UFS1': 80, 'UFA1': 64}
    if magic not in sizes or len(data) != sizes[magic] or u(4, 2) != 1 or u(6, 2) != len(data):
        raise ValueError('unknown contract/version/length')
    d = dict(contract=magic, silicon=f'{u(8,8):016x}', boot=f'{u(16,8):016x}',
             usb_epoch=u(24,4), capture_epoch=u(28,4), sequence=u(32,4))
    if d['silicon'] != expected_silicon or int(d['silicon'],16) == 0 or not u(16,8) or not d['usb_epoch'] or not d['capture_epoch'] or not d['sequence']:
        raise ValueError('identity/epoch/sequence missing or mismatched')
    if magic in ('UFS1', 'UFA1'):
        d.update(frame=u(40,2), valid=bool(u(42 if magic == 'UFS1' else 36,2 if magic == 'UFS1' else 4)),
                 tick=u(44,8), current_tick=u(52,8), reason=u(76 if magic == 'UFS1' else 60,4))
        if u(42 if magic == 'UFS1' else 36,2 if magic == 'UFS1' else 4) > 1 or d['frame'] not in range(2048) and d['frame'] != 65535:
            raise ValueError('invalid frame/flags')
        if magic == 'UFS1':
            d.update(loss_total=u(36,4), races=u(60,4), missed=u(64,4), overflow=u(68,4), fences=u(72,4))
        else:
            d['token'] = u(42,2)
        if d['valid'] and (d['reason'] or magic=='UFA1' and not d['token']):
            raise ValueError('invalid SOF/marker reason or token')
        if d['valid'] and (d['frame'] >= 2048 or d['current_tick'] < d['tick'] or d['current_tick']-d['tick'] >= 16000):
            raise ValueError('invalid captured SOF age')
        return d
    flags = u(36,4)
    if u(42,2) != 1 or data[72] not in (1,2) or any(data[44:48]) or any(data[73:80]):
        raise ValueError('invalid tick unit/kind/reserved bytes')
    if magic == 'UTS1' and flags > 1:
        raise ValueError('invalid source flags')
    if magic == 'UTE1':
        state = u(248,4)
        if data[72] != 1 or flags & ~31 or state > 2 or flags & 1 and flags & 30 != 30 or flags & 2 and (not flags & 4 or not state) or not flags & 2 and state:
            raise ValueError('invalid UTE1 flags/state')
        d.update(estimate_present=bool(flags & 2), tracking_state=state, last_discipline_tick=u(232,8),
                 session_until_tick=u(240,8), estimate_reason=u(252,4), measurement=False,
                 counter_valid=bool(flags & 4),session_current=bool(flags & 8),root_envelope_current=bool(flags & 16))
    d.update(valid=bool(flags & 1), kind=data[72], frame=u(40,2), flags=flags,
             tick=u(48,8), current_tick=u(56,8), utc_ns=u(64,8), root=f'{u(80,8):016x}',
             root_boot=f'{u(88,8):016x}', source_epoch=u(96,4), map_generation=u(100,4),
             until_tick=u(104,8), source_expires_ns=u(112,8), anchor_tick=u(120,8), anchor_ns=u(128,8),
             effective_tick=u(136,8), active_lo_tick=u(144,8), active_hi_tick=u(152,8),
             map_ppb=signed(160,4), map_phase_ns=signed(164,4), map_age_us=u(168,4),
             bound_ns=None if u(172,4)==UNKNOWN else u(172,4),
             rate_bound_ppb=None if u(176,4)==UNKNOWN else u(176,4),
             capture_bound_ns=None if u(180,4)==UNKNOWN else u(180,4),
             utc_offset_ns=signed(184,8), source_issued_ns=u(192,8), path_scope=data[200:216].hex(),
             history_overwrites=u(216,4), reason=u(220,4), hold_us=u(224,4), hold_ns=u(228,4))
    if d['frame'] not in range(2048) and d['frame'] != 65535 or d['current_tick'] < d['tick'] or d['utc_ns'] > I64_MAX:
        raise ValueError('invalid frame/counter/UTC')
    if magic=='UTE1' and (flags&24 and not flags&2 or
            flags&4 and (d['frame']>=2048 or d['current_tick']-d['tick']>=16000) or
            flags&2 and not d['last_discipline_tick']<=d['tick'] or
            flags&8 and not d['current_tick']<d['session_until_tick']):
        raise ValueError('invalid UTE1 local estimate counter/session')
    if magic == 'UTS1':
        if d['reason'] == 7:
            if d['valid'] or d['kind'] != 1 or d['utc_ns'] or data[232:240] != b'USM1\x01\x00\x18\x00' or u(248,4) != 1 or u(252,4):
                raise ValueError('malformed missing-SOF classification')
            d['current_source_ns'] = u(240,8)
        elif any(data[232:]):
            raise ValueError('unexpected extension')
    if d['valid']:
        if d['reason'] or not u(80,8) or not u(88,8) or not d['source_epoch'] or not d['map_generation'] or not 0 < d['utc_ns'] < UNKNOWN*1_000_000_000:
            raise ValueError('invalid source authority')
        if d['current_tick'] >= d['until_tick'] or d['kind'] == 1 and (d['frame'] >= 2048 or d['current_tick']-d['tick'] >= 16000) or d['kind'] == 2 and d['tick'] != d['current_tick']:
            raise ValueError('expired/noncoherent source sample')
        value = project(d, d['tick'])
        if value+d['utc_offset_ns'] != d['utc_ns']:
            raise ValueError('immutable map/sample disagreement')
    return d


def scope(d):
    fields = ['silicon','boot','usb_epoch','capture_epoch']
    if d['contract'] in ('UTS1','UTE1'):
        fields += ['root','root_boot','source_epoch','path_scope']
    return tuple(d[k] for k in fields)


def project(d, tick):
    if not (d['effective_tick'] <= d['active_lo_tick'] < d['active_hi_tick'] and
            max(d['effective_tick'],d['anchor_tick'],d['active_lo_tick']) <= tick < min(d['active_hi_tick'],d['until_tick']) and
            abs(d['map_ppb']) <= 300000 and abs(d['map_phase_ns']) <= 300000 and
            0 <= d['source_issued_ns'] < d['source_expires_ns'] <= I64_MAX):
        raise ValueError('map geometry/deadline/rate invalid')
    nominal = (tick-d['anchor_tick'])*125//2
    elapsed = nominal*1_000_000_000//(1_000_000_000+d['map_ppb'])
    # C and Swift signed integer division truncates toward zero.
    product = min(elapsed,1_000_000_000)*d['map_phase_ns']
    slew = (abs(product)//1_000_000_000) * (-1 if product < 0 else 1)
    value = d['anchor_ns']+elapsed+slew
    if not d['source_issued_ns'] <= value < d['source_expires_ns'] or value > I64_MAX:
        raise ValueError('original source deadline expired')
    return value


def permission_window_end(d, request_before):
    if d['contract'] != 'UTS1' or not d['valid']:
        raise ValueError('not a strict source measurement')
    remaining = min(500_000_000,(d['until_tick']-d['current_tick'])*6250//102)
    return request_before+remaining


def permission_deadline(d, request_before, received):
    deadline = permission_window_end(d,request_before)
    if received < request_before or received >= deadline:
        raise ValueError('original permission expired in flight')
    return deadline

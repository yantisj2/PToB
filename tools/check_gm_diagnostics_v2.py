#!/usr/bin/env python3
"""Offline GD2/legacy GD1 table/vectors check. MIT, copyright (c) 2026 Jonathan Yantis.

Python standard library only. No device access or physical timing claims.
"""
from pathlib import Path
import hashlib
import hmac
import json
import struct

ROOT = Path(__file__).resolve().parents[1]
LAYOUT = [
    ('ad_length', 0, 1), ('ad_type', 1, 1), ('company', 2, 2),
    ('marker', 4, 2), ('version', 6, 1), ('reserved', 7, 1),
    ('group_id', 8, 8), ('key_epoch', 16, 4), ('root', 20, 8),
    ('root_boot', 28, 8), ('mesh_epoch', 36, 4), ('sequence', 40, 4),
    ('sampled_us', 44, 8), ('valid_us', 52, 4), ('known', 56, 1),
    ('values', 57, 1), ('sats', 58, 1), ('source_kind', 59, 1),
    ('rmc_age_ms', 60, 2), ('pps_age_ms', 62, 2), ('gga_age_ms', 64, 2),
    ('priority1', 66, 1), ('clock_class', 67, 1), ('accuracy', 68, 1),
    ('priority2', 69, 1), ('variance', 70, 2), ('hdop_centi', 72, 2), ('tag', 74, 16),
]


def decode(wire, key, group_id, key_epoch):
    """Independent bounded decoder; receipt/replay state belongs to clients."""
    legacy = len(wire) == 88
    if not legacy and len(wire) != 90:
        raise ValueError('record length')
    if wire[:8] != bytes([87 if legacy else 89, 255, 255, 255, 71, 68, 1 if legacy else 2, 0]):
        raise ValueError('header/version/reserved')
    group = struct.unpack_from('<QI', wire, 8)
    if group != (group_id, key_epoch) or not group_id or not key_epoch:
        raise ValueError('group')
    if len(key) != 32 or not any(key):
        raise ValueError('key')
    context = struct.pack('<BQI', 1, group_id, key_epoch)
    body_size = 72 if legacy else 74
    domain = b'R2GNDG01' if legacy else b'R2GNDG02'
    expected = hmac.new(key, domain + context + wire[:body_size], hashlib.sha256).digest()[:16]
    if not hmac.compare_digest(wire[body_size:], expected):
        raise ValueError('authentication')
    root, boot, mesh, sequence, sampled, valid = struct.unpack_from('<QQIIQI', wire, 20)
    if not all((root, boot, mesh, sequence, sampled, valid)) or valid > 30000000:
        raise ValueError('zero scope or lifetime')
    if sampled + valid > 2**64 - 1:
        raise ValueError('snapshot lifetime overflow')
    known, values, sats, kind = wire[56:60]
    if kind != 1 or known & ~(31 if legacy else 63) or values & ~7 or values & ~known:
        raise ValueError('flags/source kind')
    rmc, pps, gga = struct.unpack_from('<HHH', wire, 60)
    if values & 1 and rmc >= 2500:
        raise ValueError('true fix age')
    if values & 2 and pps >= 1500:
        raise ValueError('true PPS age')
    if values & 4 and values & 3 != 3:
        raise ValueError('qualification observation consistency')
    if known & 8:
        if sats > 99 or gga >= 4000:
            raise ValueError('known satellites')
    elif sats != 255:
        raise ValueError('unknown satellites padding')
    hdop = 65535 if legacy else int.from_bytes(wire[72:74], 'little')
    if known & 32:
        if hdop == 65535 or gga >= 4000:
            raise ValueError('known HDOP')
    elif hdop != 65535:
        raise ValueError('unknown HDOP padding')
    quality = (*wire[66:70], int.from_bytes(wire[70:72], 'little'))
    if not known & 16 and quality != (128, 248, 254, 128, 65535):
        raise ValueError('unknown PTP padding')
    return {'root': root, 'boot': boot, 'mesh': mesh, 'sequence': sequence,
            'sampled_us': sampled, 'valid_us': valid, 'known': known,
            'values': values, 'sats': sats, 'source_kind': kind,
            'ages_ms': [rmc, pps, gga], 'quality': list(quality), 'hdop_centi': hdop}


def main():
    directory = ROOT / 'protocol/gm_diagnostics_v2'
    contract = json.loads((directory / 'contract.json').read_text())
    vectors = json.loads((directory / 'vectors.json').read_text())
    document = (ROOT / contract['specification']).read_text()
    assert contract['id'] == vectors['profile'] == 'V14-GD2'
    assert contract['byte_order'] == 'little' and contract['wire_version'] == 2
    assert contract['wire']['bytes'] == 90 and contract['wire']['body_bytes'] == 74
    assert contract['wire']['domain'] == 'R2GNDG02'
    fields = contract['wire']['fields']
    assert [(f['name'], f['offset'], f['bytes']) for f in fields] == LAYOUT
    occupied = [offset for f in fields for offset in range(f['offset'], f['offset'] + f['bytes'])]
    assert occupied == list(range(90))
    for field in fields:
        start, size = field['offset'], field['bytes']
        offset = str(start) if size == 1 else f'{start}–{start + size - 1}'
        row = f"| {offset} | {size} | `{field['name']}` | {field['meaning']} |"
        assert row in document, field['name']
    assert contract['known_bits'] == {'fix': 0, 'pps': 1, 'source_qualified': 2,
                                      'satellites': 3, 'ptp_dataset': 4, 'hdop': 5}
    assert contract['value_bits'] == {'fix': 0, 'pps': 1, 'source_qualified': 2}
    assert contract['unknown'] == {'age_ms': 65535, 'satellites': 255,
                                   'ptp_tuple': [128, 248, 254, 128, 65535], 'hdop_centi': 65535}
    assert contract['age_saturated_ms'] == 65534
    assert contract['scheduler']['owner_update_min_us'] == 1000000
    assert contract['scheduler']['gd_next_due_after_success_us'] == 2000000
    assert contract['scheduler']['diagnostic_cache_max_us'] == 30000000
    assert contract['scheduler']['controller_data_ceiling_bytes'] == 191
    assert contract['scheduler']['additional_advertisers'] == 0
    assert contract['authority'] is False and vectors['synthetic'] is True
    assert vectors['hardware'] is False
    assert contract['status'] == 'EXPERIMENTAL_IMPLEMENTATION_WITH_BOUNDED_FIELD_OBSERVATION'
    assert contract['legacy_decoder'] == {
        'wire_version': 1, 'bytes': 88, 'body_bytes': 72, 'domain': 'R2GNDG01',
        'known_mask': 31, 'hdop': 'unavailable; known bit clear and hdop_centi65535', 'encode': False}
    cache = contract['descriptive_cache']
    assert cache['same_selected_scope_required_for_current']
    assert cache['history_allowed_when_selection_matches_or_is_entirely_empty']
    assert cache['entirely_empty_selection'] == 'root=0,root_boot=0,mesh_epoch=0'
    assert cache['empty_selection_label'] == 'LAST GM / CACHED plus original receipt age'
    assert cache['any_other_nonempty_selection_hides_history']
    assert not cache['receipt_or_deadline_refresh_on_replay_or_reselection'] and not cache['timing_authority']
    assert contract['display_quality'] == {
        'accuracy_unknown_raw': 254, 'accuracy_unknown_label': 'ACC UNKNOWN',
        'variance_unreported_raw': 65535, 'variance_unreported_label': 'VAR NONE',
        'sentinel_is_measured_zero': False}
    field = contract['field_validation']
    assert field['summary_license'] == 'CC-BY-4.0' and field['credit'] == 'Jonathan Yantis'
    receiver = field['receiver']
    assert receiver['samples'] == receiver['current_selected_samples'] + receiver['empty_selection_last_GM_history_samples'] == 177
    assert receiver['distinct_GD2_reports'] == 61 and receiver['expired_report_samples'] == 0
    assert receiver['last_sequence'] - receiver['first_sequence'] + 1 == receiver['distinct_GD2_reports'] + receiver['unobserved_sequence_labels_in_inclusive_range'] == 105
    assert receiver['RF_packet_loss_rate'] is None
    joins = field['reported_HDOP_source_join']
    assert joins['received_reports'] == joins['same_GGA_event_exact_HDOP_and_SV_matches'] + joins['unmatched_reports'] == 61
    assert joins['same_GGA_event_exact_HDOP_and_SV_matches'] == 20
    previous = field['retained_failures']['prior_Core018']
    assert previous['samples'] == previous['current_diagnostic_samples'] + previous['empty_selected_scope_samples'] + previous['expired_diagnostic_samples'] == 150
    assert previous['freshness_result'] == 'INCOMPLETE' and previous['expired_diagnostic_samples'] == 3
    key = bytes.fromhex(vectors['test_key_hex'])
    assert key == bytes([1]) + bytes(31), 'Only the public synthetic key belongs here'
    group_id, key_epoch = vectors['group_id'], vectors['key_epoch']
    assert bytes.fromhex(vectors['context_hex']) == struct.pack('<BQI', 1, group_id, key_epoch)
    ids = set()
    accepted = refused = 0
    baselines = []
    for vector in vectors['vectors']:
        assert vector['id'] not in ids
        ids.add(vector['id'])
        wire = bytes.fromhex(vector['wire_hex'])
        try:
            decoded = decode(wire, key, group_id, key_epoch)
            result = True
        except ValueError:
            result = False
        assert result is vector['accept'], vector['id']
        if result:
            accepted += 1
            if vector['id'] in ('reported_gnss_qualified', 'legacy_reported_gnss_qualified'):
                baselines.append(wire)
                assert decoded['quality'] == [128, 248, 254, 128, 65535]
                assert decoded['known'] == (31 if len(wire) == 88 else 63)
                assert decoded['hdop_centi'] == (65535 if len(wire) == 88 else 123)
                assert decoded['values'] == 7 and decoded['sats'] == 12
        else:
            refused += 1
    assert len(baselines) == 2 and accepted >= 9 and refused >= 30
    # Every input bit is authenticated, including all AD framing bytes.
    mutations = 0
    for baseline in baselines:
        for offset in range(len(baseline)):
            for bit in range(8):
                wire = bytearray(baseline)
                wire[offset] ^= 1 << bit
                try:
                    decode(bytes(wire), key, group_id, key_epoch)
                except ValueError:
                    mutations += 1
                else:
                    raise AssertionError(('mutation accepted', offset, bit))
    assert mutations == 1424
    print(json.dumps({'status': 'PASS_GD2_AND_LEGACY_DOCUMENT_VECTORS_ONLY',
                      'contract_fields': len(fields), 'accepted_vectors': accepted,
                      'rejected_vectors': refused, 'rejected_bit_mutations': mutations,
                      'recorded_field_summary': {'received_reports': receiver['distinct_GD2_reports'],
                           'expired_report_samples': receiver['expired_report_samples'],
                           'empty_selection_history_samples': receiver['empty_selection_last_GM_history_samples'],
                           'same_GGA_event_joins': joins['same_GGA_event_exact_HDOP_and_SV_matches']},
                      'field_extraction_replayed_by_this_check': False,
                      'hardware_access': False, 'accuracy_qualified': False}, indent=2))


if __name__ == '__main__':
    main()

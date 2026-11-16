#!/usr/bin/env python3
"""Bounded watch-only RC1/direct/RC1 capture policy and observational report.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""

def _status(frame):
    row = frame['rows'].get('W12_R2_STATUS', {})
    return row if 'client_running' in row else None


def _scope(row):
    return tuple(row.get(k) for k in ('root_identity', 'root_boot', 'source_epoch'))


def _depth(row):
    fields = row.get('rc7', '').split('/')
    if len(fields) != 4 or fields[0] != '2':
        raise RuntimeError('Unknown authenticated RC7 status schema')
    return int(fields[3]) if fields[2] == '1' else 0


def auto_restored(frame):
    row = _status(frame)
    fields = row.get('rp', '').split('/') if row else []
    return len(fields) == 6 and fields[0] == '0000000000000000' and fields[2] == '3'


class BidirectionalRoaming:
    """Actions need a fresh Console V3 ticket; this class owns no transport."""
    def __init__(self, first, relay):
        row = _status(first)
        if not row or row.get('following') != '1' or _depth(row) != 0:
            raise RuntimeError('Fresh following direct GM required before the roaming comparison')
        self.scope = _scope(row)
        self.gm = self.scope[0]
        if not all(self.scope) or not all(int(v, 16 if i < 2 else 10) for i, v in enumerate(self.scope)) or self.gm != row.get('joined_peer') or self.gm == relay:
            raise RuntimeError('Current direct GM identity/scope is unavailable or ambiguous')
        self.plan = [('RC1', relay, 1), ('BLE', self.gm, 0), ('RC1', relay, 1)]
        self.started = first['capture_elapsed_s']
        self.legs = []
        self.stage = 'BASELINE'
        self.pending = None
        self.done = False

    def action(self, frame):
        row = _status(frame)
        if not row:
            return None
        at = frame['capture_elapsed_s']
        if row.get('requested') != '1' or row.get('client_running') != '1' or row.get('fault') != '0':
            raise RuntimeError('BLE owner stopped or faulted during the roaming comparison')
        if row.get('following') == '1' and _scope(row) != self.scope:
            raise RuntimeError('Authenticated root/boot/source changed; incompatible cohorts refused')
        if self.pending or self.done:
            return None
        if self.stage == 'RESTORING':
            if auto_restored(frame):
                self.done = True
                self.stage = 'DONE'
            elif at >= self.restore_deadline:
                raise RuntimeError('AUTO owner restoration did not complete within3s')
            return None
        if self.stage == 'BASELINE' and at-self.started < 30:
            return None
        if self.stage == 'ADMIT':
            leg = self.legs[-1]
            if row.get('following') == '1' and row.get('joined_peer') == leg['peer'] and _depth(row) == leg['depth']:
                leg['admitted_s'] = at
                leg['admission_latency_s'] = at-leg['requested_s']
                self.stage = 'OBSERVE'
            elif at >= leg['admission_deadline_s']:
                raise RuntimeError('Requested authenticated parent not observed within45s; unavailable source retained as failure')
            return None
        if self.stage == 'OBSERVE':
            leg = self.legs[-1]
            if at-leg['admitted_s'] < 60:
                return None
            leg['observed_until_s'] = at
        if len(self.legs) == len(self.plan):
            self.pending = 'rc-parent auto'
        else:
            kind, peer, _ = self.plan[len(self.legs)]
            self.pending = ('gm-parent ' if kind == 'BLE' else 'rc-parent ')+peer
        return self.pending

    def acknowledged(self, frame):
        command = frame['rows'].get('W12_V12_COMMAND', {})
        if not self.pending or command.get('accepted') != '1':
            raise RuntimeError('Watch parent selection was refused')
        at = frame['capture_elapsed_s']
        if self.pending == 'rc-parent auto':
            self.stage = 'RESTORING'
            self.restore_deadline = at+3
        else:
            kind, peer, depth = self.plan[len(self.legs)]
            self.legs.append({'kind': kind, 'peer': peer, 'depth': depth, 'action': self.pending,
                             'requested_s': at, 'admission_deadline_s': at+45})
            self.stage = 'ADMIT'
        self.pending = None


def report(frames, controller):
    """Sampled firmware diagnostics, never independent UTC/output accuracy."""
    if controller is None:
        return None
    result = {'status': 'COMPLETE' if controller.done else 'INCOMPLETE',
              'scope': 'SAMPLED_FIRMWARE_DIAGNOSTICS_NOT_INDEPENDENT_ACCURACY',
              'root': controller.scope[0], 'root_boot': controller.scope[1],
              'source_epoch': controller.scope[2], 'legs': []}
    for index, leg in enumerate(controller.legs):
        end = controller.legs[index+1]['requested_s'] if index+1 < len(controller.legs) else (frames[-1]['capture_elapsed_s'] if frames else leg['requested_s'])
        samples = [f for f in frames if leg['requested_s'] <= f['capture_elapsed_s'] <= end]
        ui = [(f['capture_elapsed_s'], f['rows']['TWATCH_UI']) for f in samples if 'TWATCH_UI' in f['rows']]
        dual = [(f['capture_elapsed_s'], f['rows']['TWATCH_SOURCE']) for f in samples if 'TWATCH_SOURCE' in f['rows']]
        item = dict(leg)
        item['includes_admission_hold'] = True
        item['time_missing_samples'] = sum(u.get('clock') != '1' for _, u in ui)
        item['display_hold_sampled_s'] = sum(b[0]-a[0] for a, b in zip(ui, ui[1:]) if a[1].get('local') == '1')
        item['display_backward_steps'] = sum(int(b[1].get('utc_us', 0)) < int(a[1].get('utc_us', 0))
                                             for a, b in zip(ui, ui[1:]) if int(b[1].get('sampled_us', 0)) > int(a[1].get('sampled_us', 0)))
        item['selected_source_transitions'] = []
        for at, u in ui:
            source = u.get('source')
            if not item['selected_source_transitions'] or source != item['selected_source_transitions'][-1]['source']:
                item['selected_source_transitions'].append({'at_s': at, 'source': source})
        for key in ('servo_trained', 'p99_ready'):
            first = next((at for at, d in dual if d.get(key) == '1' and at >= leg.get('admitted_s', float('inf'))
                          and d.get('p99_root') == controller.scope[0] and d.get('p99_source') == controller.scope[2]
                          and d.get('p99_parent') == (leg['peer'] if leg['depth'] else '0000000000000000')), None)
            item['first_'+key+'_latency_s'] = None if first is None else first-leg['requested_s']
        for key in ('servo_phase_ns', 'servo_rate_mppb', 'servo_raw_rms_ns', 'servo_max_ns', 'servo_accepted',
                    'servo_refused', 'servo_samples', 'servo_fit_points', 'p99_ms', 'p99_trials', 'p99_resets'):
            raw = [(at, int(d[key])) for at, d in dual if key in d]
            unknown = -2147483648 if key == 'servo_rate_mppb' else 4294967295 if key in ('servo_raw_rms_ns', 'servo_max_ns') else None
            values = [(at, value) for at, value in raw if value != unknown]
            item[key] = {'first': values[0][1], 'last': values[-1][1], 'min': min(v for _, v in values),
                         'max': max(v for _, v in values), 'known_samples': len(values), 'unknown_samples': len(raw)-len(values)} if values else {'known_samples': 0, 'unknown_samples': len(raw)}
        item['samples'] = {'display': len(ui), 'servo': len(dual)}
        result['legs'].append(item)
    return result

"""Bounded timing history; integer ns, explicit gaps. Jonathan Yantis. MIT."""
import csv
from decimal import Decimal, InvalidOperation
import io
import json
import math
from pathlib import Path
import sqlite3
import time

METRICS = ('source_offset_ns', 'source_measured_ns', 'source_stddev_ns',
           'tracking_offset_ns', 'tracking_rms_ns', 'system_offset_ns',
           'fit_rms_ns', 'fit_drift_ppb', 'fit_age_ms', 'pps_phase_ns',
           'pps_period_ns', 'query_ms', 'uart_age_ms')
REPORTED_METRICS = ('source_offset_ns', 'source_measured_ns', 'source_stddev_ns',
                    'tracking_offset_ns', 'tracking_rms_ns', 'system_offset_ns')
# Preserve the old display-only readiness annotations in exported history.
# They never authorize timing, and must not blank already recorded estimates.
LEGACY_REPORT_FIELDS = ('report_policy', 'report_ready', 'report_state', 'report_reason',
                        'report_elapsed_s', 'report_required_s', 'report_samples', 'report_edges')
WINDOWS = (60, 300, 1800, 7200, 21600)


def ns(value):
    """Preserve ns at epoch-sized seconds as well as small decimal offsets."""
    d = Decimal(str(value))
    if not d.is_finite():
        raise ValueError('non-finite time')
    return int((d*1_000_000_000).to_integral_value())


def tracking(text):
    try:
        r = next(csv.reader(io.StringIO(text)))
        if len(r) != 14:
            return {}
        return dict(reference=r[1], stratum=int(r[2]), reference_time=r[3],
                    system_offset_ns=ns(r[4]), tracking_offset_ns=ns(r[5]),
                    tracking_rms_ns=ns(r[6]), frequency_ppm=float(r[7]),
                    skew_ppm=float(r[9]), leap=r[13])
    except (IndexError, ValueError, InvalidOperation, StopIteration):
        return {}


def source_stats(text):
    rows = []
    for r in csv.reader(io.StringIO(text)):
        try:
            rows.append(dict(name=r[0], samples=int(r[1]), runs=int(r[2]), span_s=int(r[3]),
                             frequency_ppm=float(r[4]), skew_ppm=float(r[5]),
                             estimated_offset_ns=ns(r[6]), stddev_ns=ns(r[7])))
        except (IndexError, ValueError, InvalidOperation):
            continue
    return rows


def sample(state, query_ms):
    receiver = state['receiver']; rx = receiver.get('data') or {}
    pps = state['pps']; fit = rx.get('fit') or {}
    origin = rx.get('clock_source') or {}
    acquisition = rx.get('acquisition') or []
    parent = (f'{acquisition[2]:08x}{acquisition[1]:08x}' if len(acquisition) == 20 else None)
    source = next((s for s in state['sources'] if s['name'] == 'PTOB' and s['kind'] == '#'), {})
    stat = next((s for s in state['source_stats'] if s['name'] == 'PTOB'), {})
    track = state['tracking']
    # A scope change breaks lines. Unknown observations remain null, never zero.
    context = json.dumps([rx.get('source'), rx.get('boot'), rx.get('path'),
                          (state['chain'].get('selected') or {}).get('name'),
                          state['chain']['admitted'], origin.get('root'), origin.get('root_boot'),
                          origin.get('epoch'), parent], separators=(',', ':'))
    # LastRx is the age of chrony's measurement, not the age of the GPIO edge.
    # Filtered refclocks submit less often and timestamp the middle of their
    # sample window. Keep the actual reported estimate between updates, with
    # its age, while real PPS remains fresh. Never invent a new measurement.
    try:
        reachable = int(str(source.get('reach', '0')), 8) > 0
    except ValueError:
        reachable = False
    live_source = bool(reachable and pps.get('fresh') and state['chain']['admitted'])
    receiver_live = receiver.get('fresh') and not receiver.get('error')
    if not receiver_live:
        reason = 'Receiver telemetry unavailable'
    elif not state['chain']['admitted']:
        reason = 'Receiver clock offer withdrawn'
    elif not pps.get('fresh'):
        reason = 'PPS paused; no current edge'
    elif not state['chain'].get('complete'):
        reason = 'PPS present; PTOB not selected'
    else:
        reason = 'Live admitted PPS'
    normal_tracking = track.get('leap') == 'Normal'
    tracking_live = normal_tracking and (track.get('reference') != 'PTOB' or
                                         (live_source and state['chain'].get('complete')))
    poll = source.get('poll')
    result = dict(at_ms=time.time_ns()//1_000_000,
                  mono_ms=int(time.clock_gettime(time.CLOCK_BOOTTIME)*1000), context=context,
                  selected=(state['chain'].get('selected') or {}).get('name'),
                  receiver_boot=rx.get('boot'), pps_sequence=pps.get('sequence'),
                  receiver_uptime_ms=rx.get('uptime_ms'), parent=parent,
                  lineage_schema=1, gm_admitted=state['chain']['admitted'],
                  gm_root=origin.get('root'), gm_boot=origin.get('root_boot'), gm_epoch=origin.get('epoch'),
                  discovery_context=(rx.get('discovery') or {}).get('context'),
                  metric_schema=3, pps_interval_ns=pps.get('interval_ns'), pps_gap=pps.get('gap_interval'),
                  pps_fresh=bool(pps.get('fresh')), pps_age_s=pps.get('age_s'),
                  pps_observed_interval_s=pps.get('observed_interval_s'),
                  pps_skipped_edges=pps.get('skipped_edges'),
                  receiver_following=rx.get('following'), receiver_clock_valid=rx.get('clock_valid'),
                  receiver_offer_valid=rx.get('offer_valid'), timing_reason=reason,
                  tracking_reference=track.get('reference'),
                  tracking_mode='live' if tracking_live else 'holdover' if normal_tracking else 'unavailable',
                  source_mode='live' if live_source else 'holdover' if reachable else 'unavailable',
                  source_sample_age_s=source.get('age_s'), source_state=source.get('state'),
                  source_poll_s=2**poll if type(poll) is int and -7 <= poll <= 24 else None,
                  source_fit_samples=stat.get('samples'),
                  complete=state['chain']['complete'],
                  query_ms=round(query_ms, 3), uart_age_ms=round(receiver['age_s']*1000, 3) if receiver.get('age_s') is not None else None,
                  source_offset_ns=source.get('offset_ns') if live_source else None,
                  source_measured_ns=source.get('measured_offset_ns') if live_source else None,
                  source_stddev_ns=stat.get('stddev_ns') if live_source else None,
                  pps_phase_ns=pps.get('phase_ns') if pps.get('fresh') else None,
                  pps_period_ns=pps.get('period_error_ns') if pps.get('fresh') else None,
                  fit_rms_ns=fit.get('rms_ns') if receiver_live and fit.get('rms_valid') else None,
                  fit_drift_ppb=fit.get('drift_ppb') if receiver_live and fit.get('drift_valid') else None,
                  fit_age_ms=fit.get('age_ms') if receiver_live and fit.get('age_valid') else None)
    # A successful chrony query still reports an estimate during PPS holdover.
    # Keep that observation separately; it is not a new admitted PPS sample and
    # must not enter the live timing population or be used to fill an old null.
    for key, value in (('source_offset_ns', source.get('offset_ns')),
                       ('source_measured_ns', source.get('measured_offset_ns')),
                       ('source_stddev_ns', stat.get('stddev_ns'))):
        result['reported_'+key] = value if reachable else None
    for key in ('system_offset_ns', 'tracking_offset_ns', 'tracking_rms_ns'):
        result[key] = track.get(key) if tracking_live else None
        result['reported_'+key] = track.get(key) if normal_tracking else None
    return result


def receiver_scope(row, require_known=False):
    """Admission/selection flags are availability, not a new receiver clock."""
    try:
        context = json.loads(row['context'])
        if isinstance(context, list) and len(context) == 9:
            if require_known and any(context[i] is None for i in (0, 1, 2, 5, 6, 7, 8)):
                return None
            return json.dumps(context[:3]+context[5:]+[row.get('discovery_context')], separators=(',', ':'))
    except (ValueError, TypeError, KeyError):
        pass
    return row.get('context')


class History:
    def __init__(self, path):
        self.path = Path(path)
        self.db = sqlite3.connect(path)
        self.db.execute('PRAGMA journal_mode=WAL')
        self.db.execute('PRAGMA synchronous=NORMAL')
        self.db.execute('PRAGMA wal_autocheckpoint=64')
        self.db.execute('CREATE TABLE IF NOT EXISTS samples (id INTEGER PRIMARY KEY, at_ms INTEGER, payload TEXT)')
        self.db.execute('CREATE INDEX IF NOT EXISTS samples_time ON samples(at_ms)')
        self.db.commit()
        self.pending, self.flush_at = 0, time.monotonic()+10
        self.path.chmod(0o644)

    def add(self, record):
        self.db.execute('INSERT INTO samples(at_ms,payload) VALUES (?,?)',
                        (record['at_ms'], json.dumps(record, allow_nan=False, separators=(',', ':'))))
        self.pending += 1
        if time.monotonic() >= self.flush_at:
            # Bound by population even if UTC steps backward. Keep about 24 h at 1 Hz.
            self.db.execute('DELETE FROM samples WHERE id <= (SELECT coalesce(max(id),0)-86400 FROM samples)')
            self.db.commit()
            self.pending, self.flush_at = 0, time.monotonic()+10
            # Read-only web service can see the WAL through a traversable state dir.
            for suffix in ('-wal', '-shm'):
                p = Path(str(self.path)+suffix)
                if p.exists(): p.chmod(0o644)

    def close(self):
        self.db.commit(); self.db.close()


def statistics(values):
    values = [v for v in values if isinstance(v, (int, float)) and math.isfinite(v)]
    if not values:
        return dict(n=0)
    n = len(values); mean = math.fsum(values)/n
    absolute = sorted(abs(v) for v in values)
    return dict(n=n, mean=mean, rms=math.sqrt(math.fsum(v*v for v in values)/n),
                stddev=math.sqrt(math.fsum((v-mean)**2 for v in values)/n),
                p95_abs=absolute[max(0, math.ceil(.95*n)-1)],
                p99_abs=absolute[max(0, math.ceil(.99*n)-1)], min=min(values), max=max(values))


def averaging(rows):
    """Non-overlapping means of unique contiguous observed PPS edges.

    No sqrt(N) accuracy/SEM claim: servo, reference and IRQ errors correlate.
    Missing snapshot edges break blocks rather than being interpolated.
    """
    groups, group, previous = [], [], None
    for r in rows:
        seq = r.get('pps_sequence')
        valid = r.get('complete') and r.get('pps_phase_ns') is not None and seq is not None
        if valid and previous and seq == previous['pps_sequence'] and r['context'] == previous['context'] and r.get('segment') == previous.get('segment'):
            continue
        if (not valid or (previous and (seq != previous['pps_sequence']+1 or
                r['context'] != previous['context'] or r.get('segment') != previous.get('segment') or r.get('pps_gap')))):
            if group: groups.append(group)
            group, previous = [], None
        if valid:
            group.append(r['pps_phase_ns']); previous = r
    if group: groups.append(group)
    values = [v for g in groups for v in g]
    pairs = [(a,b) for g in groups for a,b in zip(g,g[1:])]
    ma = math.fsum(a for a,b in pairs)/len(pairs) if pairs else 0
    mb = math.fsum(b for a,b in pairs)/len(pairs) if pairs else 0
    denom = math.sqrt(math.fsum((a-ma)**2 for a,b in pairs)*math.fsum((b-mb)**2 for a,b in pairs))
    lag1 = math.fsum((a-ma)*(b-mb) for a,b in pairs)/denom if denom else None
    blocks = []
    for length in (1,2,4,8,16,32,64):
        means = [math.fsum(g[i:i+length])/length for g in groups
                 for i in range(0,len(g)-length+1,length)]
        blocks.append(dict(seconds=length, used_edges=len(means)*length, **statistics(means)))
    return dict(unique_edges=len(values), segments=len(groups), lag1=lag1, blocks=blocks,
                basis='unique contiguous observed GPIO PPS edges while the complete path is valid; no interpolation')


def read(path, seconds, export=False):
    if seconds not in WINDOWS:
        raise ValueError('unsupported window')
    db = sqlite3.connect('file:'+str(Path(path).resolve())+'?mode=ro', uri=True, timeout=1)
    try:
        latest = db.execute('SELECT at_ms FROM samples ORDER BY id DESC LIMIT 1').fetchone()
        rows = [] if not latest else [json.loads(r[0]) for r in db.execute(
            'SELECT payload FROM samples WHERE at_ms>=? ORDER BY id', (latest[0]-seconds*1000,))]
    finally:
        db.close()
    # Earlier schema-1 snapshots called a withdrawn 19-second interval a
    # period error. Preserve that interval explicitly and break its jitter line.
    for row in rows:
        if row.get('metric_schema', 1) == 1 and row.get('pps_period_ns') is not None:
            interval = row['pps_period_ns']+1_000_000_000
            row['pps_interval_ns'] = interval
            row['pps_gap'] = not 500_000_000 < interval < 1_500_000_000
            if row['pps_gap']: row['pps_period_ns'] = None
    if export:
        return rows
    # Old snapshots retain the reported estimate during holdover in CSV. Do
    # not let a frozen last estimate masquerade as improving live jitter.
    for row in rows:
        if row.get('selected') == 'PTOB' and (row.get('pps_phase_ns') is None or row.get('gm_admitted') is False):
            for keys, group in ((('system_offset_ns','tracking_offset_ns','tracking_rms_ns'),'tracking'),
                                (('source_offset_ns','source_measured_ns','source_stddev_ns'),'source')):
                for key in keys:
                    if row.get(key) is not None:
                        row.setdefault('reported_'+key,row[key])
                    row[key] = None
                if any(row.get('reported_'+key) is not None for key in keys):
                    row[group+'_mode'] = 'holdover'
    stats = {k: statistics([r.get(k) for r in rows]) for k in METRICS}
    gaps, segment = 0, 0
    scopes, counters = {}, dict(receiver=0, source=0, tracking=0, pps=0)
    health = dict(source_changes=0, collection_breaks=0, availability_changes=0,
                  pps_unavailable_snapshots=sum(r.get('pps_phase_ns') is None for r in rows),
                  pps_gap_events=0, nominal_missing_pps_slots=0)
    seen_pps = set()
    last_known_scope = None
    for i, row in enumerate(rows):
        hard_break = False
        scope = receiver_scope(row)
        known_scope = receiver_scope(row, require_known=True)
        # Collector startup and missing telemetry are unknown observations,
        # not evidence that the receiver changed clocks. Preserve their line
        # breaks, but compare only known scopes when counting source changes.
        if known_scope is not None:
            if last_known_scope is not None:
                health['source_changes'] += known_scope != last_known_scope
            last_known_scope = known_scope
        if i:
            previous = rows[i-1]
            hard_break = (row['mono_ms']-previous['mono_ms'] > 2500 or row['mono_ms'] <= previous['mono_ms'] or
                          row['at_ms'] <= previous['at_ms'] or
                          abs((row['at_ms']-previous['at_ms'])-(row['mono_ms']-previous['mono_ms'])) > 100)
            if hard_break or previous['context'] != row['context']:
                gaps += 1; segment += 1
            health['collection_breaks'] += hard_break
            health['availability_changes'] += row.get('complete') != previous.get('complete')
        row['segment'] = segment
        reference = row.get('tracking_reference', row.get('selected'))
        current = dict(receiver=scope, source=scope, pps=scope,
                       tracking=(reference, scope if reference == 'PTOB' else None))
        for key, value in current.items():
            if i and (hard_break or value != scopes[key]):
                counters[key] += 1
        row['series_segments'] = counters.copy()
        scopes = current
        if not row.get('timing_reason'):
            row['timing_reason'] = ('Receiver clock offer withdrawn' if row.get('gm_admitted') is False else
                                    'PPS paused; no current edge' if row.get('pps_phase_ns') is None else
                                    'Live admitted PPS' if row.get('complete') else 'Timing path unavailable')
        seq = (row.get('receiver_boot'), row.get('pps_sequence'))
        if row.get('pps_gap') and seq not in seen_pps:
            seen_pps.add(seq); health['pps_gap_events'] += 1
            # This is a nominal count from a real-time timestamp interval, not
            # independent evidence distinguishing missing pulses from a step.
            interval = row.get('pps_interval_ns')
            if isinstance(interval, (int, float)) and interval > 1_500_000_000:
                health['nominal_missing_pps_slots'] += max(0, round(interval/1_000_000_000)-1)
    averages = averaging(rows)
    # Retain extrema of EVERY metric within each bucket. No average-only decimation.
    chosen = set()
    size = max(1, math.ceil(len(rows)/100))
    for start in range(0, len(rows), size):
        indices = range(start, min(start+size, len(rows)))
        chosen.update((start, min(start+size, len(rows))-1))
        for key in (*METRICS, *('reported_'+k for k in REPORTED_METRICS)):
            valid = [i for i in indices if rows[i].get(key) is not None]
            if valid:
                chosen.add(min(valid, key=lambda i: rows[i][key])); chosen.add(max(valid, key=lambda i: rows[i][key]))
        for i in indices:
            if i and (rows[i]['segment'] != rows[i-1]['segment'] or
                      rows[i].get('tracking_mode') != rows[i-1].get('tracking_mode') or
                      rows[i].get('source_mode') != rows[i-1].get('source_mode') or
                      any((rows[i].get(k) is None) != (rows[i-1].get(k) is None) for k in METRICS)):
                chosen.update((i-1, i))
    return dict(schema=1, window_s=seconds, population=len(rows), points=[rows[i] for i in sorted(chosen)],
                statistics=stats, averaging=averages, gaps=gaps, timing_health=health,
                held_estimates={k:sum(r.get(k) is None and r.get('reported_'+k) is not None for r in rows) for k in REPORTED_METRICS},
                decimation='per-metric extrema retained; statistics use live 1 Hz snapshots; separately reported holdover estimates are excluded',
                newest_ms=rows[-1]['at_ms'] if rows else None,
                newest_mono_ms=rows[-1]['mono_ms'] if rows else None,
                cadence='1 Hz snapshots; chrony and fit estimates can repeat between updates')

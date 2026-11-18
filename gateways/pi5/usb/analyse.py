"""Offline replay of paired USB/control evidence. Copyright 2026 Jonathan Yantis. MIT."""
from collections import Counter
import bisect
import copy
from decimal import Decimal
from .contract import decode, scope
from .model import SourceSeries, HostSeries, coarse_interval, statistics,spec_frame_ordinal,MarkerIdentity,CaptureContinuity
from .calibration import DEFAULT,metadata as calibration_metadata,corrected_utc,corrected_phase
from .local_diagnostic import margins as local_margins
from .clock_pair import realtime_interval,snapshot_summary


def hat_status(s):
    status=s['status'];receiver=status.get('receiver') or {};data=receiver.get('data') or {}
    q=status.get('source_status') or {};observed=status.get('observed_boottime_s')
    age=s['before']['boottime_ns']/1e9-observed if type(observed) in (int,float) else None
    context=tuple(q.get(k) for k in ('root','root_boot','epoch','parent','context'))
    eligible=bool(age is not None and 0<=age<=3 and receiver.get('fresh') and not receiver.get('error') and
                  all(type(data.get(k)) is int and data[k]==0 for k in ('error','receiver_error','capture_error','uart_error')) and
                  data.get('following') and data.get('clock_valid') and data.get('offer_valid') and q.get('current') and
                  all(v is not None for v in context) and status.get('pps',{}).get('fresh') and status.get('chain',{}).get('complete'))
    return dict(boottime_ns=s['before']['boottime_ns'],eligible=eligible,context=context,
                root=q.get('root'),root_boot=q.get('root_boot'),epoch=q.get('epoch'),current=q.get('current'),age_s=age)


def settlement_ready(raw,a):
    """Select the next fixed window by continuity, never by error magnitude.

    Readiness does not qualify timing, promote a source, or silently extend a
    failed window. All readiness captures remain separate evidence populations.
    """
    reasons=[];n=a['attempts'].get('attempted',0);g=a['simultaneous_GPIO']
    if raw['elapsed_monotonic_ns']<60_000_000_000 or n<raw['rate']*59:reasons.append('READINESS_SHORTER_THAN_60S')
    if raw['errors'] or not raw['interface_released']:reasons.append('CAPTURE_OR_RELEASE_FAILURE')
    if not n or a['attempts'].get('source_valid')!=n or a['source']['counts'].get('accepted')!=n:
        reasons.append('USB_SOURCE_NOT_CONTINUOUSLY_ADMITTED')
    if g['status_observations']<59 or g['eligible_status_observations']!=g['status_observations']:
        reasons.append('MHAT_NOT_CONTINUOUSLY_ADMITTED')
    if not 59<=g['new_edges']['n']<=61 or g['source_eligible_edges']['n']<58 or g['gaps']:
        reasons.append('MHAT_PPS_CONTINUITY_NOT_ESTABLISHED')
    if a['sampled_lineage_join'].get('MATCHING_SAMPLED_GM_ROOT_BOOT_EPOCH')!=n:
        reasons.append('PAIRED_SOURCE_COHORT_NOT_CONTINUOUSLY_MATCHED')
    if not a['marker_identity']['integer_origin_constrained'] or a['marker_identity']['blocked']:
        reasons.append('INTEGER_FRAME_ORIGIN_NOT_CONSTRAINED')
    return dict(ready=not reasons,reasons=reasons,selection_uses_error_magnitude=False,
                means='eligible to begin one separately named fixed600s shadow comparison; not parity or accuracy qualification')


def current_observation(d,a,b):
    """Valid wire unavailability is distinct from a bad clock/map interval."""
    if not d['valid']:return dict(state='UNAVAILABLE',reason=d['reason'])
    try:return dict(state='COARSE_INTERVAL',interval=coarse_interval(d,a,b))
    except ValueError as e:return dict(state='WITHHELD',reason=str(e))


def analyse(raw,calibration=DEFAULT,host_filter=False):
    method='robust-host-v1' if host_filter is True else host_filter if host_filter else 'none'
    if method not in ('none','robust-host-v1','weighted-host-v2'):raise ValueError('unknown experimental host filter')
    enabled=method!='none';host_method=method if enabled else 'robust-host-v1'
    provisional=calibration_metadata(raw,calibration)
    expected=raw['device']['silicon'];source=SourceSeries();host=HostSeries(robust_enabled=enabled,robust_method=host_method);conditional_source=SourceSeries(host_reference=True);markers=MarkerIdentity();capture=CaptureContinuity()
    counts=Counter();rows=[];coarse=[];brackets=[];clock_widths=[];rejections=[];candidates=None
    previous_clocks=None;last_host=None;host_origin_segments=[];join=Counter();hat_contexts=[];conditional_offsets={};filtered_offsets={};local_counts=Counter();local_states=[]
    clock_pairs={};clock_pair_widths=[]
    prewarm=raw.get('host_preparation_warmup',{})
    if prewarm:
        if type(prewarm.get('source_requests')) is not int or prewarm['source_requests']!=0 or prewarm.get('errors'):
            raise ValueError('host preparation was not a complete host-only population')
        for burst in prewarm.get('bursts',[]):
            for r in burst['reads']:
                if type(r.get('after_raw_ns')) is not int or r['after_raw_ns']>raw['start']['raw_before_ns']:
                    raise ValueError('host preparation is not strictly preceding acquisition')
                host.observe(r)
    for sample in raw['samples']:
        counts['attempted']+=1;row=dict(index=sample['index']);c=sample['clocks']
        clock_widths.append(c['raw_after_ns']-c['raw_before_ns'])
        if previous_clocks:
            drift=(c['boottime_ns']-c['monotonic_ns'])-(previous_clocks['boottime_ns']-previous_clocks['monotonic_ns'])
            if abs(drift)>100_000:
                source.fence('HOST_SUSPEND_OR_CLOCK_BOUNDARY');host=HostSeries(robust_enabled=enabled,robust_method=host_method);candidates=None
                conditional_source.fence('HOST_SUSPEND_OR_CLOCK_BOUNDARY')
                row['host_boundary_reset']=True
        previous_clocks=c
        pre=None;post=None
        for r in sample.get('host_before',[]):pre=host.observe(r)
        marker_pre=pre;marker_post=None;marker_reference=copy.copy(host.transitions[-1]) if host.transitions else None
        for r in sample.get('host_marker_after',[]):marker_post=host.observe(r);pre=marker_post
        for name in ('source','current','raw_sof','estimate'):
            if name not in sample:continue
            transport=sample[name]
            if 'error' in transport:
                counts[name+'_transport_error']+=1
                if name=='source':source.fence('TRANSPORT_ERROR')
                if name=='source':conditional_source.fence('TRANSPORT_ERROR')
                continue
            a,b=transport['before'],transport['after'];span=b['raw_after_ns']-a['raw_before_ns']
            brackets.append(span)
            try:
                d=decode(bytes.fromhex(transport['wire_hex']),expected)
                counts[name+'_decoded']+=1
                counts[name+('_valid' if d['valid'] else '_unavailable')]+=1
                if name=='source':
                    row['source_record']=d;row['capture_continuity']=capture.observe(d,sample['index'])
                    row['source']=source.observe(d,a['raw_before_ns'],b['raw_after_ns'])
                    if row['source']['state']=='WITHHELD':counts['source_withheld']+=1
                elif name=='current':
                    row['current_record']=d;row['current']=current_observation(d,a,b)
                    if row['current']['state']=='COARSE_INTERVAL':
                        interval=row['current']['interval'];coarse.append(interval);row['coarse']=interval
                    elif row['current']['state']=='WITHHELD':
                        counts['current_interval_withheld']+=1
                        rejections.append(dict(index=sample['index'],request=name,stage='clock_or_original_permission',reason=row['current']['reason']))
                elif name=='estimate':
                    if d['contract']!='UTE1' or d['measurement']:raise ValueError('not a local-only UTE1 diagnostic')
                    row['local_estimate_record']=d
                    state=dict(index=sample['index'],scope=list(scope(d)),counter_valid=d['counter_valid'],
                        estimate_present=d['estimate_present'],session_current=d['session_current'],root_envelope_current=d['root_envelope_current'],
                        strict_identity_bit=d['valid'],tracking_state=d['tracking_state'],reason=d['reason'],estimate_reason=d['estimate_reason'],
                        tick=d['tick'],current_tick=d['current_tick'],last_discipline_tick=d['last_discipline_tick'],session_until_tick=d['session_until_tick'],
                        discipline_age_ticks=d['current_tick']-d['last_discipline_tick'] if d['estimate_present'] else None,
                        session_remaining_ticks=d['session_until_tick']-d['current_tick'] if d['estimate_present'] else None,
                        source_valid_at_separate_request=row.get('source_record',{}).get('valid'),
                        current_valid_at_separate_request=row.get('current_record',{}).get('valid'),
                        clock_request_bracket_ns=span,measurement=False,grants_time_authority=False,absolute_uncertainty_ns=None)
                    row['local_estimate_diagnostic']=state;local_states.append(state)
                    try:state['retained_envelope_margins']=local_margins(d)
                    except ValueError as e:state['retained_envelope_margins']=dict(unavailable=str(e),grants_time_authority=False)
                    local_counts['decoded']+=1
                    for flag in ('counter_valid','estimate_present','session_current','root_envelope_current','strict_identity_bit'):
                        local_counts[flag+'_YES' if state[flag] else flag+'_NO']+=1
            except (ValueError,KeyError,UnicodeError) as e:
                counts[name+'_rejected']+=1;rejections.append(dict(index=sample['index'],request=name,reason=str(e)))
                if name=='source':source.fence('DECODE_REJECTION')
                if name=='source':conditional_source.fence('DECODE_REJECTION')
        # Freeze the host fit before adding post-request register observations.
        # No source value trains its own prediction, nor does arrival train the
        # host bridge. Host points are measured register-transition brackets.
        host_before_fit=host.fit
        robust_before=copy.copy(host.robust) if host.robust else None
        same_window_before=copy.deepcopy(host.same_window_fit) if host.same_window_fit else None
        for r in sample.get('host_after',[]):
            # Post reads are ordinarily separated by several microframes and
            # add no transition point. Freeze explicitly to handle fixtures or
            # a future shorter transfer without accidental current-sample fit.
            if host_before_fit is host.fit:host_before_fit=copy.deepcopy(host.fit)
            post=host.observe(r)
        # Causal IN capture occurred between pre and post host reads, or <1ms
        # earlier for its SOF. Candidate numbering is conservative full-frame
        # quantization only; no midpoint fit resolves an integer origin.
        # The integrated TT/PHY phase has no measured bound, so this set is
        # explicitly conditional on direct-root MFINDEX/frame semantics.
        d=row.get('source_record')
        if pre is not None and post is not None and d and d['frame']<2048:
            proposed={(f-d['frame'])%2048 for f in range(pre//8-1,post//8+2)}
            if candidates is not None and not candidates.intersection(proposed):
                host_origin_segments.append(sorted(candidates));candidates=None;counts['conditional_origin_resets']+=1
            candidates=proposed if candidates is None else candidates.intersection(proposed)
            row['conditional_full_frame_origin_candidates']=sorted(candidates)
        if d:
            request=sample['source'];a=request['before'];b=request['after'];reference=None
            try:
                if not raw['device'].get('direct_root_port') or raw['device'].get('speed_mbps')!='12':
                    raise ValueError('conditional spec mapping requires direct full-speed topology')
                if pre is None or post is None:raise ValueError('no host register interval')
                event=spec_frame_ordinal(d['frame'],pre,post)
                prediction_delta=host_before_fit.predict_offset(event)
                if prediction_delta is None:raise ValueError('host transition fit warmup')
                prediction_origin=host_before_fit.origin[1]
                reference=(prediction_origin-raw['start']['raw_before_ns'])+prediction_delta
                # Subtract epoch integers before converting a bounded delta.
                raw_clock=(a['raw_before_ns']+a['raw_after_ns'])//2
                offset=(d['utc_ns']-a['realtime_ns'])-((prediction_origin-raw_clock)+prediction_delta) if d['valid'] else None
                row['conditional_spec_shadow']=dict(host_ordinal=event,raw_estimate_origin_ns=prediction_origin,
                    raw_estimate_delta_ns=prediction_delta,
                    source_utc_minus_system_ns=offset,clock_read_bracket_ns=a['raw_after_ns']-a['raw_before_ns'],
                    absolute_uncertainty_ns=None,identity_qualified=False,chrony_eligible=False)
                measurement_state='ADMITTED' if row.get('source',{}).get('state') not in ('WITHHELD',None) else 'WITHHELD'
                row['conditional_spec_shadow']['source_measurement_state']=measurement_state
                if offset is not None:
                    conditional_offsets.setdefault((scope(d),measurement_state),[]).append(offset)
                    row['conditional_spec_shadow']['provisional_source_utc_minus_system_ns']=corrected_phase(offset,provisional)
                    # Same event/model/realtime value: change only which RAW
                    # interval associates that realtime read. Never retune bias.
                    try:
                        association=realtime_interval(a)
                        if association['nested']:
                            paired_raw=(association['low_ns']+association['high_ns'])//2
                            paired_offset=(d['utc_ns']-a['realtime_ns'])-((prediction_origin-paired_raw)+prediction_delta)
                            row['native_realtime_pair_shadow']=dict(source_measurement_state=measurement_state,
                                source_utc_minus_system_ns=paired_offset,provisional_source_utc_minus_system_ns=corrected_phase(paired_offset,provisional),
                                full_bracket_control_source_utc_minus_system_ns=offset,
                                full_bracket_width_ns=a['raw_after_ns']-a['raw_before_ns'],
                                native_bracket_width_ns=association['high_ns']-association['low_ns'],
                                changes_only_cross_clock_association=True,absolute_uncertainty_ns=None,chrony_eligible=False)
                            clock_pairs.setdefault((scope(d),measurement_state),[]).append((offset,paired_offset))
                            clock_pair_widths.append(association['high_ns']-association['low_ns'])
                    except ValueError as e:row['native_realtime_pair_shadow']=dict(withheld=str(e),chrony_eligible=False)
                    filtered=robust_before.predict(event) if robust_before else None
                    if filtered is not None:
                        filtered_offset=(d['utc_ns']-a['realtime_ns'])-((filtered[0]-raw_clock)+filtered[1])
                        control_delta=same_window_before.predict_offset(event)
                        control_offset=(d['utc_ns']-a['realtime_ns'])-((same_window_before.origin[1]-raw_clock)+control_delta)
                        row['experimental_host_filter']=dict(method=method,source_measurement_state=measurement_state,
                            raw_estimate_origin_ns=filtered[0],raw_estimate_delta_ns=filtered[1],
                            source_utc_minus_system_ns=filtered_offset,
                            same_window_OLS_source_utc_minus_system_ns=control_offset,
                            provisional_same_window_OLS_source_utc_minus_system_ns=corrected_phase(control_offset,provisional),
                            provisional_source_utc_minus_system_ns=corrected_phase(filtered_offset,provisional),
                            previous_model_only=True,source_or_MHAT_phase_used=False,absolute_uncertainty_ns=None,chrony_eligible=False)
                        filtered_offsets.setdefault((scope(d),measurement_state),[]).append((offset,filtered_offset,control_offset))
            except ValueError as e:row['conditional_spec_shadow']=dict(withheld=str(e))
            row['conditional_source']=conditional_source.observe(d,a['raw_before_ns'],b['raw_after_ns'],reference_ns=reference)
            if d['valid']:
                row['provisional_shadow']=dict(derived_utc_ns=corrected_utc(d['utc_ns'],provisional),
                    source_measurement_state=row.get('source',{}).get('state'),grants_time_authority=False,
                    original_source_utc_ns=d['utc_ns'],absolute_uncertainty_ns=None,chrony_eligible=False)
        if 'marker' in sample:
            m=sample['marker'];counts['marker_attempted']+=1
            try:
                if 'error' in m:raise ValueError('marker transport failed')
                anchor=decode(bytes.fromhex(m['wire_hex']),expected)
                row['marker_record']=anchor;counts['marker_decoded']+=1
                counts['marker_valid' if anchor['valid'] else 'marker_unavailable']+=1
                if d is None or marker_pre is None or marker_post is None:raise ValueError('marker source/host evidence missing')
                row['marker_identity']=markers.observe(anchor,m['token'],m['out_before']['raw_before_ns'],
                    m['out_after']['raw_after_ns'],marker_reference,marker_pre,marker_post,d)
            except (ValueError,KeyError) as e:
                row['marker_identity']=dict(state='REJECTED',reason=str(e));counts['marker_rejected']+=1
        rows.append(row)
    # Preserve each status record and compare only source provenance present on
    # both paths. Status snapshots do not prove exact-event lineage.
    statuses=[hat_status(s) for s in raw['status']]
    for s in raw['status']:
        status=s['status'];q=status.get('source_status') or {};hat_contexts.append({k:q.get(k) for k in ('root','root_boot','epoch','parent','context','current')})
    for row,sample in zip(rows,raw['samples']):
        d=row.get('source_record')
        if not d:continue
        at=sample['clocks']['boottime_ns'];near=[]
        for s in statuses:
            if abs(s['boottime_ns']-at)<=1_600_000_000:near.append(s)
        if not d['valid']:join['USB_SOURCE_UNAVAILABLE']+=1
        elif not near:join['NO_NEARBY_HAT_STATUS']+=1
        elif all(q.get('eligible') and q.get('root')==d['root'] and q.get('root_boot')==d['root_boot'] and q.get('epoch')==d['source_epoch'] for q in near):
            join['MATCHING_SAMPLED_GM_ROOT_BOOT_EPOCH']+=1
        elif any(q.get('root')==d['root'] for q in near):join['SAME_ROOT_OTHER_LINEAGE_OR_UNAVAILABLE']+=1
        else:join['DIFFERENT_OR_UNKNOWN_ROOT']+=1
    pps=[];eligible_pps=[];pps_periods=[];gaps=[];previous=None;seed_count=0;first_new=None;last_new=None;seed_age=None
    status_times=[s['boottime_ns'] for s in statuses]
    for r in raw['pps']:
        stamp,seq=r['assertion'].split('#');scaled=Decimal(stamp)*1_000_000_000
        if not scaled.is_finite() or scaled!=scaled.to_integral_value():raise ValueError('invalid integer PPS timestamp')
        ns=int(scaled);seq=int(seq)
        if r['seed']:
            seed_count+=1;previous=None;seed_age=(raw['start']['realtime_ns']-ns)/1e9;continue
        phase=(ns+500_000_000)%1_000_000_000-500_000_000;pps.append(phase)
        when=r['before']['boottime_ns'];first_new=when if first_new is None else first_new;last_new=when
        k=bisect.bisect_left(status_times,when)
        if 0<k<len(statuses):
            a,b=statuses[k-1],statuses[k]
            if a['eligible'] and b['eligible'] and a['context']==b['context'] and b['boottime_ns']-a['boottime_ns']<=1_600_000_000:
                eligible_pps.append(phase)
        if previous:
            dt,ds=ns-previous[0],seq-previous[1]
            if ds==1 and 500_000_000<dt<1_500_000_000:pps_periods.append(dt-1_000_000_000)
            else:gaps.append(dict(previous_sequence=previous[1],sequence=seq,sequence_delta=ds,interval_ns=dt))
        previous=(ns,seq)
    lower=[];upper=[]
    for r in coarse:
        lo,hi=r['low_ns'],r['high_ns'];lower.append(0 if lo<=0<=hi else min(abs(lo),abs(hi)));upper.append(max(abs(lo),abs(hi)))
    offset_counts=Counter()
    for (_,state),values in conditional_offsets.items():offset_counts[state]+=len(values)
    valid_sources=[r['source_record'] for r in rows if r.get('source_record',{}).get('valid')]
    paired_maps=Counter()
    for r in rows:
        if 'source_record' in r and 'current_record' in r:
            paired_maps['SOF_'+('VALID' if r['source_record']['valid'] else 'UNAVAILABLE')+
                        '_CURRENT_'+('VALID' if r['current_record']['valid'] else 'UNAVAILABLE')]+=1
    return dict(schema=1,mode='SHADOW_ONLY',attempts=dict(counts),
                denominators=dict(attempted=counts['attempted'],wire_source_valid=counts['source_valid'],
                    wire_source_unavailable=counts['source_unavailable'],source_admitted=source.counts['accepted'],
                    source_warmup=source.counts['warmup'],source_causal_predicted=source.counts['predicted'],
                    valid_flag_withheld_by_host=counts['source_valid']-source.counts['accepted'],
                    conditional_offset_admitted=offset_counts['ADMITTED'],conditional_offset_withheld_diagnostic=offset_counts['WITHHELD'],
                    experimental_filtered_phase_admitted=sum(len(v) for (s,state),v in filtered_offsets.items() if state=='ADMITTED'),
                    experimental_filtered_phase_withheld_diagnostic=sum(len(v) for (s,state),v in filtered_offsets.items() if state=='WITHHELD'),
                    valid_without_conditional_offset=counts['source_valid']-sum(offset_counts.values()),
                    withheld_is_never_zero_error_or_omitted_from_availability=True),
                source=source.summary(),host=host.summary(),
                host_preparation_warmup=dict(requested_seconds=raw.get('host_warmup_seconds',0),bursts=len(prewarm.get('bursts',[])),
                    retained_reads=sum(len(b['reads']) for b in prewarm.get('bursts',[])),
                    source_model_training_points=0,cost_included_in_full_observer=True),
                source_coherence=dict(valid_full_source_cohorts=len({scope(d) for d in valid_sources}),
                    valid_root_boot_source_epoch_cohorts=len({tuple(d[k] for k in ('root','root_boot','source_epoch')) for d in valid_sources}),
                    valid_paths=len({d['path_scope'] for d in valid_sources}),
                    paired_current_SOF_map_coverage=dict(paired_maps),
                    current_and_SOF_are_sequential_requests_not_simultaneous=True),
                captured_tuple_diagnostic=dict(counts=dict(capture.counts),grants_time_authority=False,
                    mismatch_transitions=[r for r in capture.rows if r['state']=='COUNTER_FRAME_MISMATCH'],
                    each_compared_transition_retained_in_source_rows=True,endpoint_blame='UNRESOLVED'),
                source_rows=rows,host_transitions=host.transitions,
                conditional_spec_shadow=dict(source_causal=conditional_source.summary(),
                    offset_cohorts=[dict(scope=list(k[0]),measurement_state=k[1],USB_minus_system=statistics(v),
                        provisional_USB_minus_system=statistics([corrected_phase(x,provisional) for x in v]) if provisional['enabled'] else statistics([])) for k,v in conditional_offsets.items()],
                    absolute_uncertainty_ns=None,host_identity_qualified=False,chrony_eligible=False,
                    mapping='xHCI4.14.2 FrameNumber=MFINDEX[13:3], conditional on implementation and unknown physical phase'),
                experimental_host_filter=dict(enabled=enabled,method=method,control_maximum_training_points=host.control_window if enabled else None,
                    source_or_MHAT_phase_used=False,calibration_refit=False,absolute_uncertainty_ns=None,chrony_eligible=False,
                    phase_cohorts=[dict(scope=list(k[0]),measurement_state=k[1],common_denominator=len(v),
                        common_OLS_USB_minus_system=statistics([p[0] for p in v]),
                        common_OLS_provisional_USB_minus_system=statistics([corrected_phase(p[0],provisional) for p in v]) if provisional['enabled'] else statistics([]),
                        filtered_USB_minus_system=statistics([p[1] for p in v]),
                        common_window_OLS_USB_minus_system=statistics([p[2] for p in v]),
                        common_window_OLS_provisional_USB_minus_system=statistics([corrected_phase(p[2],provisional) for p in v]) if provisional['enabled'] else statistics([]),
                        filtered_provisional_USB_minus_system=statistics([corrected_phase(p[1],provisional) for p in v]) if provisional['enabled'] else statistics([])) for k,v in filtered_offsets.items()],
                    interpretation='Conditional previous host model estimate; training exclusions remain forecast diagnostics; no source permission changes.'),
                marker_identity=markers.summary(),
                native_realtime_pair_shadow=dict(method=raw.get('realtime_bracket_method','legacy-wide'),
                    all_snapshots=snapshot_summary(raw),
                    source_request_before_snapshots=snapshot_summary([s['source']['before'] for s in raw['samples'] if 'before' in s.get('source',{})]),
                    common_predictions=sum(len(v) for v in clock_pairs.values()),native_bracket_ns=statistics(clock_pair_widths),
                    phase_cohorts=[dict(scope=list(k[0]),measurement_state=k[1],common_denominator=len(v),
                        full_bracket_control=statistics([p[0] for p in v]),native_pair=statistics([p[1] for p in v]),
                        full_bracket_control_provisional=statistics([corrected_phase(p[0],provisional) for p in v]) if provisional['enabled'] else statistics([]),
                        native_pair_provisional=statistics([corrected_phase(p[1],provisional) for p in v]) if provisional['enabled'] else statistics([])) for k,v in clock_pairs.items()],
                    calibration_refit=False,changes_source_admission=False,absolute_uncertainty_ns=None,chrony_eligible=False),
                local_estimate_diagnostic=dict(enabled=bool(raw.get('local_estimate_diagnostic_hz')),counts=dict(local_counts),states=local_states,
                    grants_time_authority=False,measurement=False,absolute_uncertainty_ns=None,
                    interpretation='Sequential UTE1 local retained-estimate/session/root snapshots; flags do not identify an RF/GNSS cause or refresh strict permission.'),
                provisional_calibration=provisional,
                conditional_frame_origin=dict(candidates=sorted(candidates) if candidates is not None else None,
                                              prior_segments=host_origin_segments,resolved=False,
                                              reason='Integrated TT/PHY phase and causal numbering not independently bounded; IN-only candidates are diagnostic'),
                transport_bracket_ns=statistics(brackets),clock_read_bracket_ns=statistics(clock_widths),
                coarse_USB_minus_MHAT_disciplined_system=dict(intervals=coarse,
                    rms_lower_ns=statistics(lower)['rms_ns'],rms_upper_ns=statistics(upper)['rms_ns'],
                    finite_absolute_uncertainty_ns=None,midpoints_not_used_as_time_samples=True),
                simultaneous_GPIO=dict(new_edges=statistics(pps),source_eligible_edges=statistics(eligible_pps),
                                       cached_seeds=seed_count,cached_seed_age_at_start_s=seed_age,
                                       first_new_observed_after_start_s=(first_new-raw['start']['boottime_ns'])/1e9 if first_new is not None else None,
                                       last_new_observed_before_end_s=(raw['end']['boottime_ns']-last_new)/1e9 if last_new is not None else None,
                                       status_observations=len(statuses),eligible_status_observations=sum(s['eligible'] for s in statuses),
                                       period_error=statistics(pps_periods),gaps=gaps),
                sampled_lineage_join=dict(join),hat_contexts=hat_contexts,rejections=rejections,
                capture_errors=raw['errors'],interface_released=raw['interface_released'],
                observer=dict(cpu_one_core_percent=100*raw['process_cpu_ns']/raw['elapsed_monotonic_ns'],
                              elapsed_s=raw['elapsed_monotonic_ns']/1e9,register_reads=raw.get('register_reads',0),
                              register_reads_preparation=raw.get('register_reads_preparation',0),
                              register_reads_acquisition=raw.get('register_reads_acquisition',raw.get('register_reads',0)),
                              register_reads_per_s=raw.get('register_reads_acquisition',raw.get('register_reads',0))*1e9/raw['elapsed_monotonic_ns'],
                              native_compiler_child_cpu_ns=raw.get('native_build',{}).get('compiler_child_cpu_ns',0),
                              native_compile_startup_wall_ns=raw.get('native_build',{}).get('elapsed_monotonic_ns',0),
                              total_process_and_compiler_CPU_one_core_percent=100*(raw['process_cpu_ns']+raw.get('native_build',{}).get('compiler_child_cpu_ns',0))/raw['elapsed_monotonic_ns'],
                              full_observer_CPU_one_core_percent=100*(raw['observer_total']['process_cpu_ns']+raw['observer_total']['child_cpu_ns'])/raw['observer_total']['elapsed_monotonic_ns'] if 'observer_total' in raw else None,
                              full_observer_cost_boundary=raw.get('observer_total',{}).get('includes','legacy region; environment child costs unavailable')),
                USB_MHAT_parity_demonstrated=False,chrony_eligible=False,
                limits=['Pi CLOCK_REALTIME is disciplined by the simultaneous M-HAT path; not independent UTC.',
                        'Device SOF ordinal residual excludes host-clock phase and is not macOS RX USB RMS.',
                        'MFINDEX transition midpoint diagnostics retain brackets and unknown controller-to-wire bias.',
                        'Sampled matching lineage is not proof of exact USB/PPS event lineage.',
                        'No fitted residual or clock agreement supplies an absolute uncertainty bound.'])

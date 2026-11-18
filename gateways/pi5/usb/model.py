"""Causal shadow diagnostics. No value here may feed chrony or set a clock.

Copyright (c) 2026 Jonathan Yantis. MIT.
"""
from collections import Counter, deque
import math
from .contract import permission_deadline,permission_window_end,project,scope
from .interval import BoundedRate,AffineBrackets
from .host_filter import RobustHost


def statistics(values):
    if not values:
        return dict(n=0,mean_ns=None,rms_ns=None,sigma_ns=None,p99_abs_ns=None,max_abs_ns=None,
                    above_1us=0,above_2us=0,above_5us=0)
    n=len(values); mean=math.fsum(values)/n; absolute=sorted(abs(v) for v in values)
    return dict(n=n,mean_ns=mean,rms_ns=math.sqrt(math.fsum(v*v for v in values)/n),
                sigma_ns=math.sqrt(math.fsum((v-mean)**2 for v in values)/n),
                p99_abs_ns=absolute[math.ceil(.99*n)-1],max_abs_ns=absolute[-1],
                above_1us=sum(v>1000 for v in absolute),above_2us=sum(v>2000 for v in absolute),
                above_5us=sum(v>5000 for v in absolute))


class CausalAffine:
    """Integer origins are removed before float conversion. Predict, then add."""
    def __init__(self, minimum=64, maximum=1024):
        self.minimum=minimum; self.points=deque(maxlen=maximum); self.origin=None

    def predict_offset(self, x):
        if len(self.points)<self.minimum:
            return None
        xs=[p[0] for p in self.points]; ys=[p[1] for p in self.points]
        mx=math.fsum(xs)/len(xs); my=math.fsum(ys)/len(ys)
        xx=math.fsum((a-mx)**2 for a in xs)
        if not xx:
            return None
        slope=math.fsum((a-mx)*(b-my) for a,b in self.points)/xx
        return my+slope*(x-self.origin[0]-mx)

    def predict(self,x):
        value=self.predict_offset(x)
        return None if value is None else self.origin[1]+value

    def observe(self,x,y):
        if self.origin is None:
            self.origin=(x,y)
        prediction=self.predict_offset(x)
        self.points.append((x-self.origin[0],y-self.origin[1]))
        return None if prediction is None else (y-self.origin[1])-prediction


class Unwrapper:
    def __init__(self,modulus,period_ns,max_gap_ns=500_000_000):
        self.modulus=modulus;self.period=period_ns;self.max_gap=max_gap_ns;self.last=None;self.ordinal=0

    def observe(self,value,at):
        if type(value) is not int or not 0<=value<self.modulus:
            raise ValueError('frame outside modulus')
        if self.last is None:
            self.last=(value,at);self.ordinal=value;return self.ordinal
        old,t=self.last;dt=at-t;delta=(value-old)%self.modulus
        if dt<=0 or dt>self.max_gap or dt>=self.modulus*self.period//2:
            self.last=None;raise ValueError('frame wrap/gap ambiguous')
        # The caller supplies the clock of the observation: host RAW for an
        # MFINDEX read, captured device counter for a device SOF. USB request
        # submission/arrival is not a device frame timestamp.
        if abs(delta*self.period-dt)>1_000_000+dt//1000:
            self.last=None;raise ValueError('frame cadence discontinuity')
        self.ordinal+=delta;self.last=(value,at);return self.ordinal


class CaptureContinuity:
    """Raw SOF tuple continuity, independent of permission to publish time.

    Every usable adjacent tuple is compared, including the transition after a
    mismatch. Neither endpoint is silently selected as the correct event.
    This diagnostic never grants source authority or trains a UTC map.
    """
    def __init__(self):
        self.context=None;self.last=None;self.counts=Counter();self.rows=[]

    def observe(self,d,index):
        self.counts['attempted']+=1;row=dict(index=index,state='NO_USABLE_CAPTURE',source_valid=d.get('valid',False))
        ctx=tuple(d[k] for k in ('silicon','boot','usb_epoch','capture_epoch'))
        if ctx!=self.context:self.last=None;self.context=ctx;self.counts['scope_fences']+=1
        usable=(d['contract']=='UTS1' and d['kind']==1 and 0<=d['frame']<2048 and
                0<=d['current_tick']-d['tick']<16000)
        if not usable:
            self.last=None;self.counts['no_usable_capture']+=1
        else:
            self.counts['captured_tuple']+=1;row['age_ticks']=d['current_tick']-d['tick']
            if self.last is None:row['state']='SEED';self.counts['seed']+=1
            else:
                old,old_index=self.last;dt=d['tick']-old['tick'];df=(d['frame']-old['frame'])%2048
                row.update(previous_index=old_index,tick_delta=dt,frame_delta=df,frame_counter_residual_ticks=dt-df*16000)
                if not 0<dt<=8_000_000:
                    row['state']='COUNTER_GAP_OR_ROLLBACK';self.counts['gap_or_rollback']+=1
                else:
                    self.counts['compared_transitions']+=1
                    good=0<df<=500 and abs(dt-df*16000)<=4000
                    row['state']='COHERENT' if good else 'COUNTER_FRAME_MISMATCH'
                    self.counts['coherent' if good else 'counter_frame_mismatch']+=1
            self.last=(d,index)
        self.rows.append(row);return row


class SourceSeries:
    """Source vs device SOF ordinal, explicitly lacking host-clock phase.

    This is a useful relative consistency diagnostic while Linux frame origin
    is unresolved. It is not the macOS SOF/Mach metric or USB-vs-system error.
    """
    def __init__(self,host_reference=False):
        self.context=None;self.last=None;self.last_request=None;self.fit=CausalAffine();self.unwrapper=Unwrapper(2048,1_000_000)
        self.host_reference=host_reference
        self.maps={};self.generation=0;self.errors=[];self.counts=Counter();self.segments=[];self.rows=[]
        self.sequence_high={};self.counter_high={};self.known_maps={};self.generation_high={}
        self.permission_limits={}

    def fence(self,why):
        if self.context is not None:
            self.segments.append(dict(scope=list(self.context),errors=statistics(self.errors)))
        self.context=None;self.last=None;self.last_request=None;self.fit=CausalAffine();self.unwrapper=Unwrapper(2048,1_000_000)
        self.maps={};self.generation=0;self.errors=[];self.counts[why]+=1

    def observe(self,d,request_before,received,reference_ns=None):
        row=dict(state='WITHHELD',reason=None,causal_error_ns=None)
        try:
            if d['contract']=='UTS1':
                base=tuple(d[k] for k in ('silicon','boot','usb_epoch','capture_epoch'))
                if d['sequence']<=self.sequence_high.get(base,0):raise ValueError('SEQUENCE_REPLAY')
                if len(self.sequence_high)>=256 and base not in self.sequence_high:raise ValueError('CONTEXT_HISTORY_SATURATED')
                self.sequence_high[base]=d['sequence']
            if d['contract']!='UTS1' or d['kind']!=1 or not d['valid']:
                raise ValueError('SOURCE_UNAVAILABLE_OR_NONMEASUREMENT')
            ctx=scope(d)
            geometry=tuple(d[k] for k in ['anchor_tick','anchor_ns','effective_tick','active_lo_tick','map_ppb','map_phase_ns',
                                         'source_issued_ns','source_expires_ns','utc_offset_ns'])
            map_key=(ctx,d['map_generation']);known=self.known_maps.get(map_key)
            if known is not None and known!=geometry:raise ValueError('IMMUTABLE_MAP_MUTATED')
            if known is None and len(self.known_maps)>=8192:raise ValueError('IMMUTABLE_HISTORY_SATURATED')
            self.known_maps[map_key]=geometry
            # The serialized conservative until may vary slightly with the
            # current counter. Re-observation can shorten, never refresh,
            # authority first seen for this generation, even after a gap.
            limit=self.permission_limits.get(map_key)
            until=min(d['until_tick'],limit['until_tick']) if limit else d['until_tick']
            hi=min(d['active_hi_tick'],limit['active_hi_tick']) if limit else d['active_hi_tick']
            guarded=dict(d,until_tick=until,active_hi_tick=hi)
            deadline=permission_window_end(guarded,request_before)
            if limit:deadline=min(deadline,limit['deadline_raw_ns'])
            self.permission_limits[map_key]=dict(until_tick=until,active_hi_tick=hi,deadline_raw_ns=deadline)
            if received<request_before or received>=deadline:raise ValueError('original permission expired in flight')
            if self.host_reference and reference_ns is None:raise ValueError('HOST_REFERENCE_UNAVAILABLE')
            if d['tick']<=self.counter_high.get(base,-1):raise ValueError('COUNTER_OR_MAP_ROLLBACK')
            self.counter_high[base]=d['tick']
            if d['map_generation']<self.generation_high.get(ctx,0):raise ValueError('COUNTER_OR_MAP_ROLLBACK')
            self.generation_high[ctx]=d['map_generation']
            if self.context!=ctx:
                self.fence('SOURCE_SCOPE_CHANGE');self.context=ctx
            if self.last is not None:
                if not 0<request_before-self.last_request<=500_000_000:
                    raise ValueError('HOST_OBSERVATION_GAP')
                if d['sequence']<=self.last['sequence']:
                    raise ValueError('SEQUENCE_REPLAY')
                if d['tick']<=self.last['tick'] or d['map_generation']<self.generation:
                    raise ValueError('COUNTER_OR_MAP_ROLLBACK')
                dt=d['tick']-self.last['tick'];df=(d['frame']-self.last['frame'])%2048
                if not 0<df<=500 or abs(dt-df*16000)>4000:
                    raise ValueError('COUNTER_FRAME_DISCONTINUITY')
            # The independent captured counter establishes device cadence.
            # Transfer scheduling can move the request boundary by several
            # milliseconds and must never masquerade as SOF event time.
            ordinal=self.unwrapper.observe(d['frame'],d['tick']*125//2)
            corrected=project(guarded,d['tick'])
            error=self.fit.observe(reference_ns if self.host_reference else ordinal*1_000_000,corrected)
            self.last=d;self.last_request=request_before;self.generation=d['map_generation']
            self.counts['accepted']+=1
            if error is None:
                row['state']='WARMUP';self.counts['warmup']+=1
            else:
                self.errors.append(error);row['state']='CAUSAL_RELATIVE_DIAGNOSTIC';row['causal_error_ns']=error;self.counts['predicted']+=1
            row.update(scope=list(ctx),ordinal=ordinal,corrected_source_ns=corrected,original_deadline_raw_ns=deadline,
                       host_clock_association='UNRESOLVED',absolute_uncertainty_ns=None)
        except (ValueError,KeyError) as e:
            row['reason']=str(e);self.fence(str(e))
        self.rows.append(row);return row

    def summary(self):
        segments=self.segments+([dict(scope=list(self.context),errors=statistics(self.errors))] if self.context is not None else [])
        return dict(counts=dict(self.counts),segments=segments,
                    metric='CONDITIONAL_SPEC_FRAME_RAW_SOURCE_CAUSAL' if self.host_reference else 'CORRECTED_SOURCE_VS_DEVICE_SOF_ORDINAL_CAUSAL',
                    minimum_training_samples=64,maximum_training_samples=1024,maximum_gap_ns=500_000_000,
                    host_phase_included=self.host_reference,host_phase_qualified=False,
                    macOS_metric_equivalent=False,chrony_eligible=False,
                    absolute_uncertainty_ns=None)


class HostSeries:
    """Read brackets of successive MFINDEX values bound observed transitions.

    A transition lies after the old read's start and before the new read's end.
    Midpoint prediction is a diagnostic with an explicit acquisition bracket,
    never an atomic frame timestamp or a certified error bound.
    """
    def __init__(self,robust_enabled=False,robust_method='robust-host-v1'):
        self.unwrap=Unwrapper(16384,125000);self.previous=None;self.fit=CausalAffine()
        self.widths=[];self.errors=[];self.counts=Counter();self.transitions=[]
        self.rate=BoundedRate();self.affine=AffineBrackets()
        if robust_method not in ('robust-host-v1','weighted-host-v2'):raise ValueError('unknown experimental host method')
        self.robust_method=robust_method
        self.control_window=128 if robust_method=='robust-host-v1' else 1024
        self.robust=RobustHost(maximum=self.control_window,gated=robust_method=='robust-host-v1') if robust_enabled else None
        self.same_window_fit=CausalAffine(maximum=self.control_window) if robust_enabled else None

    def observe(self,r):
        a,b=r['before_raw_ns'],r['after_raw_ns']
        if b<a:
            raise ValueError('negative register bracket')
        try:
            ordinal=self.unwrap.observe(r['mfindex'],(a+b)//2)
        except ValueError:
            self.previous=None;self.fit=CausalAffine();self.counts['resets']+=1
            self.rate=BoundedRate();self.affine=AffineBrackets()
            if self.robust:self.robust.reset('HOST_WRAP_GAP_OR_CLOCK_FENCE')
            if self.same_window_fit:self.same_window_fit=CausalAffine(maximum=self.control_window)
            ordinal=self.unwrap.observe(r['mfindex'],(a+b)//2)
        if self.previous is not None:
            old,old_ordinal=self.previous;delta=ordinal-old_ordinal
            if delta==1:
                lo,hi=old['before_raw_ns'],b
                midpoint=(lo+hi)//2;err=self.fit.observe(ordinal,midpoint)
                self.widths.append(hi-lo)
                if err is not None:self.errors.append(err)
                rate=self.rate.observe(ordinal,lo,hi)
                resets=self.affine.counts['inconsistent_reset'];affine=self.affine.observe(ordinal,lo,hi)
                robust=self.robust.observe(ordinal,lo,hi) if self.robust else None
                same_window=self.same_window_fit.observe(ordinal,midpoint) if self.same_window_fit else None
                self.counts['affine_inconsistent_resets']+=self.affine.counts['inconsistent_reset']-resets
                if rate is not None and not rate['overlaps_complete_read_bracket']:self.counts['bounded_rate_inconsistent_resets']+=1
                self.transitions.append(dict(ordinal=ordinal,low_raw_ns=lo,high_raw_ns=hi,causal_midpoint_error_ns=err,
                    bounded_rate=rate,affine_set=affine,
                    experimental_robust_host=robust,
                    causal_control_OLS_error_ns=same_window,
                    midpoint_OLS_worst_endpoint_distance_ns=max(abs((midpoint-(lo-1))-err),abs((midpoint-(hi+1))-err)) if err is not None else None))
            elif delta>1:
                self.counts['unobserved_microframe_transitions']+=delta-1
        self.previous=(r,ordinal);self.counts['register_reads']+=1
        return ordinal

    def summary(self):
        def intervals(key):
            values=[r[key] for r in self.transitions if r[key] is not None]
            common=[r[key] for r in self.transitions if r[key] is not None and r['causal_midpoint_error_ns'] is not None]
            def coverage(rows):
                return dict(n=len(rows),overlap_not_falsified=sum(r['overlaps_complete_read_bracket'] for r in rows),
                            contains_complete_observation_bracket=sum(r['contains_complete_read_bracket'] for r in rows),
                            disjoint=sum(not r['overlaps_complete_read_bracket'] for r in rows),
                            actual_event_coverage='UNKNOWN_WITHIN_OBSERVATION_BRACKET')
            return dict(all_prior_model_predictions=coverage(values),common_with_OLS=coverage(common),
                        predicted_width_ns=statistics([r['width_ns'] for r in values]),
                        disjoint_distance_ns=statistics([r['disjoint_distance_ns'] for r in values]))
        common=[r for r in self.transitions if r.get('experimental_robust_host') and r['experimental_robust_host']['forecast_error_ns'] is not None and r['causal_midpoint_error_ns'] is not None]
        selected=[r for r in common if r['experimental_robust_host']['training_accepted']]
        robust=dict(enabled=self.robust is not None,experimental=True,method=self.robust_method,control_maximum_training_points=self.control_window,
            counts=dict(self.robust.counts) if self.robust else {},
            common_denominator=len(common),common_OLS=statistics([r['causal_midpoint_error_ns'] for r in common]),
            common_robust_all_forecasts=statistics([r['experimental_robust_host']['forecast_error_ns'] for r in common]),
            common_window_OLS=statistics([r['causal_control_OLS_error_ns'] for r in common]),
            training_selected_denominator=len(selected),selected_OLS=statistics([r['causal_midpoint_error_ns'] for r in selected]),
            selected_robust=statistics([r['experimental_robust_host']['forecast_error_ns'] for r in selected]),
            widest_training_withheld_bracket_ns=max((r['experimental_robust_host']['bracket_width_ns'] for r in common if not r['experimental_robust_host']['training_accepted']),default=None),
            absolute_uncertainty_ns=None,source_or_MHAT_phase_used=False,calibration_refit=False)
        return dict(counts=dict(self.counts),experimental_robust_host=robust,transition_bracket_width_ns=statistics(self.widths),
                    transition_midpoint_causal_diagnostic_ns=statistics(self.errors),
                    bounded_rate_intervals=intervals('bounded_rate'),affine_set_intervals=intervals('affine_set'),
                    midpoint_OLS_worst_endpoint_distance_ns=statistics([r['midpoint_OLS_worst_endpoint_distance_ns'] for r in self.transitions if r['midpoint_OLS_worst_endpoint_distance_ns'] is not None]),
                    interval_assumptions=dict(rate_ratio_scenario_ppm=1000,rate_scenario_physically_calibrated=False,
                        bounded_rate='arbitrary per-microframe rate within declared scenario; integer outward bounds',
                        affine_trial='constant period consistent with preceding<=64 transition brackets; minimum16; failures retained',
                        quantization_ns=1,physical_phase_bias_included=False),
                    absolute_uncertainty_ns=None,controller_to_wire_bias_ns=None,
                    atomic_frame_timestamp=False,chrony_eligible=False)


def spec_frame_ordinal(frame,pre,post):
    """Conditional xHCI4.14.2 mapping: USB2 FrameNumber = MFINDEX[13:3].

    Return only a unique nearby wrap of frame*8. This validates compatibility
    with the causal IN interval; it does not measure integrated-controller/PHY
    phase, prove implementation conformance or resolve the wider empirical set.
    """
    if not 0<=frame<2048 or post<pre or post-pre>=4000:raise ValueError('ambiguous host frame interval')
    base=frame*8;wrap=pre//16384
    possible=[base+k*16384 for k in range(wrap-1,wrap+2) if pre-8<=base+k*16384<=post+8]
    if len(possible)!=1:raise ValueError('spec frame mapping incompatible or wrap ambiguous')
    return possible[0]


class MarkerIdentity:
    """Independent transient OUT markers constrain integer frame candidates.

    The declared1000ppm short extrapolation envelope is a model assumption,
    matching the macOS marker logic, not a calibrated physical bias bound.
    Controller-to-wire/PHY phase and host transition read intervals remain.
    """
    def __init__(self):
        self.context=None;self.sequence=0;self.candidates=None;self.accepted=0
        self.blocked=False;self.counts=Counter();self.rows=[];self.segments=[]

    def observe(self,a,token,before,after,reference,pre,post,source):
        row=dict(state='REJECTED',reason=None,conditional_only=True,absolute_uncertainty_ns=None)
        try:
            if a['contract']!='UFA1' or a['token']!=token or not token:
                raise ValueError('MARKER_INVALID_OR_TOKEN_MISMATCH')
            ctx=scope(a)
            if self.context!=ctx:
                if self.context is not None:self.segments.append(self.summary(include_segments=False))
                self.context=ctx;self.sequence=0;self.candidates=None;self.accepted=0;self.blocked=False
            if self.blocked:raise ValueError('MARKER_EPOCH_BLOCKED')
            if a['sequence']<=self.sequence:raise ValueError('MARKER_SEQUENCE_REPLAY')
            self.sequence=a['sequence']
            if tuple(source[k] for k in ('silicon','boot','usb_epoch','capture_epoch'))!=ctx:
                raise ValueError('MARKER_SOURCE_EPOCH_MISMATCH')
            if not a['valid']:
                if a.get('reason')==2:
                    # A successful, token/epoch-matched handshake can report
                    # an unavailable SOF snapshot. It supplies no origin point
                    # and fences training, but is not a numbering contradiction.
                    self.segments.append(self.summary(include_segments=False))
                    self.candidates=None;self.accepted=0
                    row['state']='UNAVAILABLE';raise ValueError('MARKER_UNAVAILABLE')
                raise ValueError('MARKER_STALE_OR_INVALID')
            back=(source['frame']-a['frame'])%2048
            if source['tick']<a['tick'] or back>500 or abs(source['tick']-a['tick']-back*16000)>=4000:
                raise ValueError('MARKER_SOURCE_COUNTER_FRAME_MISMATCH')
            if after<before or after-before>100_000_000 or post<pre or post-pre>=4000:
                raise ValueError('MARKER_BRACKET_AMBIGUOUS')
            if reference is None or reference['high_raw_ns']>before:
                raise ValueError('NO_PRECEDING_HOST_TRANSITION')
            age_twice=(a['current_tick']-a['tick'])*125
            sof_lo=before*2-age_twice-2000;sof_hi=after*2-age_twice+2000
            possible=set()
            for frame in range(pre//8-2,post//8+3):
                delta=frame*8-reference['ordinal'];margin=abs(delta)*125 # declared1000ppm
                lo=reference['low_raw_ns']+delta*125000-margin
                hi=reference['high_raw_ns']+delta*125000+margin
                if lo*2<=sof_hi and hi*2>=sof_lo:possible.add((frame-a['frame'])%2048)
            if not possible:raise ValueError('NO_CAUSAL_MARKER_CANDIDATE')
            next_candidates=possible if self.candidates is None else self.candidates.intersection(possible)
            if not next_candidates:
                self.blocked=True;raise ValueError('CAUSAL_MARKER_CONTRADICTION')
            self.candidates=next_candidates;self.accepted+=1;self.counts['accepted']+=1
            row.update(state='INTEGER_ORIGIN_CONSTRAINED' if self.accepted>=16 and len(self.candidates)==1 else 'COLLECTING',
                       candidates=sorted(self.candidates),marker_sof_low_raw_twice_ns=sof_lo,marker_sof_high_raw_twice_ns=sof_hi,
                       out_bracket_ns=after-before,host_transition_bracket_ns=reference['high_raw_ns']-reference['low_raw_ns'])
        except (ValueError,KeyError) as e:
            row['reason']=str(e);self.counts[str(e)]+=1
            if str(e) not in ('NO_PRECEDING_HOST_TRANSITION','MARKER_UNAVAILABLE'):self.blocked=True
        self.rows.append(row);return row

    def summary(self,include_segments=True):
        r=dict(scope=list(self.context) if self.context else None,candidates=sorted(self.candidates) if self.candidates is not None else None,
               accepted_in_current_epoch=self.accepted,blocked=self.blocked,counts=dict(self.counts),
               integer_origin_constrained=bool(not self.blocked and self.accepted>=16 and self.candidates and len(self.candidates)==1),
               host_timestamp_qualified=False,absolute_uncertainty_ns=None,chrony_eligible=False)
        if include_segments:r['prior_epochs']=self.segments
        return r


def coarse_interval(d,before,after):
    if not d['valid'] or d['kind']!=2 or d['contract']!='UTS1':
        raise ValueError('not a current-counter measurement')
    span=after['raw_after_ns']-before['raw_before_ns']
    real_span=after['realtime_ns']-before['realtime_ns']
    if not 0<=span<=100_000_000 or real_span<0 or abs(span-real_span)>5_000_000:
        raise ValueError('clock bracket/step incoherent')
    permission_deadline(d,before['raw_before_ns'],after['raw_after_ns'])
    # Wall reads are inside separately retained RAW brackets. Broaden to the
    # outer read bounds; no request midpoint is treated as an event timestamp.
    lo=d['utc_ns']-after['realtime_ns']-(after['raw_after_ns']-after['raw_before_ns'])
    hi=d['utc_ns']-before['realtime_ns']+(before['raw_after_ns']-before['raw_before_ns'])
    return dict(low_ns=lo,high_ns=hi,width_ns=hi-lo,midpoint_is_time_sample=False)

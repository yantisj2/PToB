"""Experimental causal host-bracket regression; no source phase is an input.

Copyright (c) 2026 Jonathan Yantis. MIT. This is not an uncertainty bound.
"""
from collections import Counter,deque
import math
from statistics import median


class RobustHost:
    def __init__(self,minimum=64,maximum=128,gated=True):
        self.minimum=minimum;self.points=deque(maxlen=maximum);self.history=deque(maxlen=256)
        if type(minimum) is not int or type(maximum) is not int or not 4<=minimum<=maximum:raise ValueError('invalid host training window')
        if type(gated) is not bool:raise ValueError('invalid host gating option')
        self.gated=gated
        self.params=None;self.weight_stats=None;self.last_accepted=None;self.last_observed=None;self.rejected_run=0;self.counts=Counter();self.rows=[]

    def reset(self,reason):
        self.points.clear();self.history.clear();self.params=None;self.weight_stats=None;self.last_accepted=None;self.last_observed=None;self.rejected_run=0
        self.counts[reason]+=1

    def rebuild(self):
        if len(self.points)<self.minimum:self.params=None;return
        ox,origin,_,_=self.points[0]
        points=[(x-ox,mid-origin-(x-ox)*125000) for x,mid,_,_ in self.points]
        half=len(points)//2
        slopes=[(points[i+half][1]-points[i][1])/(points[i+half][0]-points[i][0]) for i in range(half) if points[i+half][0]!=points[i][0]]
        slope=median(slopes);intercept=median(y-slope*x for x,y in points)
        # Two robust reweighting passes use preceding points exclusively.
        for _ in range(2):
            residual=[y-intercept-slope*x for x,y in points];center=median(residual)
            scale=max(500.,1.4826*median(abs(e-center) for e in residual));cut=3*scale
            weights=[min(1.,cut/max(abs(e-center),1.)) for e in residual];total=math.fsum(weights)
            mx=math.fsum(w*x for w,(x,y) in zip(weights,points))/total
            my=math.fsum(w*y for w,(x,y) in zip(weights,points))/total
            xx=math.fsum(w*(x-mx)**2 for w,(x,y) in zip(weights,points))
            if xx:slope=math.fsum(w*(x-mx)*(y-my) for w,(x,y) in zip(weights,points))/xx
            intercept=my-slope*mx
        self.params=(ox,origin,intercept,slope)
        self.weight_stats=dict(n=len(points),reweighted=sum(w<1 for w in weights),minimum_weight=min(weights),
            Huber_cutoff_ns=cut,scale_floor_ns=500,IRLS_passes=2)

    def predict(self,x):
        if self.params is None or self.last_accepted is None or abs(x-self.last_accepted)*125000>500_000_000:return None
        ox,origin,intercept,slope=self.params;dx=x-ox
        return origin,dx*125000+intercept+slope*dx

    def observe(self,x,lo,hi):
        if any(type(v) is not int for v in (x,lo,hi)) or hi<lo:raise ValueError('invalid robust host bracket')
        if self.last_observed is not None and (x<=self.last_observed or (x-self.last_observed)*125000>500_000_000):
            self.reset('HOST_ORDINAL_GAP_OR_ROLLBACK')
        self.last_observed=x
        mid=(lo+hi)//2;width=hi-lo;prediction=self.predict(x)
        error=None if prediction is None else (mid-prediction[0])-prediction[1]
        past_widths=[p[3] for p in self.points]
        width_limit=None;innovation_limit=None;center=0.
        if self.gated and len(past_widths)>=self.minimum:
            wm=median(past_widths);width_limit=wm+max(1000.,6*1.4826*median(abs(w-wm) for w in past_widths))
        if self.gated and len(self.history)>=16:
            center=median(self.history);innovation_limit=max(1000.,6*1.4826*median(abs(e-center) for e in self.history))
        bad_width=width_limit is not None and width>width_limit
        bad_innovation=error is not None and innovation_limit is not None and abs(error-center)>innovation_limit
        accepted=not (bad_width or bad_innovation)
        row=dict(ordinal=x,forecast_error_ns=error,training_accepted=accepted,
            state='WARMUP' if prediction is None else 'FORECAST_SCORED',
            rejection='WIDE_COMPLETE_BRACKET' if bad_width else 'HOST_INNOVATION_TAIL' if bad_innovation else None,
            preceding_width_limit_ns=width_limit,preceding_innovation_limit_ns=innovation_limit,
            preceding_fit_weights=dict(self.weight_stats) if self.weight_stats else None,
            bracket_width_ns=width,thresholds_from_current=False)
        if error is not None:
            self.counts['forecast_scored']+=1;self.history.append(error)
        else:self.counts['warmup_or_unavailable']+=1
        if accepted:
            self.points.append((x,mid,lo,width));self.last_accepted=x;self.rejected_run=0;self.rebuild();self.counts['training_accepted']+=1
        else:
            self.rejected_run+=1;self.counts['training_withheld']+=1
            if self.rejected_run>=4:
                self.reset('FOUR_CONSECUTIVE_HOST_FAILURES');row['training_reset_after_score']=True
        self.rows.append(row);return row

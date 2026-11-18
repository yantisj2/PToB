"""Causal host-clock sets from complete MFINDEX transition brackets.

Copyright (c) 2026 Jonathan Yantis. MIT.
All bounds are conditional on declared clock-rate/smoothness assumptions. They
exclude controller-to-wire/PHY and UTC bias and never qualify chrony input.
"""
from collections import Counter,deque
import math


class BoundedRate:
    """Feasible phase interval with arbitrary per-microframe rate changes.

    ±1000ppm is a declared RAW/USB ratio scenario, not a measured oscillator
    calibration. Unlike the affine trial, no derivative or constant-rate claim
    is made. Interval endpoints are rounded outward using integer arithmetic.
    """
    def __init__(self,ppm=1000):
        if type(ppm) is not int or not 0<=ppm<=10000:raise ValueError('invalid rate scenario')
        self.ppm=ppm;self.last=None;self.counts=Counter()

    def predict(self,x):
        if self.last is None:return None
        old,lo,hi=self.last;delta=x-old
        a=delta*125000*(1_000_000-self.ppm);b=delta*125000*(1_000_000+self.ppm)
        lower=min(a,b)//1_000_000;upper=-(-max(a,b)//1_000_000)
        return lo+lower,hi+upper

    def observe(self,x,lo,hi):
        if type(x) is not int or type(lo) is not int or type(hi) is not int or hi<lo:
            raise ValueError('invalid transition interval')
        # Clock reads are integral nanoseconds; retain one ns outward on each
        # side rather than interpreting decimal resolution as zero error.
        lo-=1;hi+=1;predicted=self.predict(x)
        if self.last is not None and x<=self.last[0]:raise ValueError('nonmonotonic transition ordinal')
        if predicted is None:
            self.last=(x,lo,hi);self.counts['seed']+=1;return None
        a,b=predicted;overlap=max(lo,a)<=min(hi,b)
        if overlap:self.last=(x,max(lo,a),min(hi,b))
        else:self.last=(x,lo,hi);self.counts['inconsistent_reset']+=1
        self.counts['predictions']+=1;self.counts['overlap']+=overlap
        return dict(low_ns=a,high_ns=b,width_ns=b-a,observed_low_ns=lo,observed_high_ns=hi,
                    overlaps_complete_read_bracket=overlap,contains_complete_read_bracket=a<=lo and b>=hi,
                    disjoint_distance_ns=max(0,lo-b,a-hi),
                    actual_event_coverage='UNKNOWN_WITHIN_OBSERVATION_BRACKET')


def clip(polygon,dx,bound,upper):
    """Clip period/intercept polygon by intercept+period*dx <=/>= bound."""
    if not polygon:return []
    def f(p):return (p[1]+p[0]*dx-bound)*(1 if upper else -1)
    out=[];a=polygon[-1];fa=f(a)
    for b in polygon:
        fb=f(b);ina=fa<=0;inb=fb<=0
        if ina!=inb:
            t=fa/(fa-fb);out.append((a[0]+t*(b[0]-a[0]),a[1]+t*(b[1]-a[1])))
        if inb:out.append(b)
        a=b;fa=fb
    return out


class AffineBrackets:
    """Affine clocks consistent with preceding brackets; default window64.

    Constant period within this short window is an explicit trial assumption;
    model failures remain out-of-sample failures and reset the next window.
    Larger windows are separately declared offline sensitivity scenarios.
    No residual percentile, trimming or post-observation prediction is used.
    """
    def __init__(self,ppm=1000,maximum=64,minimum=16):
        if type(ppm) is not int or not 0<=ppm<=10000 or type(maximum) is not int or type(minimum) is not int or not 4<=minimum<=maximum<=4096:
            raise ValueError('invalid affine window/rate scenario')
        self.ppm=ppm;self.points=deque(maxlen=maximum);self.minimum=minimum
        self.origin=None;self.polygon=[];self.counts=Counter()

    def rebuild(self):
        x,lo,hi=self.points[0];self.origin=(x,lo)
        p=125000*(1-self.ppm/1e6);q=125000*(1+self.ppm/1e6)
        poly=[(p,0),(q,0),(q,hi-lo),(p,hi-lo)]
        for a,b,c in self.points:
            poly=clip(poly,a-x,c-lo,True);poly=clip(poly,a-x,b-lo,False)
            if not poly:break
        self.polygon=poly

    def predict(self,x):
        if len(self.points)<self.minimum or not self.polygon:return None
        dx=x-self.origin[0];values=[a+b*dx for b,a in self.polygon]
        # Subtract epoch integers before float conversion; outward integer
        # endpoints include an extra ns for numeric clipping/clock resolution.
        return self.origin[1]+math.floor(min(values))-1,self.origin[1]+math.ceil(max(values))+1

    def observe(self,x,lo,hi):
        lo-=1;hi+=1;predicted=self.predict(x)
        if self.points and x<=self.points[-1][0]:raise ValueError('nonmonotonic affine transition')
        result=None
        if predicted is not None:
            a,b=predicted;overlap=max(lo,a)<=min(hi,b)
            result=dict(low_ns=a,high_ns=b,width_ns=b-a,observed_low_ns=lo,observed_high_ns=hi,
                        overlaps_complete_read_bracket=overlap,contains_complete_read_bracket=a<=lo and b>=hi,
                        disjoint_distance_ns=max(0,lo-b,a-hi),
                        actual_event_coverage='UNKNOWN_WITHIN_OBSERVATION_BRACKET')
            self.counts['predictions']+=1;self.counts['overlap']+=overlap
        self.points.append((x,lo,hi));self.rebuild()
        if not self.polygon:
            self.counts['inconsistent_reset']+=1
            # The failed observation remains in the output denominator. Start
            # the next fit from it; never delete it to make a model pass.
            self.points.clear();self.points.append((x,lo,hi));self.rebuild()
        return result

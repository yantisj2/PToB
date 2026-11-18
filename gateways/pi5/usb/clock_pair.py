"""Validate a host cross-clock association, never a USB event timestamp.

Copyright (c) 2026 Jonathan Yantis. MIT.
"""
from collections import Counter
from .model import statistics


def realtime_interval(clocks):
    keys=('raw_before_ns','raw_after_ns','realtime_ns')
    if any(type(clocks.get(k)) is not int for k in keys):
        raise ValueError('clock association requires integer nanoseconds')
    lo,hi=(clocks[k] for k in keys[:2])
    if not 0<=lo<=hi or not 0<clocks['realtime_ns']<(1<<63):
        raise ValueError('invalid full cross-clock bracket')
    method=clocks.get('realtime_bracket_method','legacy-wide')
    nested=('realtime_raw_before_ns','realtime_raw_after_ns')
    if method=='legacy-wide':
        if any(k in clocks for k in nested):raise ValueError('unlabeled nested cross-clock bracket')
        return dict(method=method,low_ns=lo,high_ns=hi,nested=False)
    if method!='native-pair' or any(type(clocks.get(k)) is not int for k in nested):
        raise ValueError('unknown or incomplete native cross-clock bracket')
    a,b=(clocks[k] for k in nested)
    if not lo<=a<=b<=hi:raise ValueError('native cross-clock bracket outside full bracket')
    return dict(method=method,low_ns=a,high_ns=b,nested=True)


def snapshot_summary(raw):
    """Every retained full snapshot, including unavailable-source intervals."""
    counts=Counter();full=[];paired_full=[];nested=[];shifts=[];invalid=[]
    def visit(value,path):
        if isinstance(value,dict):
            if 'raw_before_ns' in value and 'raw_after_ns' in value:
                counts['snapshots']+=1
                try:
                    association=realtime_interval(value);counts[association['method']]+=1
                    full.append(value['raw_after_ns']-value['raw_before_ns'])
                    if association['nested']:
                        paired_full.append(value['raw_after_ns']-value['raw_before_ns'])
                        nested.append(association['high_ns']-association['low_ns'])
                        shifts.append((association['low_ns']+association['high_ns'])//2-(value['raw_before_ns']+value['raw_after_ns'])//2)
                except ValueError as e:invalid.append(dict(path=path,error=str(e)));counts['invalid']+=1
                return
            for key,item in value.items():visit(item,path+'.'+str(key))
        elif isinstance(value,list):
            for i,item in enumerate(value):visit(item,path+'['+str(i)+']')
    visit(raw,'raw')
    return dict(counts=dict(counts),full_bracket_ns=statistics(full),paired_full_bracket_ns=statistics(paired_full),native_bracket_ns=statistics(nested),
        native_minus_full_midpoint_ns=statistics(shifts),invalid=invalid,
        selection='all retained clock snapshots, independent of source availability or phase prediction',
        midpoint_is_diagnostic_not_event_timestamp=True,absolute_uncertainty_ns=None)

#!/usr/bin/env python3
"""Independent replay of causal, accepted BLE HOLD-rate inputs; no witness training.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import math,struct
HZ=20000000

def fit(points,at,seconds):
    ps=[p for p in points if at-seconds*HZ<=p['local20']<=at and p['available20']<=at]
    r=dict(rate=0.,guard=math.inf,n=len(ps),pairs=0,span_ms=0)
    if not ps:return r
    origin=ps[0];r['span_ms']=(ps[-1]['local20']-origin['local20'])//20000
    if len(ps)<32 or r['span_ms']<seconds*850:return r
    channels={}
    for p in ps:
        dt=p['local20']-origin['local20']
        channels.setdefault(p['channel'],[]).append((dt/HZ,p['reference_ns']-origin['reference_ns']-dt*50))
    r['pairs']=sum(len(c)-1 for c in channels.values() if len(c)>1)
    if r['pairs']<8:return r
    residual=[]
    for c in channels.values():
        if len(c)<2:continue
        mx=sum(x for x,y in c)/len(c);my=sum(y for x,y in c)/len(c)
        residual.extend((x-mx,y-my) for x,y in c)
    xx=sum(x*x for x,y in residual)
    if xx<=0:return r
    r['rate']=sum(x*y for x,y in residual)/xx
    squares=sum((y-r['rate']*x)**2 for x,y in residual)
    r['guard']=max(50000/r['span_ms'],3*math.sqrt(squares/(r['pairs']-1)/xx))
    return r

def verify(pairs,meta,entry):
    at=int(meta['available20']);start=int(meta['start20']);generation=int(meta['generation'])
    history=[];last=0;gen=0;eligible=[]
    for p in pairs:
        if p['available20']>start:continue
        eligible.append(p)
        if p['available20']>at:continue
        if p['generation']!=gen:history=[];last=0;gen=p['generation']
        tick=p['local20']
        if not tick or p['available20']<tick or p['reference_ns']>2**63-1 or p['channel']>=37 or tick<=last:continue
        if last and tick-last<HZ//4:continue
        history=[q for q in history if tick-q['local20']<=32*HZ][-159:]
        history.append(p);last=tick
    assert gen==generation and history,'Missing generation/history for causal rate replay'
    fits=[fit(history,at,seconds) for seconds in (16,32)]
    for seconds,f in zip((16,32),fits):
        for key,value in f.items():
            actual=float(meta['fit'+str(seconds)+'_'+key])
            assert actual==value or math.isclose(actual,value,rel_tol=1e-9,abs_tol=1e-7),(seconds,key,actual,value)
    # The current entry history includes points accepted after the cached fit.
    raw=bytes.fromhex(entry['points']);assert len(raw)==int(entry['n'])*8
    accepted={(p['local20'],p['reference_ns'],p['channel']) for p in eligible}
    for offset in range(0,len(raw),8):
        b=raw[offset:offset+8];dt=struct.unpack('<I',b[:4])[0]
        residual=int.from_bytes(b[4:7],'little',signed=True)
        local=int(entry['base_local20'])+dt;remote=int(entry['base_remote_ns'])+dt*50+residual
        assert local<=start and (local,remote,b[7]) in accepted,'Entry fit point lacks accepted causal provenance'
    fast=float(meta['fast_rate']);guard=float(entry['guard']);a,b=fits
    selected=(0<=start-at<=2*HZ and 0<=start-last<=2*HZ and math.isfinite(fast) and math.isfinite(guard)
              and a['guard']<=10 and b['guard']<=10 and b['guard']<guard and abs(a['rate']-b['rate'])<=5)
    median=sorted([fast,a['rate'],b['rate']])[1]
    selected=selected and abs(median-fast)<=15
    delta=max(-5,min(5,median-fast)) if selected else 0
    assert int(meta['selected'])==int(selected) and math.isclose(float(meta['delta']),delta,abs_tol=1e-7)
    assert math.isclose(float(meta['rate']),fast+delta,abs_tol=1e-7)
    return dict(verified=True,selected=bool(selected),delta_ppb=delta,fit16=a,fit32=b,
                training_source='ACCEPTED_BLE_PAIRS_AVAILABLE_BEFORE_ENTRY',entry_points=int(entry['n']))

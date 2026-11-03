#!/usr/bin/env python3
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""Vector scientific plot of selected unsmoothed common-population errors.

Original output figure: CC BY 4.0, Jonathan Yantis. Requires public ReportLab.
"""
import csv
import json
import math
from pathlib import Path
from reportlab.pdfgen import canvas
from reportlab.lib.colors import Color

ROOT=Path(__file__).resolve().parent
MODELS=[('all900','NTP-only WLS900',Color(.32,.32,.32),[]),
        ('AP900','AP WLS900',Color(.05,.32,.66),[]),
        ('Huber_AP900','AP Huber900',Color(.76,.20,.10),[3,2])]

def ticks(low,high):
    span=max(1.,high-low);low-=span*.07;high+=span*.07
    unit=10**math.floor(math.log10((high-low)/4))
    step=next(s*unit for s in [1,2,5,10] if s*unit>=(high-low)/4)
    a=math.floor(low/step)*step;b=math.ceil(high/step)*step
    return a,b,[a+i*step for i in range(round((b-a)/step)+1)]

def main():
    runs={}
    provenance=json.loads((ROOT/'provenance.json').read_text())
    primary=provenance.get('paper_primary_runs',list(provenance['runs']))
    with (ROOT/'future.csv').open(newline='') as f:
        for row in csv.DictReader(f):
            if row['run'] not in primary:continue
            if row['model'] not in [m[0] for m in MODELS]:continue
            runs.setdefault(row['run'],{}).setdefault(row['model'],{})[int(row['pps'])]=row
    assert set(runs)==set(primary)
    runs={run:runs[run] for run in primary}
    width=480;panel=68;gap=19;left=49;right=12;top=33;bottom=27
    height=top+bottom+len(runs)*panel+(len(runs)-1)*gap
    c=canvas.Canvas(str(ROOT/'phase-curves.pdf'),pagesize=(width,height),pageCompression=1,invariant=1)
    c.setTitle('Continuous clock estimates against GNSS shadow PPS')
    c.setAuthor('Jonathan Yantis')
    c.setSubject('CC BY 4.0; unsmoothed fixed-prior-base errors, identical AP-common PPS; not UTC accuracy')
    xlegend=left;c.setFont('Helvetica',8)
    for _,label,color,dash in MODELS:
        c.setStrokeColor(color);c.setLineWidth(.9);c.setDash(dash)
        c.line(xlegend,height-12,xlegend+17,height-12);c.setFillColor(Color(.1,.1,.1))
        c.drawString(xlegend+21,height-15,label);xlegend+=127
    c.setDash([])
    for index,(run,data) in enumerate(runs.items()):
        y0=height-top-panel-index*(panel+gap);x0=left;w=width-left-right
        ids=[i for i in range(900) if all(data.get(n,{}).get(i,{}).get('verified')=='1'
            and data[n][i]['error_us'] for n,_,_,_ in MODELS)]
        if not ids:
            c.setDash([]);c.setLineWidth(.3);c.setStrokeColor(Color(.6,.6,.6));c.rect(x0,y0,w,panel,stroke=1,fill=0)
            c.setFillColor(Color(.25,.25,.25));c.setFont('Helvetica-Bold',8)
            c.drawCentredString(x0+w/2,y0+panel/2+3,f'{run}: no fully verified joint comparison')
            native=sum(all(data.get(n,{}).get(i,{}).get('admitted')=='1'
                and data[n][i]['error_us'] for n,_,_,_ in MODELS) for i in range(900))
            c.setFont('Helvetica',7)
            c.drawCentredString(x0+w/2,y0+panel/2-9,f'{native}/900 native-computed observations retained in the dataset')
            if index==len(runs)-1:
                for tick in [0,300,600,900]:c.drawCentredString(x0+w*tick/900,y0-11,str(tick))
            continue
        values=[float(data[n][i]['error_us']) for i in ids for n,_,_,_ in MODELS]
        low,high,yticks=ticks(min(values),max(values))
        def xy(r):
            seconds=(int(r['local_whole_us'])+float(r['local_fraction_us']))/1e6
            return x0+w*seconds/900,y0+panel*(float(r['error_us'])-low)/(high-low)
        c.setFont('Helvetica',7);c.setLineWidth(.3);c.setDash([])
        for tick in yticks:
            y=y0+panel*(tick-low)/(high-low);c.setStrokeColor(Color(.87,.88,.89));c.line(x0,y,x0+w,y)
            c.setFillColor(Color(.15,.15,.15));c.drawRightString(x0-5,y-2.2,f'{tick:g}')
        for tick in [0,300,600,900]:
            x=x0+w*tick/900;c.setStrokeColor(Color(.90,.91,.92));c.line(x,y0,x,y0+panel)
            if index==len(runs)-1:
                c.setFillColor(Color(.15,.15,.15));c.drawCentredString(x,y0-11,str(tick))
        c.setStrokeColor(Color(.45,.45,.45));c.rect(x0,y0,w,panel,stroke=1,fill=0)
        for name,_,color,dash in MODELS:
            c.setStrokeColor(color);c.setLineWidth(.7);c.setDash(dash)
            path=c.beginPath();previous=None
            for i in ids:
                x,y=xy(data[name][i])
                if previous is None or i!=previous+1:path.moveTo(x,y)
                else:path.lineTo(x,y)
                previous=i
            c.drawPath(path,stroke=1,fill=0)
        c.setDash([]);c.setFillColor(Color(.1,.1,.1));c.setFont('Helvetica-Bold',7.5)
        status='exploratory' if run=='P1' else 'locked validation'
        c.drawString(x0+4,y0+panel-9,f'{run}: {status}; {len(ids)}/900 common')
    c.setFont('Helvetica',8);c.drawCentredString(left+(width-left-right)/2,4,'Seconds after training cutoff')
    c.saveState();c.translate(10,height/2);c.rotate(90);c.drawCentredString(0,0,'PPS-relative error (us)');c.restoreState()
    c.showPage();c.save()
    print('Wrote phase-curves.pdf with identical common masks, explicit gaps and no smoothing/clipping')

if __name__=='__main__':main()

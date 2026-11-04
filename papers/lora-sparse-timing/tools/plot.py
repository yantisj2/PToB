#!/usr/bin/env python3
# Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""Vector figure; public dependency: reportlab. No filtering or smoothing."""
import csv,json
from pathlib import Path
from reportlab.pdfgen import canvas
from reportlab.lib.colors import HexColor, Color, black
P=Path(__file__).resolve().parents[1]
rows=list(csv.DictReader((P/'data/f288-phase.csv').open()))
r=json.loads((P/'results.json').read_text())
W,H=480,262;c=canvas.Canvas(str(P/'phase-results.pdf'),pagesize=(W,H))
c.setTitle('Complete 900-PPS fixed500 validation and physical SYNC age')
c.setAuthor('Jonathan Yantis');c.setSubject('Original figure CC BY 4.0; all 900 observations retained')
navy=HexColor('#17384c');teal=HexColor('#007c83');grey=HexColor('#d6dfe4')
def txt(x,y,s,size=8,color=navy):c.setFillColor(color);c.setFont('Helvetica',size);c.drawString(x,y,s)
def axes(x,y,w,h,ylims,yticks):
 for tick in yticks:
  yy=y+(tick-ylims[0])/(ylims[1]-ylims[0])*h
  c.setStrokeColor(grey);c.setLineWidth(.35);c.line(x,yy,x+w,yy);c.setFillColor(navy);c.setFont('Helvetica',7);c.drawRightString(x-6,yy-2,str(tick))
 c.setStrokeColor(navy);c.setLineWidth(.6);c.line(x,y,x+w,y);c.line(x,y,x,y+h)
 return lambda v:y+(v-ylims[0])/(ylims[1]-ylims[0])*h
txt(40,247,'(a) Every future PPS observation',9);x,y,w,h=40,137,420,95;fy=axes(x,y,w,h,(-1000,1000),[-1000,-500,0,500,1000])
for i,row in enumerate(rows):
 xx=x+i/899*w;yy=fy(float(row['phase_ns']));c.setFillColor(teal);c.circle(xx,yy,1,fill=1,stroke=0)
for t in [0,180,360,540,720,900]:
 xx=x+t/900*w;txt(xx-5,y-11,str(t),7)
txt(160,111,'PPS opportunity since declared boundary (s)',8)
c.saveState();c.translate(9,156);c.rotate(90);txt(0,0,'Phase (ns)',8);c.restoreState()
txt(40,94,'(b) Phase RMS and largest absolute phase by physical SYNC age',9)
x,y,w,h=40,28,420,55;fy=axes(x,y,w,h,(0,1000),[0,500,1000])
for i,(label,z) in enumerate(r['f288']['physical_sync_age_bins'].items()):
 xx=x+(i+.5)*w/6;v=z['RMS_ns'];m=z['max_absolute_ns'];c.setFillColor(teal);c.rect(xx-12,y,24,fy(v)-y,fill=1,stroke=0)
 c.setStrokeColor(navy);c.setLineWidth(1);c.line(xx,fy(v),xx,fy(m));c.line(xx-5,fy(m),xx+5,fy(m));txt(xx-16,y-11,label.replace('..','-'),7)
 txt(xx-12,fy(m)+3,str(round(m)),7)
txt(145,2,'Seconds since latest authenticated physical SYNC; n = 150 per bin',8)
c.saveState();c.translate(9,33);c.rotate(90);txt(0,0,'Phase (ns)',8);c.restoreState()
c.showPage();c.save();print('phase-results.pdf: all 900 samples; RMS bars, maxima caps; fixed 0-1000 ns age scale')

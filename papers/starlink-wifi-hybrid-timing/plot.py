#!/usr/bin/env python3
"""Vector scientific plots from selected derivatives. Requires ReportLab.
Copyright (c) 2026 Jonathan Yantis. SPDX-License-Identifier: MIT
"""
import csv,json,math
from pathlib import Path
from reportlab.graphics.shapes import Drawing,String,Line,Rect
from reportlab.graphics.charts.lineplots import LinePlot
from reportlab.graphics import renderPDF
from reportlab.lib import colors

def plot(folder):
 rows=list(csv.DictReader((folder/'pps.csv').open()));r=json.loads((folder/'results.json').read_text())
 good=[p for p in rows if p['valid']=='1'];last=r['last_admitted_training_receive_us']
 raw=[(int(p['local_us'])/1e6,float(p['raw_phase_us'])/1000) for p in good]
 filtered=[(int(p['local_us'])/1e6,float(p['filtered_phase_us'])/1000) for p in good]
 future=[((int(p['local_us'])-last)/1e6,float(p['frozen_error_us'])) for p in good if p['frozen_error_us']]
 futures=[future]
 for name in ('same-boot-01','same-boot-02','same-boot-longspan'):
  data_folder=folder/'controls'/name
  if (data_folder/'results.json').exists():
   cr=json.loads((data_folder/'results.json').read_text());crs=list(csv.DictReader((data_folder/'pps.csv').open()))
   futures.append([((int(p['local_us'])-cr['last_admitted_training_receive_us'])/1e6,float(p['frozen_error_us'])) for p in crs if p['valid']=='1' and p['frozen_error_us']])
 d=Drawing(480,238);blue=colors.HexColor('#1e5d8b');orange=colors.HexColor('#b84e24')
 def panel(y,height,data,xmax,ymin,ymax,title,xlabel,ylabel,steps):
  x=48;w=420
  d.add(String(x,y+height+12,title,fontName='Helvetica-Bold',fontSize=9))
  p=LinePlot();p.x=x;p.y=y;p.width=w;p.height=height;p.data=data
  p.xValueAxis.valueMin=0;p.xValueAxis.valueMax=xmax;p.xValueAxis.valueSteps=steps
  p.yValueAxis.valueMin=ymin;p.yValueAxis.valueMax=ymax
  p.xValueAxis.labels.fontSize=7;p.yValueAxis.labels.fontSize=7
  p.xValueAxis.strokeColor=colors.grey;p.yValueAxis.strokeColor=colors.grey
  p.yValueAxis.visibleGrid=True;p.yValueAxis.gridStrokeColor=colors.HexColor('#dedede');p.yValueAxis.gridStrokeWidth=.3
  for i,color in enumerate((orange,blue,colors.HexColor('#9956a6'),colors.HexColor('#18806a'))):
   if i<len(data):p.lines[i].strokeColor=color;p.lines[i].strokeWidth=.65 if i==0 else 1.0
  if len(data)==4:p.lines[3].strokeDashArray=[3,2]
  d.add(p);d.add(String(x+w/2,y-24,xlabel,fontSize=8,textAnchor='middle'))
  d.add(String(3,y+height/2,ylabel,fontSize=8))
  return x,y,w,height
 lo=5*math.floor(min(v for t,v in raw)/5);hi=5*math.ceil(max(v for t,v in raw)/5)
 panel(151,66,[raw,filtered],600,lo,hi,'A   S1 runtime NTP phase at GNSS input','Capture elapsed (s)','ms',[0,100,200,300,400,500,600])
 d.add(Line(285,226,299,226,strokeColor=orange,strokeWidth=1));d.add(String(303,223,'latest reply',fontSize=7))
 d.add(Line(365,226,379,226,strokeColor=blue,strokeWidth=1));d.add(String(383,223,'MIN_RTT8',fontSize=7))
 peak=max(abs(v) for series in futures for t,v in series);step=25 if peak>10 else 1;bound=max(1.,step*math.ceil(1.1*peak/step));xmax=50*math.ceil(max(t for series in futures for t,v in series)/50)
 x,y,w,h=panel(35,66,futures,xmax,-bound,bound,'B   Sealed frozen models, model-specific GNSS bases','Age since final admitted NTP training reply (s)','us',list(range(0,xmax+1,50)))
 if len(futures)==4:
  for px,py,label,color,dashed in ((246,94,'S1 first',orange,False),(246,84,'S2 repeat',blue,False),(350,94,'S3 fixed warmup',colors.HexColor('#9956a6'),False),(350,84,'S2-L (same S2 PPS)',colors.HexColor('#18806a'),True)):
   line=Line(px,py+2,px+14,py+2,strokeColor=color,strokeWidth=1)
   if dashed:line.strokeDashArray=[3,2]
   d.add(line);d.add(String(px+18,py,label,fontSize=7))
 for threshold in (-1,1):
  py=y+(threshold+bound)/(2*bound)*h
  d.add(Line(x,py,x+w,py,strokeColor=colors.HexColor('#aab2bb'),strokeWidth=.5,strokeDashArray=[2,2]))
 px=x+60/xmax*w;d.add(Line(px,y,px,y+h,strokeColor=colors.grey,strokeWidth=.6,strokeDashArray=[3,2]))
 d.add(String(px+3,y+h-9,'60 s lease',fontSize=7,fillColor=colors.grey))
 renderPDF.drawToFile(d,str(folder/'timing-results.pdf'))

if __name__=='__main__':plot(Path(__file__).resolve().parent)

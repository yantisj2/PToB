#!/usr/bin/env python3
"""Rebuild V14 figures/tables from included sanitized data into a fresh output folder.

MIT, copyright (c) 2026 Jonathan Yantis. Adapted from the original figure
derivation; raw extraction removed. Original scientific exports are CC BY 4.0.
"""
import argparse,csv,hashlib,json,math,sys
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
DATA=Path(__file__).resolve().parents[1]/'evidence/v14/2026-10-12'
SHA=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def writecsv(p,rows):
 with p.open('w',newline='') as f:
  w=csv.DictWriter(f,list(rows[0]));w.writeheader();w.writerows(rows)
def picture_begin(w,h):return [r'\setlength{\unitlength}{1pt}',fr'\begin{{picture}}({w},{h})']
def line(out,x1,y1,x2,y2,color='black',width=.5):
 # arbitrary vector via qbezier, linear midpoint
 out.append(fr'{{\color{{{color}}}\linethickness{{{width}pt}}\qbezier({x1:.3f},{y1:.3f})({(x1+x2)/2:.3f},{(y1+y2)/2:.3f})({x2:.3f},{y2:.3f})}}')
def label(out,x,y,text,w=50,align='l'):out.append(fr'\put({x:.3f},{y:.3f}){{\makebox({w},10)[{align}]{{\scriptsize {text}}}}}')
def dot(out,x,y,color='blue!65!black',d=1.65):out.append(fr'\put({x:.3f},{y:.3f}){{\color{{{color}}}\circle*{{{d}}}}}')
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--out',required=True,type=Path);a=ap.parse_args();BASE=a.out.resolve()
 if BASE.exists():ap.error('Use a fresh output directory to preserve reviewed exports')
 BASE.mkdir(parents=True)
 src={p.name:SHA(p) for p in [DATA/'pps-sanitized.csv',DATA/'diagnostic-sanitized.csv',DATA/'summary.json',DATA/'wire-layouts.csv']}
 for name in ['pps-sanitized.csv','diagnostic-sanitized.csv','summary.json','wire-layouts.csv']:(BASE/name).write_bytes((DATA/name).read_bytes())
 rows=list(csv.DictReader((BASE/'pps-sanitized.csv').open()));s=json.loads((BASE/'summary.json').read_text());x=np.array([float(r['time_s']) for r in rows]);valid=np.array([r['reference_valid']=='1' and r['map_available']=='1' for r in rows]);invalid=np.array([r['reference_valid']=='0' and r['map_available']=='1' for r in rows]);expired=np.array([r['map_available']=='0' for r in rows]);e=np.array([float(r['phase_ns']) if r['phase_ns'] else np.nan for r in rows]);age=np.array([float(r['update_age_ms']) for r in rows]);v=e[valid]
 assert len(rows)==504 and len(v)==436 and int(invalid.sum())==65 and int(expired.sum())==3
 calculated=dict(n=len(v),bias_ns=float(v.mean()),rms_including_bias_ns=float(np.sqrt(np.mean(v*v))),jitter_rms_ns=float(np.std(v)),abs_p50_ns=float(np.quantile(abs(v),.5)),abs_p95_ns=float(np.quantile(abs(v),.95)),abs_p99_ns=float(np.quantile(abs(v),.99)),abs_max_ns=float(abs(v).max()))
 for k,val in calculated.items():assert math.isclose(val,s['reference_valid_causal'][k],rel_tol=1e-10,abs_tol=1e-8),(k,val)
 plt.rcParams.update({'font.family':'DejaVu Sans','font.size':10,'axes.spines.top':False,'axes.spines.right':False,'pdf.fonttype':42})
 fig,axs=plt.subplots(3,1,figsize=(7.2,5.4),sharex=True,gridspec_kw={'height_ratios':[2,1,1]})
 gaps=[(x[i-1],x[i]) for i in range(1,len(x)) if x[i]-x[i-1]>1.5]
 axs[0].scatter(x[valid],e[valid],s=7,color='#205e8a');axs[0].axhline(0,color='gray',lw=.5);axs[0].set_ylim(-220,220);axs[0].set_ylabel('Causal phase (ns)')
 axs[1].scatter(x[invalid],e[invalid]/1e6,s=8,color='#aa442a');axs[1].set_ylabel('Invalid ref. (ms)')
 axs[2].scatter(x,age,s=5,color='#205e8a');axs[2].axhline(500,color='#aa442a',ls='--',lw=.8);axs[2].scatter(x[expired],age[expired],s=28,marker='x',color='black');axs[2].set_ylabel('Update age (ms)');axs[2].set_xlabel('Leaf hardware-counter elapsed time (s)')
 for ax in axs:
  ax.set_xlim(0,s['duration_s']);ax.grid(alpha=.16)
  for lo,hi in gaps:ax.axvspan(lo,hi,color='gray',alpha=.10)
 fig.tight_layout();fig.savefig(BASE/'phase-availability.pdf',metadata={'CreationDate':None,'ModDate':None});fig.savefig(BASE/'phase-availability.png',dpi=200);plt.close(fig)
 # Embedded vector counterpart: no extra project file needed by native editor.
 out=picture_begin(454,282);left,right=62,445;xx=lambda t:left+t/s['duration_s']*(right-left)
 panels=[(158,252,-220,220),(91,134,-.5,21),(29,72,0,1200)]
 for low,high,emin,emax in panels:
  for lo,hi in gaps:out.append(fr'\put({xx(lo):.3f},{low}){{\color{{gray!15}}\rule{{{xx(hi)-xx(lo):.3f}pt}}{{{high-low}pt}}}}')
  line(out,left,low,right,low);line(out,left,low,left,high)
 for t in range(0,601,100):
  xt=xx(t);line(out,xt,29,xt,25);label(out,xt-15,11,str(t),30,'c')
 for j,(low,high,emin,emax) in enumerate(panels):
  yy=lambda z:low+(z-emin)/(emax-emin)*(high-low)
  vals=[(-200,0,200),(0,10,20),(0,500,1000)][j]
  for val in vals:label(out,18,yy(val)-5,str(val),37,'r');line(out,left,yy(val),right,yy(val),'gray!35',.3)
  mask=[valid,invalid,np.ones(len(x),dtype=bool)][j];ys=[e,e/1e6,age][j]
  for tx,ty in zip(x[mask],ys[mask]):dot(out,xx(tx),yy(ty),'red!60!black' if j==1 else 'blue!65!black')
  if j==2:
   line(out,left,yy(500),right,yy(500),'red!70!black',.7)
   for tx,ty in zip(x[expired],age[expired]):dot(out,xx(tx),yy(ty),'black',3.7)
 label(out,62,264,'(a) Valid-reference causal phase (ns): 436 samples',340)
 label(out,62,139,'(b) Reference-invalid phase (ms): 65 causal samples',360)
 label(out,62,76,'(c) Local update age (ms); black = three expired edges',380)
 label(out,100,-3,'Leaf hardware-counter elapsed time (s); gray = gaps between observed edges',350)
 out.append(r'\end{picture}');(BASE/'phase-availability-inline.tex').write_text('\n'.join(out)+'\n')
 d=list(csv.DictReader((BASE/'diagnostic-sanitized.csv').open()));out=picture_begin(454,205)
 fig,axs=plt.subplots(2,1,figsize=(7.2,3.6),sharex=True)
 for row,(role,color) in enumerate([('RC','blue!65!black'),('leaf','red!60!black')]):
  precision=[r for r in d if r['role']==role and r['op']=='precision'];network=[r for r in d if r['role']==role and r['op']=='network'];p0=float(precision[0]['device_uptime_s']);n0=float(network[0]['device_uptime_s']);lo=35 if row else 120;hi=lo+55
  line(out,62,lo,445,lo);label(out,0,lo+22,role,30,'l')
  xs=[float(r['device_uptime_s'])-p0 for r in precision];ys=[int(r['expiries'])-int(precision[0]['expiries']) for r in precision]
  axs[row].plot(xs,ys,'o',ms=3,color='#205e8a' if row==0 else '#aa442a',label='Sampled expiry count increment');axs[row].set_ylabel(role+' expiry delta')
  for tx,ty in zip(xs,ys):dot(out,62+tx/620*383,lo+ty/12*55,color,3)
  for r in network:
   if int(r['binds'])>int(network[0]['binds']):
    tx=float(r['device_uptime_s'])-n0
    # only first sample at each new receipt count
    earlier=[z for z in network if float(z['device_uptime_s'])<float(r['device_uptime_s']) and int(z['binds'])==int(r['binds'])]
    if not earlier:
     axs[row].axvline(tx,color='gray',ls=':',label='First sampled renewed receipt')
     line(out,62+tx/620*383,lo,62+tx/620*383,hi,'gray',.8);label(out,62+tx/620*383-28,hi+2,'bind '+r['binds'],60,'c')
  for val in [0,6,12]:label(out,38,lo+val/12*55-5,str(val),20,'r')
 label(out,62,191,'Sampled expiry/recovery counter increments; vertical marks = new admission receipts',392)
 for t in range(0,601,100):label(out,62+t/620*383-15,19,str(t),30,'c')
 label(out,95,1,'Device-counter elapsed since first diagnostic of each type (s)',340)
 out.append(r'\end{picture}');(BASE/'renewal-expiry-inline.tex').write_text('\n'.join(out)+'\n')
 axs[-1].set_xlabel('Device-counter elapsed since first diagnostic of each type (s)')
 for ax in axs:ax.grid(alpha=.16);ax.set_xlim(0,620)
 fig.tight_layout();fig.savefig(BASE/'renewal-expiry.pdf',metadata={'CreationDate':None,'ModDate':None});fig.savefig(BASE/'renewal-expiry.png',dpi=200);plt.close(fig)
 writecsv(BASE/'result-table.csv',[dict(quantity=k,value=v) for k,v in calculated.items()]+[dict(quantity='observed_edges',value=504),dict(quantity='nominal_boundaries',value=602),dict(quantity='usable_coverage',value=436/602)])
 files={p.name:SHA(p) for p in sorted(BASE.iterdir()) if p.is_file() and p.name!='derivation-manifest.json'}
 (BASE/'derivation-manifest.json').write_text(json.dumps(dict(schema=1,metric='causal_signed_modulo_second_PPS_phase',quantile='numpy linear interpolation (n-1)p',input_hashes=src,outputs=files,python=sys.version,numpy=np.__version__,matplotlib=matplotlib.__version__,calculated=calculated,sanitization='No silicon, boots, factory MAC, absolute epoch, private path, enrollment or console record exported',no_interpolation_of_missing_output=True),indent=2)+'\n')
 print(json.dumps(calculated))
if __name__=='__main__':main()

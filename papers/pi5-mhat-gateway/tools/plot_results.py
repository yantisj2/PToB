#!/usr/bin/env python3
"""Render the selected final population. Jonathan Yantis. MIT; figures CC BY 4.0."""
import csv,json,os
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];PAPER=Path(__file__).resolve().parents[1]
os.environ.setdefault('MPLCONFIGDIR',str(ROOT/'artifacts/pi5/paper-plot-cache'))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
BASE=ROOT/'evidence/pi5/2026-11-02'
with (BASE/'final-edges.csv').open() as f:e=list(csv.DictReader(f))
with (BASE/'final-status.csv').open() as f:s=list(csv.DictReader(f))
plt.rcParams.update({'font.size':8,'axes.spines.top':False,'axes.spines.right':False,'pdf.fonttype':42})
fig,axs=plt.subplots(3,1,figsize=(7,4.65),sharex=True,layout='constrained')
x=[int(r['elapsed_ns'])/1e9 for r in e];phase=[int(r['phase_ns'])/1000 for r in e]
axs[0].plot(x,phase,'.',ms=2.3,color='#176579')
for y in [-1,1]:axs[0].axhline(y,color='#a54d32',lw=.65,ls='--')
axs[0].set_ylabel('GPIO phase (µs)');axs[0].set_title('600 new pulses · RMS 0.872 µs · absolute P99 4.809 µs · max 7.017 µs',loc='left',fontsize=9)
x=[int(r['elapsed_ns'])/1e9 for r in s]
for key,label,color in [('hat_fit_rms_ns','nRF fit RMS','#2f7349'),('chrony_tracking_rms_ns','Chrony RMS estimate','#ac6428'),('chrony_source_stddev_ns','PTOB regression SD','#745a9b')]:
 axs[1].plot(x,[float(r[key]) if r[key] else float('nan') for r in s],label=label,lw=.75,color=color)
axs[1].set_yscale('log');axs[1].set_ylabel('Estimates (ns)');axs[1].legend(ncol=3,fontsize=7,loc='upper right')
axs[2].plot(x,[float(r['chrony_frequency_ppm']) for r in s],lw=.75,color='#4d555e');axs[2].set_ylabel('Chrony freq. (ppm)');axs[2].set_xlabel('Elapsed fixed-window time (s)')
for ax in axs:ax.set_xlim(0,600);ax.grid(alpha=.2)
(PAPER/'figures').mkdir(exist_ok=True);fig.savefig(PAPER/'figures/final-results.pdf',metadata={'Title':'Final Pi/M-HAT observation','Author':'Jonathan Yantis','Subject':'CC BY 4.0; GPIO phase is not independent UTC error'})
preview=ROOT/'artifacts/pi5/paper-build';preview.mkdir(parents=True,exist_ok=True);fig.savefig(preview/'final-results.png',dpi=170)

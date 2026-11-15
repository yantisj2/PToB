// Copyright 2026 Jonathan Yantis. MIT. Synthetic rendering, not device evidence.
'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
class Element {
  constructor(tag='div'){this.tag=tag;this.children=[];this.attrs={};this.textContent='';this.clientWidth=600;this.value='60';this.parts=new Map();this.listeners={};}
  setAttribute(k,v){this.attrs[k]=String(v);}
  removeAttribute(k){delete this.attrs[k];}
  append(...items){this.children.push(...items);}
  replaceChildren(...items){this.children=[...items];}
  contains(){return false;}
  addEventListener(type,fn){this.listeners[type]=fn;}
  getBoundingClientRect(){return {left:0,width:300};}
  querySelector(key){if(!this.parts.has(key))this.parts.set(key,new Element(key));return this.parts.get(key);}
  all(tag){return [...(this.tag===tag?[this]:[]),...this.children.flatMap(c=>c instanceof Element?c.all(tag):[])];}
}
const elements=new Map(),get=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id);};
const fixture={window_s:60,population:4,gaps:1,sample_age_s:0,statistics:{},points:[
  {at_ms:1000,segment:0,source_offset_ns:0,source_sample_age_s:5,source_poll_s:4},
  {at_ms:2000,segment:0,source_offset_ns:null},
  {at_ms:3000,segment:0,source_offset_ns:-500},
  {at_ms:4000,segment:1,source_offset_ns:9000,source_sample_age_s:5,source_poll_s:4}
]};
const context=vm.createContext({document:{getElementById:get,createElement:t=>new Element(t),createElementNS:(_,t)=>new Element(t),createTextNode:text=>Object.assign(new Element('text'),{textContent:text}),activeElement:null},
  window:{addEventListener(){}},setInterval(){},clearTimeout(){},setTimeout(){},AbortSignal,
  fetch:async()=>({ok:true,json:async()=>fixture})});
vm.runInContext(fs.readFileSync(path.join(__dirname,'../static/charts.js'),'utf8')+'\nTimingCharts.start();',context);
setImmediate(async()=>{
  try{
    const plot=get('chart-offset').querySelector('.plot');
    const dots=plot.all('circle');
    assert.equal(dots.length,3,'all isolated real observations must be visible');
    assert(dots.some(d=>d.attrs['aria-label'].includes('9,000')),'tail extremum remains visible');
    const lines=plot.all('path');
    assert(lines.every(p=>!p.attrs.d.includes('L')),'missing samples and source boundaries must not be bridged');
    assert.equal((lines[0].attrs.d.match(/M/g)||[]).length,3);
    assert.match(get('chart-offset').querySelector('.readout').textContent,/chrony sample age 5 s; poll 4 s/);
    assert.match(get('chart-drift').querySelector('.plot').children[0].textContent,/No valid samples/);
    fixture.points=[
      {at_ms:1000,context:'a',segment:0,series_segments:{receiver:0,tracking:0,source:0,pps:0},fit_drift_ppb:1,tracking_rms_ns:10},
      {at_ms:2000,context:'a-held',segment:1,series_segments:{receiver:0,tracking:0,source:0,pps:0},fit_drift_ppb:2,tracking_rms_ns:null,
       reported_tracking_rms_ns:11,tracking_mode:'holdover',timing_reason:'Receiver clock offer withdrawn'},
      {at_ms:3000,context:'a',segment:2,series_segments:{receiver:0,tracking:0,source:0,pps:0},fit_drift_ppb:3,tracking_rms_ns:12},
      {at_ms:4000,context:'b',segment:3,series_segments:{receiver:1,tracking:1,source:1,pps:1},fit_drift_ppb:4,tracking_rms_ns:9000}
    ];
    await vm.runInContext('TimingCharts.refresh()',context);
    const drift=get('chart-drift').querySelector('.plot').all('path')[0].attrs.d;
    assert.equal((drift.match(/L/g)||[]).length,2,'admission flags must not break an unchanged valid receiver fit');
    assert.equal((drift.match(/M/g)||[]).length,2,'a real source change must still break the receiver line');
    const rms=get('chart-rms').querySelector('.plot'),held=rms.all('path').find(p=>p.attrs['stroke-dasharray']);
    assert(held,'only actual separately reported estimates may fill a holdover interval');
    assert.equal((held.attrs.d.match(/L/g)||[]).length,2,'holdover connects to adjacent observations without a false hole');
    assert(rms.all('li').some(e=>e.textContent.includes('reported during holdover')));
    const picture=rms.all('svg')[0];
    picture.listeners.pointermove({clientX:(64+522/3)/2});
    assert.match(get('chart-rms').querySelector('.readout').textContent,/11 ns \(holdover estimate\)/);
    assert.match(get('chart-rms').querySelector('.readout').textContent,/Receiver clock offer withdrawn/);
    picture.listeners.pointermove({clientX:586/2});
    assert.match(get('chart-rms').querySelector('.readout').textContent,/9,000 ns/,'pointer inspection must scale CSS coordinates into the SVG viewBox');
    fixture.points=[{at_ms:1000,segment:0,tracking_rms_ns:null}];
    await vm.runInContext('TimingCharts.refresh()',context);
    assert.match(get('chart-rms').querySelector('.plot').children[0].textContent,/No valid samples/,'historical unknowns are never fabricated');
    console.log('Real gaps, separate holdover estimates, source scope, extrema and scaled pointer inspection render correctly.');
  }catch(e){console.error(e);process.exitCode=1;}
});

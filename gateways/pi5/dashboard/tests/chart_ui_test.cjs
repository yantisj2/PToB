// Copyright 2026 Jonathan Yantis. MIT. Synthetic rendering, not device evidence.
'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
class Element {
  constructor(tag='div'){this.tag=tag;this.children=[];this.attrs={};this.textContent='';this.clientWidth=600;this.value='60';this.parts=new Map();}
  setAttribute(k,v){this.attrs[k]=String(v);}
  removeAttribute(k){delete this.attrs[k];}
  append(...items){this.children.push(...items);}
  replaceChildren(...items){this.children=[...items];}
  contains(){return false;}
  addEventListener(){}
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
setImmediate(()=>{
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
    console.log('Sparse observations, zero values, tail extrema and real gaps render correctly.');
  }catch(e){console.error(e);process.exitCode=1;}
});

// Copyright 2026 Jonathan Yantis. MIT. Synthetic observation-boundary regression.
'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
class Element {
  constructor(){this.children=[];this.textContent='';}
  append(...items){this.children.push(...items);}
  replaceChildren(...items){this.children=[...items];}
  contents(){return this.textContent+this.children.map(x=>x.contents()).join(' ');}
}
const elements=new Map(),get=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id);};
const context=vm.createContext({document:{getElementById:get,createElement:()=>new Element()},console,
  fetch:async()=>{throw Error('synthetic offline');},AbortSignal,fixture:null});
const script=fs.readFileSync(path.join(__dirname,'../static/app.js'),'utf8');
vm.runInContext(script.slice(0,script.indexOf("$('refresh').addEventListener")),context);
context.fixture={receiver:{data:{uptime_ms:10000}},source_status:{current:false,stale:false,root:'0000000000000001',
  parent:'0000000000000002',discovery_state:2,recovery_remaining_s:42,last_valid_age_s:10,
  reports:[{root:'0000000000000001',age_ms:500,fresh:true,root_boot:'test-boot',epoch:1,hot:'0000000000000000'}]}};
function draw(stale){vm.runInContext(`sourceReport(fixture,${stale})`,context);return get('source-facts').contents();}
assert.match(draw(false),/Rediscovery in at most 42 s/);
for(const receiverStale of [false,true]){
 context.fixture.source_status.stale=receiverStale;
 const facts=draw(!receiverStale);
 assert.doesNotMatch(facts,/Rediscovery in at most|native recovery continues|received 0\.5 s ago/);
 assert.match(facts,/Current progress unknown/);
 assert.match(facts,/receipt age at last observation/);
 assert.doesNotMatch(get('gm-reports').contents(),/recent receipt/);
}
context.fixture.source_status.stale=false;
vm.runInContext('lastData=fixture',context);
vm.runInContext('refresh()',context).then(()=>{
 assert.match(get('source-state').contents(),/Stale/);
 assert.match(get('source-facts').contents(),/Current progress unknown/);
 console.log('Stale collector, stale receiver and lost dashboard connection withdraw current source claims.');
}).catch(e=>{console.error(e);process.exitCode=1;});

// Copyright 2026 Jonathan Yantis. MIT. Operational-state regression from the reported holdover screen.
'use strict';
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const context=vm.createContext({document:{getElementById:()=>({})}});
const script=fs.readFileSync(path.join(__dirname,'../static/app.js'),'utf8');
vm.runInContext(script.slice(0,script.indexOf('function render(')),context);
const states={'chrony.service':'active','ptob-ptp-wifi.service':'active','ptob-ptp4l.service':'active'};
const base={chain:{complete:true},ethernet_link:false,
  clock_chain:{transfer:{fresh:true,running:true},server:{fresh:true,ready:true,running:true}}};
function draw(data,live=true,services=states){context.data=data;context.live=live;context.states=services;return vm.runInContext('networkState(data,states,live)',context);}
const holdover=structuredClone(base);holdover.chain={complete:false,chrony_holdover:true};
assert.equal(draw(holdover).state,'wait');assert.equal(draw(holdover).title,'PToB holdover');
assert.match(draw(holdover).note,/holdover or network UTC/);
assert.equal(draw({...holdover,chain:{complete:false}}).title,'PToB unavailable');
const withheld=structuredClone(base);withheld.clock_chain.server={fresh:true,ready:false,running:false,reason:'kernel TAI unavailable'};
assert.equal(draw(withheld).state,'wait');assert.match(draw(withheld).note,/TAI unavailable/);
for(const key of ['transfer','server']){
 const stale=structuredClone(base);stale.clock_chain[key].fresh=false;
 assert.equal(draw(stale).title,'PTP state unavailable');assert.equal(draw(stale).state,'wait');
}
assert.equal(draw({...base,clock_chain:undefined}).state,'wait');
assert.equal(draw(base,false).title,'Stale');assert.equal(draw(base,false).state,'wait');
assert.equal(draw(base,true,{}).title,'NTP unavailable');
assert.equal(draw(base).state,'good');assert.match(draw(base).title,/Ethernet ready/);assert.match(draw(base).note,/Ethernet has no link/);
assert.equal(draw({...base,ethernet_link:true}).title,'NTP / Ethernet PTP active');
console.log('PToB loss, holdover, withdrawn/stale PTP guards and absent Ethernet link cannot appear as active PToB transfer.');

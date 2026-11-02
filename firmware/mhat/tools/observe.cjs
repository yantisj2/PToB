#!/usr/bin/env node
// Copyright (c) 2026 Jonathan Yantis. MIT. Bounded exact-module console capture.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const {parseArgs}=require('node:util');
async function main(){
 process.umask(0o077);
 const {values:v}=parseArgs({options:{module:{type:'string',default:path.join(os.homedir(),'.config/ptob/mhat.json')},out:{type:'string'},seconds:{type:'string',default:'30'},source:{type:'string'}}});
 const p=JSON.parse(fs.readFileSync(v.module)),id=Buffer.from(p.device_id,'hex');
 if(p.platform!=='b5som'||id.length!==12)throw Error('Exact B5 module binding required');
 const silicon=id.readUInt32LE(8).toString(16).padStart(8,'0')+id.readUInt32LE(4).toString(16).padStart(8,'0');
 const serial=(silicon+'00000000').toUpperCase();
 const seconds=Number(v.seconds);if(!Number.isInteger(seconds)||seconds<5||seconds>300)throw Error('Use 5..300 seconds');
 if(v.source&&!/^[a-f0-9]{64}$/.test(v.source))throw Error('Source binding requires the full receipt SHA256');
 const root=path.resolve(__dirname,'../../..'),artifacts=fs.realpathSync(path.join(root,'artifacts')),out=path.resolve(v.out||'');
 if(!out.startsWith(artifacts+path.sep)||fs.existsSync(out))throw Error('Use new ignored report path');
 const {SerialPort}=require(path.join(artifacts,'pi5/particle-tools/node_modules/serialport'));
 const matches=(await SerialPort.list()).filter(r=>r.serialNumber===serial&&r.vendorId==='2fe3'&&r.productId==='0100');
 if(matches.length!==1)throw Error('Expected one exact runtime USB identity');
 const rows=[],errors=[];let pending='';
 const port=new SerialPort({path:matches[0].path,baudRate:115200,autoOpen:false,lock:true});
 await new Promise((resolve,reject)=>port.open(e=>e?reject(e):resolve()));
 try{
  await new Promise((resolve,reject)=>port.set({dtr:true,rts:false},e=>e?reject(e):resolve()));
  port.on('data',data=>{pending+=data.toString('utf8');if(pending.length>16000){errors.push('oversized console fragment');pending='';}
   for(;;){let end=pending.indexOf('\n');if(end<0)break;const line=pending.slice(0,end).trim();pending=pending.slice(end+1);if(!line)continue;
    try{const row=JSON.parse(line);if(row.device_id!==silicon||row.particle_id!==p.device_id||row.target!=='mhat-b504e'||(v.source&&row.source!==v.source))throw Error('runtime binding mismatch');rows.push({...row,host_received_utc:new Date().toISOString()});}catch(e){errors.push(e.message);}
   }
  });
  await new Promise((resolve,reject)=>{const timer=setTimeout(resolve,seconds*1000);port.once('error',e=>{clearTimeout(timer);reject(e)});});
 }finally{if(port.isOpen)await new Promise(resolve=>port.close(()=>resolve()));}
 const report={schema:1,device_id:p.device_id,runtime_usb_serial:serial,duration_s:seconds,errors,rows,
  summary:{records:rows.length,boots:[...new Set(rows.map(r=>r.boot))],clock_valid_records:rows.filter(r=>r.clock_valid).length,
   pps_first:rows[0]?.pps_emitted,pps_last:rows.at(-1)?.pps_emitted,last:rows.at(-1)}};
 fs.writeFileSync(out,JSON.stringify(report,null,2)+'\n',{flag:'wx',mode:0o600});
 console.log(JSON.stringify({...report.summary,report:out},null,2));
 if(errors.length||!rows.length)process.exitCode=2;
}
main().catch(e=>{console.error(e.message);process.exitCode=1});

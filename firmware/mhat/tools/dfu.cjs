#!/usr/bin/env node
// Copyright 2026 Jonathan Yantis. MIT. Exact-image USB bootloader request.
const fs=require('node:fs'),path=require('node:path'),os=require('node:os');
const {parseArgs}=require('node:util');
async function main(){
 process.umask(0o077);
 const {values:v}=parseArgs({options:{module:{type:'string',default:path.join(os.homedir(),'.config/ptob/mhat.json')},source:{type:'string'},out:{type:'string'}}});
 if(!/^[a-f0-9]{64}$/.test(v.source||''))throw Error('Require full installed --source SHA256');
 const root=path.resolve(__dirname,'../../..'),artifacts=fs.realpathSync(path.join(root,'artifacts')),out=path.resolve(v.out||'');
 if(!out.startsWith(artifacts+path.sep)||fs.existsSync(out))throw Error('Use new ignored report');
 const profile=JSON.parse(fs.readFileSync(v.module)),id=Buffer.from(profile.device_id,'hex');
 if(profile.platform!=='b5som'||id.length!==12)throw Error('Exact B5 binding required');
 const silicon=id.readUInt32LE(8).toString(16).padStart(8,'0')+id.readUInt32LE(4).toString(16).padStart(8,'0');
 const serial=(silicon+'00000000').toUpperCase();
 const {SerialPort}=require(path.join(artifacts,'pi5/particle-tools/node_modules/serialport'));
 const matches=(await SerialPort.list()).filter(r=>r.serialNumber===serial&&r.vendorId==='2fe3'&&r.productId==='0100');
 if(matches.length!==1)throw Error('Require one exact runtime USB identity');
 const port=new SerialPort({path:matches[0].path,baudRate:115200,autoOpen:false,lock:true});
 const report={schema:1,utc:new Date().toISOString(),device_id:profile.device_id,source:v.source,operation:'guarded_usb_dfu_request',firmware_write:false};
 const save=()=>fs.writeFileSync(out,JSON.stringify(report,null,2)+'\n',{mode:0o600});
 try{
  await new Promise((resolve,reject)=>port.open(e=>e?reject(e):resolve()));
  await new Promise((resolve,reject)=>port.set({dtr:true,rts:false},e=>e?reject(e):resolve()));
  const row=await new Promise((resolve,reject)=>{
   let pending='';const timer=setTimeout(()=>reject(Error('No bound runtime status')),6000);
   port.on('data',data=>{pending=(pending+data.toString('utf8')).slice(-16000);for(;;){const i=pending.indexOf('\n');if(i<0)break;const line=pending.slice(0,i);pending=pending.slice(i+1);let r;try{r=JSON.parse(line);}catch{continue;}
    if(r.target==='mhat-b504e'&&r.device_id===silicon&&r.particle_id===profile.device_id&&r.source===v.source&&/^[a-f0-9]{16}$/.test(r.boot)&&r.dfu_status===0){clearTimeout(timer);resolve(r);}
   }});port.once('error',e=>{clearTimeout(timer);reject(e);});
  });
  report.boot=row.boot;save();
  await new Promise((resolve,reject)=>port.write(`dfu ${profile.device_id} ${v.source} ${row.boot}\n`,e=>e?reject(e):resolve()));
  await new Promise((resolve,reject)=>port.drain(e=>e?reject(e):resolve()));
  report.request_sent=true;save();
  await new Promise(resolve=>setTimeout(resolve,4000));
 }finally{if(port.isOpen)await new Promise(resolve=>port.close(()=>resolve()));}
 const usb=require(path.join(artifacts,'pi5/particle-tools/node_modules/particle-usb'));usb.config({requestTimeout:10000});
 let dev;
 try{dev=await usb.openDeviceById(profile.device_id,{includeDfu:true});if(dev.id!==profile.device_id||dev.type!=='b5som'||!dev.isInDfuMode)throw Error('Exact DFU identity not observed');report.status='EXACT_DFU_VERIFIED';save();}
 finally{if(dev)await dev.close({processPendingRequests:false,timeout:3000});}
 console.log(JSON.stringify({status:report.status,report:out}));
}
main().catch(e=>{console.error(e.message);process.exitCode=1});

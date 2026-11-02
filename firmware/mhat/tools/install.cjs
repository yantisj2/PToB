#!/usr/bin/env node
// Copyright (c) 2026 Jonathan Yantis. MIT. Exact B5 SoM system-region installer.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const {parseArgs}=require('node:util');
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function crc32(buf){let c=0xffffffff;for(const b of buf){c^=b;for(let n=0;n<8;n++)c=(c>>>1)^((c&1)?0xedb88320:0);}return (c^0xffffffff)>>>0;}
async function main(){
 process.umask(0o077);
 const {values:v}=parseArgs({options:{build:{type:'string'},module:{type:'string'},recovery:{type:'string'},out:{type:'string'},'inspect-only':{type:'boolean'},resume:{type:'string'},'current-build':{type:'string'}}});
 if(!v.build||!v.module||!v.recovery||!v.out)throw Error('Require --build, --module, --recovery manifest, --out');
 const root=path.resolve(__dirname,'../../..'),artifact=fs.realpathSync(path.join(root,'artifacts'));
 const folder=fs.realpathSync(v.build),out=path.resolve(v.out);
 if(!folder.startsWith(artifact+path.sep)||!out.startsWith(artifact+path.sep)||fs.existsSync(out))throw Error('Use a retained build and new ignored output');
 const receipt=JSON.parse(fs.readFileSync(path.join(folder,'receipt.json'))),profile=JSON.parse(fs.readFileSync(v.module));
 const binding=JSON.parse(fs.readFileSync(path.join(folder,'device.json'))),image=fs.readFileSync(path.join(folder,'mhat.bin'));
 if(receipt.status!=='BUILD_COMPLETE_PHYSICAL_UNKNOWN'||receipt.artifact_class!=='PTOB_MHAT_RC'||sha(image)!==receipt.application_sha256)throw Error('Build/receipt mismatch');
 if(profile.platform!=='b5som'||binding.particle_id!==profile.device_id||!/^[0-9a-f]{24}$/.test(profile.device_id))throw Error('Exact enrolled module required');
 for(const [name,hash] of Object.entries(receipt.artifacts)){if(sha(fs.readFileSync(path.join(folder,name)))!==hash)throw Error('Bound artifact changed: '+name);}
 if(image.readUInt32LE(0x200)!==0x30000||image.readUInt32LE(0x204)+4!==0x30000+image.length||image.readUInt16LE(0x20c)!==25||image[0x20e]!==3||0x30000+image.length>0xb4000||crc32(image.subarray(0,-4))!==image.readUInt32BE(image.length-4))throw Error('Unsafe module bounds/header/CRC');
 const recoveryPath=path.resolve(v.recovery),recovery=JSON.parse(fs.readFileSync(recoveryPath));
 const row=recovery.assets.find(a=>a.file==='b5som-system-part1@5.8.2.bin');
 if(!row)throw Error('Require matching official recovery system module');
 const stock=fs.readFileSync(path.join(path.dirname(recoveryPath),row.file));
 if(sha(stock)!==row.sha256||crc32(stock.subarray(0,-4))!==stock.readUInt32BE(stock.length-4)||stock.readUInt16LE(0x20c)!==25)throw Error('Recovery integrity/platform failure');
 let current;
 if(v['current-build']){
   const dir=fs.realpathSync(v['current-build']);
   if(!dir.startsWith(artifact+path.sep))throw Error('Current image must be a retained private build');
   const cr=JSON.parse(fs.readFileSync(path.join(dir,'receipt.json'))),cb=JSON.parse(fs.readFileSync(path.join(dir,'device.json')));
   current=fs.readFileSync(path.join(dir,'mhat.bin'));
   if(cr.status!=='BUILD_COMPLETE_PHYSICAL_UNKNOWN'||cr.artifact_class!=='PTOB_MHAT_RC'||cb.particle_id!==profile.device_id||sha(current)!==cr.application_sha256)throw Error('Current image binding/integrity failure');
 }
 const report={schema:1,utc:new Date().toISOString(),device_id:profile.device_id,build:folder,application_sha256:sha(image),application_bytes:image.length,recovery_manifest:recoveryPath,recovery_sha256:sha(stock),write_start:0x30000,write_end_exclusive:0x30000+image.length,authorized_window_end:0xb4000,external_flash_read:false,full_flash_backup:false,protection_changed:false,firmware_written:false,steps:[]};
 const save=()=>fs.writeFileSync(out,JSON.stringify(report,null,2)+'\n',{mode:0o600});
 fs.mkdirSync(path.dirname(out),{recursive:true,mode:0o700});
 if(!fs.realpathSync(path.dirname(out)).startsWith(artifact+path.sep))throw Error('Output symlink escape');
 let device;
 try{
  const lib=path.join(artifact,'pi5/particle-tools/node_modules/particle-usb');
  if(JSON.parse(fs.readFileSync(path.join(lib,'package.json'))).version!=='4.3.1')throw Error('Unexpected USB library');
  const usb=require(lib);usb.config({requestTimeout:15000});
  device=await usb.openDeviceById(profile.device_id,{includeDfu:true});
  if(device.id!==profile.device_id||device.type!=='b5som')throw Error('Live exact identity/platform mismatch');
  report.steps.push({operation:'exact_identity',dfu:device.isInDfuMode,firmware_version:device.firmwareVersion});save();
  if(v['inspect-only']){report.status='IDENTITY_ONLY_NO_RESET_OR_WRITE';save();return;}
  if(!device.isInDfuMode){if(device.firmwareVersion!=='5.8.2')throw Error('Stock recovery preflight requires installed Device OS 5.8.2');await device.enterDfuMode({timeout:20000});await device.close({timeout:3000});device=await usb.openDeviceById(profile.device_id,{includeDfu:true});}
  if(!device.isInDfuMode||device.id!==profile.device_id||device.type!=='b5som')throw Error('Exact DFU identity required');
  report.steps.push({operation:'enter_dfu',success:true});save();
  const read=async(start,size)=>{const b=await device.readOverDfu({altSetting:0,startAddr:start,size});if(b.length!==size)throw Error('Short DFU read');return b;};
  const regions=[{start:0,size:0x30000},{start:0xb4000,size:0x4c000}];
  if(v.resume){
    const previous=JSON.parse(fs.readFileSync(v.resume));
    if(previous.device_id!==profile.device_id||previous.application_sha256!==sha(image)||!previous.firmware_written||
       !Array.isArray(previous.protected_before)||previous.protected_before.length!==2||
       !previous.protected_before.every((p,i)=>p.start===regions[i].start&&p.size===regions[i].size&&/^[a-f0-9]{64}$/.test(p.sha256)))throw Error('Prior write/protected comparison binding required');
    report.previous_attempt=path.resolve(v.resume);report.protected_before=previous.protected_before;
    report.steps.push({operation:'resume_readback_only_no_rewrite',success:true});save();
  }else{
    if(current){
      const installed=await read(0x30000,current.length);
      if(!installed.equals(current))throw Error('Actual installed application differs from the bound current image');
      report.installed_before_sha256=sha(installed);
      report.steps.push({operation:'installed_current_image_match',success:true});
    }else{
      const prefix=await read(0x30000,0x218);
      if(!prefix.equals(stock.subarray(0,0x218)))throw Error('Installed system header does not match the verified recovery module');
      const tail=await read(0x30000+stock.length-40,40);
      if(!tail.equals(stock.subarray(-40)))throw Error('Installed module footer/CRC does not match recovery');
      report.steps.push({operation:'installed_stock_header_footer_match',success:true});
    }
    report.protected_before=[];
    for(const region of regions)report.protected_before.push({...region,sha256:sha(await read(region.start,region.size))});
    save();
    report.write_attempted=true;save();
    await device.writeOverDfu(image,{altSetting:0,startAddr:0x30000,noErase:false,leave:false});
    report.firmware_written=true;report.steps.push({operation:'bounded_system_write',success:true});save();
    // Pinned particle-usb 4.3.1 leaves download in dfuDNLOAD-IDLE. Its upload
    // path sends CLRSTATUS for any non-IDLE state, which this bootloader stalls
    // unless already in dfuERROR. Standard DFU_ABORT returns transfer state to
    // IDLE without changing flash, protection or executing the application.
    await device._dfu.abortToIdle();
    report.steps.push({operation:'standard_dfu_abort_to_idle',success:true});save();
    // Reopen a fresh DFU session between download and upload. Keep failures;
    // a verification failure never automatically causes a second write.
    await device.close({processPendingRequests:false,timeout:3000});
    device=await usb.openDeviceById(profile.device_id,{includeDfu:true});
    if(!device.isInDfuMode||device.id!==profile.device_id)throw Error('Lost exact DFU identity before readback');
  }
  const actual=await read(0x30000,image.length);
  report.actual_readback_sha256=sha(actual);
  if(!actual.equals(image))throw Error('Actual new-image readback mismatch; retained in DFU');
  report.protected_after=[];
  for(const region of regions)report.protected_after.push({...region,sha256:sha(await read(region.start,region.size))});
  if(JSON.stringify(report.protected_before)!==JSON.stringify(report.protected_after))throw Error('Protected-region mismatch; retained in DFU');
  report.steps.push({operation:'actual_readback_and_protected_comparison',success:true});save();
  await device.reset();report.steps.push({operation:'leave_dfu',success:true});report.status='WRITTEN_VERIFIED_STARTUP_PENDING';save();
 }catch(e){report.status='FAILED';report.error=e.message;save();throw e;}
 finally{if(device)await device.close({processPendingRequests:false,timeout:3000});}
 console.log(JSON.stringify({status:report.status,report:out,bytes:report.application_bytes}));
}
main().catch(e=>{console.error(e.message);process.exitCode=1});

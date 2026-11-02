#!/usr/bin/env node
// Copyright (c) 2026 Jonathan Yantis. MIT. Read-only exact-device USB inventory.
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const { parseArgs } = require('node:util');

async function main() {
  process.umask(0o077);
  const { values } = parseArgs({ options: {
    id: { type: 'string' }, out: { type: 'string' }, library: { type: 'string' },
    connection: { type: 'string' }
  } });
  const privateProfile = values.connection || path.join(os.homedir(), '.config/ptob/mhat.json');
  const deviceId = values.id || JSON.parse(fs.readFileSync(privateProfile, 'utf8')).device_id;
  if (!/^[0-9a-f]{24}$/.test(deviceId || '')) throw new Error('Supply a verified 24-hex module ID');
  const root = path.resolve(__dirname, '../../..');
  const artifactRoot = fs.realpathSync(path.join(root, 'artifacts'));
  const stamp = new Date().toISOString().replace(/[:.]/g, '-');
  const out = path.resolve(values.out || path.join(artifactRoot, 'pi5/reports', `mhat-${stamp}.json`));
  if (!out.startsWith(artifactRoot + path.sep) || fs.existsSync(out)) throw new Error('Choose a new output under ignored artifacts/');
  fs.mkdirSync(path.dirname(out), { recursive: true, mode: 0o700 });
  if (!fs.realpathSync(path.dirname(out)).startsWith(artifactRoot + path.sep)) throw new Error('Refusing linked output outside artifacts/');
  const library = path.resolve(values.library || path.join(artifactRoot, 'pi5/particle-tools/node_modules/particle-usb'));
  const version = JSON.parse(fs.readFileSync(path.join(library, 'package.json'), 'utf8')).version;
  if (version !== '4.3.1') throw new Error('Use the inspected particle-usb 4.3.1 dependency');
  const usb = require(library);
  usb.config({ requestTimeout: 10000 });
  let device;
  try {
    device = await usb.openDeviceById(deviceId);
    if (device.id !== deviceId || device.type !== 'b5som') throw new Error('Module identity/platform mismatch');
    const report = {
      schema: 1, utc: new Date().toISOString(), device_id: device.id,
      platform: device.type, platform_id: device.platformId,
      firmware_version: device.firmwareVersion, dfu: device.isInDfuMode,
      particle_usb_version: version, operation: 'read-only identification',
      source_sha256: crypto.createHash('sha256').update(fs.readFileSync(__filename)).digest('hex'),
      firmware_written: false, raw_flash_read: false, cloud_api_used: false
    };
    if (!device.isInDfuMode) {
      for (const [field, query] of [
        ['mode', () => device.getDeviceMode({ timeout: 10000 })],
        ['modules', () => device.getFirmwareModuleInfo({ timeout: 10000 })]
      ]) {
        try { report[field] = await query(); }
        catch (error) { report[field] = { unavailable: error.message }; }
      }
    }
    report.status = report.dfu || [report.mode, report.modules].some(value => value?.unavailable)
      ? 'partial' : 'complete';
    fs.writeFileSync(out, JSON.stringify(report, null, 2) + '\n', { flag: 'wx', mode: 0o600 });
    console.log(JSON.stringify({ device_id: report.device_id, platform: report.platform,
      firmware_version: report.firmware_version, mode: report.mode, status: report.status,
      module_count: Array.isArray(report.modules) ? report.modules.length : null, report: out }, null, 2));
    if (report.status === 'partial') process.exitCode = 2;
  } finally {
    if (device) await device.close();
  }
}

main().catch(error => { console.error(error.message); process.exitCode = 1; });

#!/usr/bin/env node
// Patch the Tuya MiniApp IDE so it can build Ray projects under Wine.
//
// The IDE builds by typing `npx ray build ...` into a PowerShell pty, and
// Wine's powershell.exe is a stub, so nothing ever runs. This replaces the pty
// call in rayBuild() (@ark/miniapp-compiler) with a child_process.spawn of the
// project's Ray CLI using the Windows node.exe from the prefix PATH.
//
// Usage: node patch-ide.mjs [IDE install dir]
// Idempotent; the original is kept next to it as index.js.orig.
import { copyFileSync, existsSync, readFileSync, writeFileSync } from 'node:fs';
import { homedir, userInfo } from 'node:os';
import { join } from 'node:path';

const prefix = process.env.WINEPREFIX || join(homedir(), '.wine-tuya');
const ide =
  process.argv[2] ||
  join(prefix, 'drive_c/users', userInfo().username, 'AppData/Local/Programs/Tuya MiniApp IDE');
const file = join(ide, 'resources/app/node_modules/@ark/miniapp-compiler/dist/electron-main/index.js');
const MARK = 'MicroESP patch';

if (!existsSync(file)) {
  console.error(`not found: ${file}`);
  process.exit(1);
}
const src = readFileSync(file, 'utf8');
if (src.includes(MARK)) {
  console.log('already patched');
  process.exit(0);
}

const re =
  /command = "cd "\.concat\(cwd, " ; npx ray build --target tuya --output '"\)\.concat\(outDir, "'; exit"\);(\s*)return \[(\s*)4,(\s*)this\.pty\.spawn\(\{\s*cwd: cwd,\s*command: command,\s*alive: false,\s*ondata: onData\s*\}\)/;
const m = src.match(re);
if (!m) {
  console.error('rayBuild() pattern not found: this IDE version differs from 0.10.9, patch by hand');
  process.exit(1);
}
const ci = m[1].replace(/^\n/, '');
const ind = m[3].replace(/^\n/, '');
const patched = `// ${MARK}: under Wine the PowerShell pty is a stub, so run the
${ci}// project's Ray CLI with the Windows node.exe from PATH instead.
${ci}command = null;${m[1]}return [${m[2]}4,${m[3]}new Promise(function(resolve) {
${ind}    var cp = require('child_process').spawn('node', [
${ind}        require('path').join(cwd, 'node_modules', '@ray-js', 'cli', 'bin', 'ray'),
${ind}        'build', '--target', 'tuya', '--output', outDir
${ind}    ], { cwd: cwd });
${ind}    var out = function(d) { try { onData && onData(String(d)); } catch (e) {} console.log('[ray] ' + String(d)); };
${ind}    cp.stdout.on('data', out);
${ind}    cp.stderr.on('data', out);
${ind}    cp.on('error', function(e) { out('ray spawn error: ' + e); resolve(); });
${ind}    cp.on('close', function(code) { out('ray exit ' + code); resolve(); });
${ind}})`;

if (!existsSync(`${file}.orig`)) copyFileSync(file, `${file}.orig`);
writeFileSync(file, src.replace(re, patched));
console.log(`patched ${file}`);

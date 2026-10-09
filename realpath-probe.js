// realpath-probe.js - what fs.realpath does in the current process (run it inside the AppContainer).
// With Claude Code's binary:  set BUN_OPTIONS=--preload C:/ccrepro/realpath-probe.js  then run claude.exe
// (the probe runs before Claude Code and exits). With Bun itself:  bun C:\ccrepro\realpath-probe.js
const fs = process.getBuiltinModule('node:fs');
const dir = process.cwd();
const tryIt = (label, fn) => {
  try {
    console.log(`${label.padEnd(28)} ok    ${fn()}`);
  } catch (e) {
    console.log(`${label.padEnd(28)} FAIL  ${e.code} ${e.message}`);
  }
};
console.log(`Bun ${typeof Bun === 'object' ? Bun.version : '-'}, cwd ${dir}`);
tryIt('statSync(cwd)', () => (fs.statSync(dir).isDirectory() ? 'directory' : 'not a directory'));
tryIt('realpathSync(cwd)', () => fs.realpathSync(dir));
tryIt('realpathSync.native(cwd)', () => fs.realpathSync.native(dir));
fs.promises.realpath(dir).then(
  (r) => console.log(`${'promises.realpath(cwd)'.padEnd(28)} ok    ${r}`),
  (e) => console.log(`${'promises.realpath(cwd)'.padEnd(28)} FAIL  ${e.code} ${e.message}`),
).finally(() => process.exit(0));

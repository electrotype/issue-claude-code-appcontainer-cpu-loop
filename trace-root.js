// trace-root.js - log every fs.promises stat/realpath of a DRIVE ROOT, with its JS stack, to
// C:\ccrepro\work\root-trace.log (first 50 calls + a count every second).
// Use: set BUN_OPTIONS=--preload C:/ccrepro/trace-root.js  then start claude.exe as usual.
const fs = process.getBuiltinModule('node:fs');
const LOG = 'C:\\ccrepro\\work\\root-trace.log';
const isRoot = (p) => /^[A-Za-z]:[\\/]?$/.test(String(p));
let n = 0;
for (const name of ['stat', 'lstat', 'realpath']) {
  const orig = fs.promises[name];
  fs.promises[name] = function (p, ...rest) {
    if (isRoot(p) && ++n <= 50) {
      const stack = (new Error().stack || '').split('\n').slice(2, 7).map((l) => l.trim()).join(' | ');
      fs.appendFileSync(LOG, `${new Date().toISOString()} fs.promises.${name}(${JSON.stringify(String(p))}) :: ${stack}\n`);
    }
    return orig.call(this, p, ...rest);
  };
}
setInterval(() => fs.appendFileSync(LOG, `${new Date().toISOString()} drive-root calls so far: ${n}\n`), 1000).unref();
delete process.env.BUN_OPTIONS;

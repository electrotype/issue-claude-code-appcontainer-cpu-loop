# Claude Code spins at 100% CPU forever when run inside a Windows AppContainer (bundled chokidar re-adds the drive root in a loop)

## Environment

- Claude Code 2.1.286 (also seen on 2.1.285), native Windows binary `claude.exe` (Bun 1.4.3, revision eecfd55d)
- Windows 11 Pro 10.0.26200, x64
- Claude Code started inside a Windows **AppContainer** (a standard Windows sandbox; we run the CLI in one so it cannot read the rest of the user's disk)

## Summary

Inside an AppContainer, `claude.exe` uses about one full CPU core (up to two on a bigger session) from the moment its first screen shows, and never stops, even while idle. Started the same way without the AppContainer, it sits at 0%.

The loop is in the copy of **chokidar** bundled in Claude Code: `FSWatcher.add()` keeps re-adding `C:/` about **6,000 times per second**.

## Steps to reproduce

`acrun.c` in this repo (about 60 lines) starts any program inside an AppContainer with no capabilities. It inherits the console and the current folder. Build it from a "Developer PowerShell for VS":

```powershell
cl /nologo acrun.c userenv.lib
```

Set up a folder the AppContainer may read (Windows lets an AppContainer read only folders that grant `ALL APPLICATION PACKAGES`, SID `S-1-15-2-1`). No admin rights are needed:

```powershell
mkdir C:\ccrepro\work, C:\ccrepro\config
copy acrun.exe, trace-root.js C:\ccrepro\
copy <path to claude.exe> C:\ccrepro\          # e.g. node_modules\@anthropic-ai\claude-code-win32-x64\claude.exe
icacls C:\ccrepro        /grant "*S-1-15-2-1:(OI)(CI)(RX)"
icacls C:\ccrepro\work   /grant "*S-1-15-2-1:(OI)(CI)(M)"
icacls C:\ccrepro\config /grant "*S-1-15-2-1:(OI)(CI)(M)"
'{"hasCompletedOnboarding": true}' | Set-Content C:\ccrepro\config\.claude.json   # skips first-run onboarding (the AppContainer has no network)
```

Run it in Windows Terminal:

```powershell
cd C:\ccrepro\work
$env:CLAUDE_CONFIG_DIR = 'C:\ccrepro\config'
C:\ccrepro\acrun.exe C:\ccrepro\claude.exe
```

Claude Code shows the "Do you trust this folder?" screen. Leave it there and watch `claude.exe` in Task Manager.

- **Actual:** `claude.exe` uses about 90-100% of one core and keeps doing so.
- **Expected:** about 0% while idle. That's what you get with the same command without `acrun.exe` (`C:\ccrepro\claude.exe`).

Measured: uncaged 0%, in the AppContainer 93% of one core, at the same screen after 20 s.

### Trace

`trace-root.js` in this repo logs every `fs.promises.stat/lstat/realpath` of a drive root (first 50 calls with their JS stack, then a count every second) to `C:\ccrepro\work\root-trace.log`:

```powershell
$env:BUN_OPTIONS = '--preload C:/ccrepro/trace-root.js'
C:\ccrepro\acrun.exe C:\ccrepro\claude.exe
```

Result:

```
fs.promises.stat("C:/") :: at _addToNodeFs (B:/~BUN/root/chunk-6np1zevt.js:11:11473) | at <anonymous> (...:11:16814) | at map (native:1:11) | at add (B:/~BUN/root/chunk-6np1zevt.js:11:16758) | at <anonymous> (...:11:16929)
...
drive-root calls so far: 126839
drive-root calls so far: 132992
drive-root calls so far: 138951
```

## Cause

1. In an AppContainer, `realpath` fails with `EPERM` for **every** path (a Bun / Windows issue, reported separately to Bun: <link to the Bun issue>). In short, Windows' `GetFinalPathNameByHandleW` with DOS volume names needs the volume mount manager, which no AppContainer may open. On top of that, the drive root itself (`C:\`) is not readable by an AppContainer by default.
2. chokidar `add()` → `_addToNodeFs(path)` stats the path and, for a directory with `followSymlinks` on (the default), calls `fs.promises.realpath`. That throws `EPERM`; `_handleError()` swallows it (the watchers use `ignorePermissionErrors: true`, and `ENOENT` is always swallowed), so `_addToNodeFs` returns the path.
3. `add()` then does `this.add(dirname(n), basename(...))` for every returned path. **It never checks that `dirname(n) !== n`.** Each parent fails the same way, so the walk climbs to `C:/`. There `dirname("C:/") === "C:/"`, so `add("C:/")` repeats forever (an async loop through Bun's thread pool, which is why several threads are busy).

Outside an AppContainer the walk stops at the first existing parent, because its `realpath` succeeds. So the bug normally stays hidden. But any of Claude Code's directory watchers triggers it inside the sandbox (settings, keybindings, the project folder waiting for `.claude`, skills...).

The same loop was reported for ImDisk RAM disks in #76608 (closed by the stale bot): there too `GetFinalPathNameByHandleW` fails on every path of the volume, and the watcher spins at the drive root.

## Suggested fix

- In the bundled chokidar's `add()`, stop climbing when `dirname(n) === n` (the drive root or `/`). One check fixes the infinite loop on every platform. chokidar 5.0.0, the latest on npm as of 2026-10-09, still has this code (`results.forEach((item) => { if (item) this.add(sp.dirname(item), ...) })`). chokidar's `main` branch has a rewritten `add()` without that retry, so a release from there (or a one-line patch to the bundled copy) should fix it.
- Optionally treat `EPERM` / `EACCES` from `realpath` like a missing link target instead of climbing.

## Workaround we use meanwhile

A `--preload` (via `BUN_OPTIONS`) that makes `fs.realpath*` fall back to a JS walk (lstat each part, follow links, tolerate an unreadable drive root) when the native call returns `EPERM`. With it, idle CPU is 0%. We'd much rather not inject code into Claude Code's process, hence this report.

## Related, separate (same AppContainer setup, worked around with env vars)

- The PowerShell tool's `cmd.exe` launcher (`cmd /d /s /c ""chcp.com" 65001 >nul & powershell ... > "<out>" 2>&1"`) makes every command report "Exit code 5". `cmd.exe` crashes with 0xC0000005 on any redirect or pipe inside an AppContainer, after the work is done. Worked around with `CLAUDE_CODE_DISABLE_WINDOWS_SHELL_LAUNCHER=1`.
- The IDE detection `cmd /c "tasklist | findstr ..."` (from the "Connect Claude to your IDE" tip's `isRelevant()`, at every start, and from `/ide`) never finishes inside an AppContainer. Because the result is not cached on a pending call, each check adds another stuck child. `CLAUDE_CODE_AUTO_CONNECT_IDE=false` does not gate it, and neither does `spinnerTipsEnabled: false` (the startup tip prewarm ignores it). Worked around with `FORCE_CODE_TERMINAL=1`.

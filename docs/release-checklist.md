# Release checklist

Use this before tagging a GitHub Release.

## Pre-flight

- [ ] `main` builds clean locally: `.\scripts\build.ps1 -Config Release`
- [ ] Unit tests: `.\scripts\test.ps1 -Config Release`
- [ ] CLI smoke: `.\scripts\smoke.ps1 -Config Release` (SOFT admin cases OK)
- [ ] Manual once: `wintune tui --theme compact` and `wintune tray`
- [ ] `ARCH.txt` / `VERSION.txt` / `CHANNEL.txt` present after packaging
- [ ] `wintune version` matches the intended tag (set via `-DWINTUNE_VERSION=`)
- [ ] Explorer shows the WinTune icon on `wintune.exe` (Properties → Details)

## Package

```powershell
.\scripts\release.ps1 -Version 0.2.0
```

Produces:

- `dist/WinTune-<ver>-win-x64.zip` + `.sha256`
- `dist/WinTune-<ver>-win-arm64.zip` + `.sha256`

Optional installer (requires [Inno Setup](https://jrsoftware.org/isinfo.php)):

```powershell
.\scripts\package.ps1 -Config Release -Arch x64 -Version 0.2.0 -Configure -Build -Zip
iscc /DMyAppVersion=0.2.0 /DMyAppArch=x64 scripts\installer\wintune.iss
```

## Tag + publish

```bash
git tag -a v0.2.0 -m "WinTune 0.2.0"
git push origin v0.2.0
```

Confirm the Release workflow uploaded both ZIPs and checksums.

## Release notes template

```markdown
## WinTune x.y.z

Native Windows performance diagnostics.

### Highlights
- …

### Downloads
| File | Arch | Notes |
|------|------|-------|
| `WinTune-x.y.z-win-x64.zip` | Intel/AMD | Most PCs |
| `WinTune-x.y.z-win-arm64.zip` | ARM64 | Snapdragon / WoA only |

Check `ARCH.txt` inside the ZIP before running.

### Verify
```powershell
Get-FileHash .\WinTune-x.y.z-win-x64.zip -Algorithm SHA256
# Compare to WinTune-x.y.z-win-x64.zip.sha256
```

### Install
1. Extract ZIP to e.g. `C:\Tools\WinTune`
2. Optional: run `Add-To-Path.cmd`
3. `wintune doctor` or double-click `Launch-WinTune.cmd`
```

## Signing (optional)

See [`signing.md`](signing.md). Unsigned open-source builds are fine; SmartScreen
may warn on first run until reputation builds.

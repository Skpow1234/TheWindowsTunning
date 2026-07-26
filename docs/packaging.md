# Packaging and deployment

WinTune ships as a **portable native executable** — no .NET, Node, or Electron
runtime. Phase 18 adds install layouts, multi-arch builds, and optional logging
for service/automation use.

---

## Quick start (developers)

**PowerShell:**

```powershell
# Build + install layout under dist/
.\scripts\build.ps1 -Config Release
.\scripts\package.ps1 -Config Release -Arch x64

# Same + ZIP + SHA-256
.\scripts\package.ps1 -Config Release -Arch x64 -Zip

# Full release (x64 + ARM64 ZIPs)
.\scripts\release.ps1 -Version 0.1.2
```

**Git Bash / WSL** (same flags; do not run `.ps1` with bash):

```bash
./scripts/build -Config Release
./scripts/package -Config Release -Arch x64 -Zip
./scripts/release -Version 0.1.2
```

See [`shells.md`](shells.md) for the full PowerShell / CMD / Git Bash / WSL matrix.
Output directory example:

```text
dist/WinTune-0.1.2-win-x64/
  wintune.exe          # icon + VERSIONINFO + manifest embedded
  README.md
  LICENSE
  VERSION.txt
  CHANNEL.txt
  ARCH.txt
  QUICKSTART.txt
  Launch-WinTune.cmd
  Add-To-Path.cmd
  Start-Tray.cmd
  ...
```

---

## Phase 21 resources

| File | Role |
|------|------|
| `resources/wintune.ico` | Explorer / tray icon |
| `resources/wintune.manifest` | `asInvoker`, DPI, long paths, Win10/11 |
| `resources/wintune.rc.in` | VERSIONINFO + icon + manifest (CMake fills version) |
| `pack/Add-To-Path.ps1` | User PATH helper for portable installs |
| `scripts/installer/wintune.iss` | Optional Inno Setup installer |

`wintune version` / `WT_VERSION_STRING` should match `-DWINTUNE_VERSION=` used at
configure time (release CI sets this from the tag).

Checklist: [`release-checklist.md`](release-checklist.md). Signing:
[`signing.md`](signing.md).

---

## `cmake --install`

After configuring and building:

```powershell
cmake --install build --config Release --prefix dist/my-prefix
```

CMake installs:

- `wintune.exe`
- `README.md`, `LICENSE` (when present)
- Files from `pack/` (launchers, quick start)

`scripts/package.ps1` wraps install and adds `VERSION.txt` / `CHANNEL.txt`.

---

## Architectures

| Script flag | CMake `-A` | Build dir default |
|-------------|------------|-------------------|
| `-Arch x64` | `x64` | `build` |
| `-Arch arm64` | `ARM64` | `build-arm64` |

```powershell
.\scripts\cmake-configure.ps1 -BuildDir build-arm64 -Arch arm64
cmake --build build-arm64 --config Release
.\scripts\package.ps1 -BuildDir build-arm64 -Arch arm64 -Zip
```

CI builds **x64** on `windows-latest` and **ARM64** on `windows-11-vs2026-arm`
(native ARM64 + VS 2026). Cross-compiling ARM64 on an x64 PC requires the MSVC
ARM64 workload in Visual Studio Installer.

Each ZIP includes `VERSION.txt`, `CHANNEL.txt`, and `ARCH.txt` so the architecture
is obvious before double-clicking `wintune.exe`.

GitHub Releases upload both ZIPs on `v*` tags.

Verify architecture:

```powershell
wintune version
# Arch: x64   or   Arch: arm64
```

---

## Release channels

| Channel | Tag example | `CHANNEL.txt` |
|---------|-------------|---------------|
| **stable** | `v0.1.2` | `stable` |
| **beta** | `v0.2.0-beta.1` | `beta` (auto-detected from `-beta` in version) |

Stable tags are the default for production. Beta tags are pre-releases for testers;
same ZIP layout, different version string.

---

## `--log-file` (service / automation)

Append diagnostic logs to a file (mirrored to stderr when logging is enabled):

```powershell
wintune scan --verbose --log-file C:\ProgramData\WinTune\logs\scan.log
wintune service run   # future: service may default log path under ProgramData
```

Parent directories are created when possible. Combine with `--verbose` or
`--debug` for more detail.

---

## Code signing (optional)

See [`signing.md`](signing.md). Unsigned portable ZIPs remain the default
open-source artifact.

---

## Optional Inno Setup installer

Requires [Inno Setup](https://jrsoftware.org/isinfo.php) (`iscc` on PATH):

```powershell
.\scripts\package.ps1 -Config Release -Arch x64 -Version 0.2.0 -Configure -Build -Zip
iscc /DMyAppVersion=0.2.0 /DMyAppArch=x64 scripts\installer\wintune.iss
```

Not built by default CI. Prefer the portable ZIP for most users.

---

## GitHub Releases

Tag push (`v*`) runs `.github/workflows/release.yml`:

- Builds x64 + ARM64
- Uploads `WinTune-<ver>-win-x64.zip` and `WinTune-<ver>-win-arm64.zip`
- Publishes SHA-256 sidecar files

Manual trigger: **Actions → Release → Run workflow**.

---

## Related docs

- [`docs/ci.md`](ci.md) — CI matrix
- [`docs/release-checklist.md`](release-checklist.md) — pre-tag checklist
- [`docs/signing.md`](signing.md) — Authenticode guidance
- [`docs/fleet.md`](fleet.md) — automation over SSH
- [`README.md`](../README.md) — portable ZIP usage for end users

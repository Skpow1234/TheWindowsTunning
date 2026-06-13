# Packaging and deployment

WinTune ships as a **portable native executable** — no .NET, Node, or Electron
runtime. Phase 18 adds install layouts, multi-arch builds, and optional logging
for service/automation use.

---

## Quick start (developers)

```powershell
# Build + install layout under dist/
.\scripts\build.ps1 -Config Release
.\scripts\package.ps1 -Config Release -Arch x64

# Same + ZIP + SHA-256
.\scripts\package.ps1 -Config Release -Arch x64 -Zip

# Full release (x64 + ARM64 ZIPs)
.\scripts\release.ps1 -Version 0.1.2
```

Output directory example:

```text
dist/WinTune-0.1.2-win-x64/
  wintune.exe
  README.md
  LICENSE
  VERSION.txt
  CHANNEL.txt
  QUICKSTART.txt
  Launch-WinTune.cmd
  ...
```

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

CI builds **x64** (lint + tests) and **ARM64** (Release smoke test) on every push.
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

WinTune is open source; signing is **optional** but recommended for enterprise
deployment.

1. Obtain a code-signing certificate (EV or standard Authenticode).
2. Sign after build:

```powershell
signtool sign /fd SHA256 /a /tr http://timestamp.digicert.com /td SHA256 `
  /d "WinTune" dist\WinTune-0.1.2-win-x64\wintune.exe
```

3. Verify:

```powershell
signtool verify /pa wintune.exe
```

**Guidelines:**

- Sign **Release** builds only.
- Timestamp so signatures remain valid after cert expiry.
- Do not commit private keys or `.pfx` files to the repository.
- GitHub Actions: store cert in secrets and sign in the release workflow when ready.

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
- [`docs/fleet.md`](fleet.md) — automation over SSH
- [`README.md`](../README.md) — portable ZIP usage for end users

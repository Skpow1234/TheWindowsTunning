# Release channels

WinTune ships **portable ZIP** as the primary artifact on every channel.
An optional Inno Setup installer may accompany releases when CI can build it.

## Channels

| Channel | How selected | `CHANNEL.txt` | Typical consumers |
| ------- | ------------ | ------------- | ----------------- |
| **stable** | Tag `vX.Y.Z` (no `-beta`) | `stable` | End users, winget later |
| **beta** | Tag contains `-beta` (e.g. `v0.2.0-beta.1`) | `beta` | Testers, early adopters |

`scripts/package.ps1 -Channel` and the Release workflow set `CHANNEL.txt` in the
stage directory. The Release job auto-detects `beta` from the version string.

## Artifacts (per arch)

| Artifact | Required | Notes |
| -------- | -------- | ----- |
| `WinTune-<ver>-win-<arch>.zip` | Yes | Primary |
| `*.zip.sha256` | Yes | SHA-256 of the ZIP |
| `WinTune-<ver>-win-<arch>-setup.exe` | Optional | Inno Setup (Phase 53) |
| `*-setup.exe.sha256` | With setup | SHA-256 of the installer |
| `SIGNING.txt` | In stage dir | `signed` or `unsigned` |

## Building locally

```powershell
# Portable ZIP (stable)
.\scripts\release.ps1 -Version 0.2.0 -Arch x64

# Beta channel
.\scripts\package.ps1 -Config Release -Arch x64 -Version 0.2.0-beta.1 `
  -Channel beta -Configure -Build -Zip

# Optional installer (requires Inno Setup on PATH)
.\scripts\installer.ps1 -Version 0.2.0 -Arch x64 -SkipIfMissingInno
```

## GitHub Releases

- Tag push `v*` or **Actions → Release → Run workflow**.
- x64 + ARM64 ZIPs always publish.
- Setup.exe uploads when the installer job succeeds (Inno installed on the
  runner); missing Inno skips the installer without failing the release.

## Chocolatey / winget

- **winget:** draft manifests under `packaging/winget/` — see [`winget.md`](winget.md).
- **Chocolatey:** not packaged yet; portable ZIP + optional installer cover
  distribution. A future `packaging/chocolatey/` package may wrap the same
  GitHub Release assets — no adware, no silent auto-start.

## Safety

- Installer PATH and desktop-icon tasks are **unchecked** by default.
- No bundled adware, telemetry uploaders, or forced tray autostart.
- Prefer Authenticode for wide installer distribution — see [`signing.md`](signing.md).

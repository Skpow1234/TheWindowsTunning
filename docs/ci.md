# CI/CD

WinTune uses [GitHub Actions](https://github.com/features/actions) for continuous
integration and release packaging.

## Workflows

| Workflow | File | Trigger |
|----------|------|---------|
| **CI** | `.github/workflows/ci.yml` | Push/PR to `main` |
| **Release** | `.github/workflows/release.yml` | Tag `v*` or manual dispatch |

### CI (lint + build)

On every push and pull request to `main`:

1. Configure with **MSVC `/W4`** and **`/WX`** (warnings as errors).
2. Build **Debug** and **Release** (x64).
3. Unit tests (`wintune_unit_tests`).
4. Smoke test: `wintune version` and `wintune help`.

A separate **ARM64** job runs on **`windows-11-vs2026-arm`** (native ARM64 with
Visual Studio 2026). Cross-compiling ARM64 on `windows-latest` (x64) fails unless
the MSVC ARM64 workload is installed; CI uses a native runner instead.

The job configures `build-arm64` with `-A ARM64`, builds Release, and
smoke-tests `wintune version` (expects `Arch: arm64`).

**Generator:** CI uses `scripts/cmake-configure.ps1`, which discovers installed
Visual Studio via **vswhere** (VS 2026 first, then VS 2022 for local dev). The
ARM64 job pins **CMake 4.3.3** (`lukka/get-cmake`) because the VS 2026 generator
requires CMake 4.2+. If the VS generator still fails on ARM64, configure falls
back to **Ninja** via `Launch-VsDevShell.ps1`.

**Run locally:**

```powershell
.\scripts\lint.ps1
```

### Release

When you push a semver tag:

```bash
git tag v0.1.0
git push origin v0.1.0
```

The release workflow:

1. Builds **Release** x64 on `windows-latest` and **ARM64** on
   `windows-11-vs2026-arm` (parallel package jobs).
2. Creates portable ZIPs:
   - `dist/WinTune-<version>-win-x64.zip`
   - `dist/WinTune-<version>-win-arm64.zip`
   Each includes launchers from `pack/`, `VERSION.txt`, and `CHANNEL.txt`.
3. Writes **SHA-256** checksum files for both ZIPs.
4. Publishes assets to **GitHub Releases**.

**Manual release (no tag yet):**

1. Open **Actions → Release → Run workflow**.
2. Enter a version such as `0.1.0`.

**Run locally:**

```powershell
.\scripts\release.ps1 -Version 0.1.0
```

## Version string

The version shown by `wintune version` and JSON output comes from CMake
(`WINTUNE_VERSION`). Release builds set it from the git tag. Local developer
builds default to `0.1.0` unless overridden:

```powershell
cmake -S . -B build -DWINTUNE_VERSION=0.2.0-dev
```

Keep `CMakeLists.txt` default, release tags, and `docs/roadmap.md` in sync when
bumping versions.

## Future (Phase 21)

Not yet in CI:

- Authenticode signing
- winget/Chocolatey manifests
- Windows version resource / icon embedding
- Installer (MSI/Inno Setup)

See [roadmap.md](roadmap.md) Phase 21.

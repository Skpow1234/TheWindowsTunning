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

A separate **ARM64** job configures `build-arm64` with `-A ARM64`, builds
Release, and smoke-tests `wintune version` (expects `Arch: arm64`).

This is the primary lint gate for v1. The project does not require clang-format
or third-party static analyzers yet.

**Generator:** CI uses `scripts/cmake-configure.ps1`, which tries **Visual Studio
18 2026** (GitHub `windows-latest` since June 2026), then **Visual Studio 17
2022** (local dev), then CMake’s default generator.

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

1. Builds **Release** x64 + **ARM64** via `scripts/release.ps1`.
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
- ARM64 release matrix
- winget/Chocolatey manifests
- Windows version resource / icon embedding
- Installer (MSI/Inno Setup)

See [roadmap.md](roadmap.md) Phase 21.

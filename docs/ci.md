# CI/CD

WinTune uses [GitHub Actions](https://github.com/features/actions) for continuous
integration and release packaging.

## Workflows

| Workflow | File | Trigger |
|----------|------|---------|
| **CI** | `.github/workflows/ci.yml` | Push/PR to `main`; tags `v*`; manual dispatch |
| **Release** | `.github/workflows/release.yml` | Tag `v*` or manual dispatch |

### CI jobs

| Job | Runner | What it does |
|-----|--------|--------------|
| **Lint and build (Windows x64)** | `windows-latest` | `/WX` configure, Debug+Release build, `test.ps1`, `smoke.ps1 -SkipSlow` |
| **Build (Windows ARM64)** | `windows-11-vs2026-arm` | `/WX` configure, Release build, `test.ps1`, full `smoke.ps1 -SkipSlow` |
| **Package dry-run (x64)** | `windows-latest` | `package.ps1 -Zip` when tag `v*`, PR labeled `release`, or dispatch with `package_dry_run` |

### Required status check (repo setting)

Make this check **required** on `main` / `master` so link/test breaks cannot merge:

1. GitHub → **Settings → Branches → Branch protection rules** (or Rulesets).
2. Require status checks to pass before merging.
3. Add exactly: **`Lint and build (Windows x64)`**  
   (optional: also require **`Build (Windows ARM64)`** if that runner is always available).

The job `name:` in `ci.yml` must stay stable — renaming it breaks the required check.

### Caching

Both architecture jobs cache the CMake build directory (`actions/cache`):

- **Cache hit:** configure without `-Clean` (incremental rebuild).
- **Cache miss:** configure with `-Clean` (fresh tree).

Cache keys hash `CMakeLists.txt`, `src/**`, `resources/**`, `tests/**`, and
`scripts/cmake-configure.ps1`.

### Smoke coverage (provenance / scoring)

`scripts/smoke.ps1` asserts JSON field presence on array items (soft-pass if the
array is empty on a clean host):

- `top --json` → `publisher`, `product_name`, `location`, `unusual_location`, …
- `startup --json` → same + `impact_score`, `impact_confidence`
- `tasks list --json` → `impact_score`, `impact_confidence`
- `services --json` → `publisher`, `origin`, `signature`, `location`

### Package dry-run

```text
# PR: add label "release"
# or: Actions → CI → Run workflow → package_dry_run = true
# or: push tag v*
```

Runs `scripts/package.ps1 -Configure -Build -Zip`, verifies ZIP + checksum +
stage layout, uploads artifacts (7-day retention). Full GitHub Release publishing
stays in `release.yml`.

### CI (lint + build) detail

On every push and pull request to `main`:

1. Configure with **MSVC `/W4`** and **`/WX`** (warnings as errors).
2. Build **Debug** and **Release** (x64).
3. Unit tests via `scripts/test.ps1` (`wintune_tests_all` + `ctest`), including
   `test_file_identity` and `test_impact_score`.
4. Full CLI smoke (`scripts/smoke.ps1 -SkipSlow`) — soft (admin) failures OK;
   hard failures fail the job. Tray remains SKIP (interactive).

A separate **ARM64** job runs on **`windows-11-vs2026-arm`** (native ARM64 with
Visual Studio 2026). It builds Release, runs the same unit-test target set, and
runs full smoke against the ARM64 binary.

**Run locally:**

```powershell
.\scripts\lint.ps1
.\scripts\test.ps1 -Config Release
.\scripts\smoke.ps1 -Config Release
```

See [`testing.md`](testing.md).

**Generator:** CI uses `scripts/cmake-configure.ps1`, which discovers installed
Visual Studio via **vswhere** (VS 2026 first, then VS 2022 for local dev). The
ARM64 job pins **CMake 4.3.3** (`lukka/get-cmake`) because the VS 2026 generator
requires CMake 4.2+. If the VS generator still fails on ARM64, configure falls
back to **Ninja** via `Launch-VsDevShell.ps1`.

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
builds default to the `CMakeLists.txt` cache default unless overridden:

```powershell
cmake -S . -B build -DWINTUNE_VERSION=0.2.0-dev
```

Keep `CMakeLists.txt` default, release tags, and `docs/roadmap.md` in sync when
bumping versions.

## Optional (post Phase 21)

Not in default CI yet:

- Authenticode signing (see [`signing.md`](signing.md))
- Publishing the draft winget manifests under `packaging/winget/` to winget-pkgs
- Inno Setup installer build (`scripts/installer/wintune.iss`)

x64 and ARM64 CI run the full `scripts/smoke.ps1` suite (with `-SkipSlow`).
Version resources and icon embedding are part of the normal CMake build
(`resources/`). See [roadmap.md](roadmap.md) Phase 21 and [`winget.md`](winget.md).

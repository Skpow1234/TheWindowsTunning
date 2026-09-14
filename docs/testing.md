# Testing WinTune

WinTune has two layers of tests:

| Layer | What it covers | How to run |
|-------|----------------|------------|
| **Unit tests** | Exit codes, pure logic (no live Windows APIs) | `.\scripts\test.ps1` |
| **CLI smoke tests** | Every command on a real machine | `.\scripts\smoke.ps1` |

---

## Unit tests

```powershell
.\scripts\build.ps1 -Config Release
.\scripts\test.ps1 -Config Release
```

```bash
./scripts/build -Config Release
./scripts/test -Config Release
```

Today this runs:

| Target | Coverage |
|--------|----------|
| `wintune_unit_tests` | Exit-code mapping |
| `test_units` | Byte / duration formatting |
| `test_cli_parser` | `wt_cli_parse_argv`, JSON mode, power tokens |
| `test_json` | Compact JSON writer escaping |
| `test_file_identity` | Path extract, location classify, name strings |
| `test_impact_score` | Deterministic impact scoring bands/thresholds |
| `test_recommendations` | Memory / disk / power threshold IDs (+ startup/impact deps) |

`.\scripts\test.ps1` builds `wintune_tests_all` then runs `ctest`.

Unit-test executables are `EXCLUDE_FROM_ALL` so a plain `cmake --build`
(package path) is not blocked by test `/WX` noise. **Always** build them via
`wintune_tests_all` / `test.ps1`. When a production `.c` file gains new link
deps (e.g. `file_identity`, `wintrust`), update the matching `wt_add_unit_test`
source list and `target_link_libraries` or CI will fail with `LNK2019`.

---

## CLI smoke tests (recommended before a release)

`scripts/smoke.ps1` launches `wintune.exe` for **every public command** and
checks exit codes + basic output shape.

```powershell
.\scripts\build.ps1 -Config Release
.\scripts\smoke.ps1 -Config Release
```

```bash
./scripts/build -Config Release
./scripts/smoke -Config Release
```

Against a packaged binary:

```powershell
.\scripts\smoke.ps1 -Exe .\dist\WinTune-0.1.2-win-x64\wintune.exe
```

### What is asserted

| Category | Commands | Expectation |
|----------|----------|-------------|
| Meta | `help`, `version`, `--help`, `--version` | exit 0, known banners |
| Core scan | `scan`, `doctor`, `recommend`, `report`, `top` (+ `--json` / `--sort`) | exit 0; JSON looks like JSON |
| Inventory | `startup`, `tasks`, `services`, `power`, `blockers`, `rollback list` | exit 0 |
| Privileged / env-sensitive | `updates`, `boot analyze` | exit 0 **or** soft-pass on access denied / missing Diagnostic-Performance channel (common on CI VMs) |
| Service agent | `service status` | exit 0 (installed or not) |
| Mutating (safe) | `apply` without id / without `--yes`; bogus `power --set`; bad rollback id | non-zero or cancel; **no silent change** |
| Interactive | `tui` without a console | exit “not supported” |
| Manual | `tray` | skipped (would hang the runner) |

Soft failures (`SOFT`) mean “behaved acceptably for a non-admin / restricted
session.” They do **not** fail the suite. Hard failures (`FAIL`) do.

### CI

| Job | What runs |
|-----|-----------|
| **Lint and build (Windows x64)** *(required check)* | `/WX` build, `test.ps1`, `smoke.ps1 -SkipSlow` |
| **Build (Windows ARM64)** | Release build, `test.ps1 -BuildDir build-arm64`, full smoke |
| **Package dry-run (x64)** | On `v*` tags, PR label `release`, or dispatch — see [`ci.md`](ci.md) |

x64 smoke:

```powershell
.\scripts\smoke.ps1 -Config Release -SkipSlow
```

`-SkipSlow` tightens timeouts; coverage is the same. ARM64 CI now runs the same
smoke suite against the ARM64 binary. See [`ci.md`](ci.md) for caching,
Dependabot, and how to set the required status check.

### What smoke does **not** do

- Does not install/uninstall the WinTune Windows Service.
- Does not run `power --set` / `apply --yes` (those change the machine).
- Does not keep `tui` / `tray` open (interactive).
- Does not prove pixel-perfect TUI layout (use a real terminal for that).

### Manual checks (after smoke)

```powershell
.\build\Release\wintune.exe tui --theme compact
.\build\Release\wintune.exe tray
# In tray: Show status, Run doctor, Exit
.\build\Release\wintune.exe power --set balanced   # only if you intend to change it
```

---

## Suggested release gate

1. `.\scripts\lint.ps1`
2. `.\scripts\test.ps1 -Config Release`
3. `.\scripts\smoke.ps1 -Config Release`
4. Manual: `tui` + `tray` once on a desktop session
5. Tag / `.\scripts\release.ps1 -Version x.y.z`

---

## Adding a new command

When you add a CLI command:

1. Add a case to `scripts/smoke.ps1` (read-only if possible).
2. Prefer `--json` coverage when the command supports it.
3. For mutating commands, assert the **without `--yes`** path refuses or prompts.
4. Update this doc’s table.

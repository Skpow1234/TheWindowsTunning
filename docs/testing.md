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

Today this runs `wintune_unit_tests` (stable exit-code mapping). More unit
targets can be added under `tests/` and wired in `CMakeLists.txt`.

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
| Privileged reads | `updates`, `boot analyze` | exit 0 **or** soft-pass on access denied |
| Service agent | `service status` | exit 0 (installed or not) |
| Mutating (safe) | `apply` without id / without `--yes`; bogus `power --set`; bad rollback id | non-zero or cancel; **no silent change** |
| Interactive | `tui` without a console | exit “not supported” |
| Manual | `tray` | skipped (would hang the runner) |

Soft failures (`SOFT`) mean “behaved acceptably for a non-admin / restricted
session.” They do **not** fail the suite. Hard failures (`FAIL`) do.

### CI

The x64 CI job runs:

```powershell
.\scripts\smoke.ps1 -Config Release -SkipSlow
```

`-SkipSlow` tightens timeouts; coverage is the same. ARM64 CI still only checks
`wintune version` (arch string).

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

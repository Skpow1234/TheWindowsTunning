# WinTune JSON schema

WinTune JSON output is designed for scripting, SSH automation, and small-scale
fleet diagnostics. The **schema version** is independent of the tool release
version.

## Version fields

Every JSON document includes:

| Field | Example | Meaning |
|-------|---------|---------|
| `schema_version` | `"1.0.0"` | Stable schema contract (semver) |
| `version` | `"0.1.0"` | WinTune executable release |
| `timestamp_utc` | `"2026-06-11T12:00:00Z"` | Document generation time (UTC) |

When `schema_version` changes, see [`json-changelog.md`](json-changelog.md).

## Common envelope

Most command documents share a session block:

```json
{
  "schema_version": "1.0.0",
  "version": "0.1.0",
  "timestamp_utc": "2026-06-11T12:00:00Z",
  "session": {
    "interactive": false,
    "remote": true,
    "elevated": false
  }
}
```

Scan/doctor documents add `system`, `metrics`, `processes`, `recommendations`,
etc. Partial failures use `"available": false` on sub-objects rather than
invalid JSON.

## Error documents (`--json-errors`)

When a command fails and `--json-errors` is set, WinTune emits a machine-readable
error object:

```json
{
  "schema_version": "1.0.0",
  "version": "0.1.0",
  "timestamp_utc": "2026-06-11T12:00:00Z",
  "ok": false,
  "session": { "interactive": false, "remote": true },
  "error": {
    "code": "access_denied",
    "exit_code": 11,
    "exit_name": "access_denied",
    "command": "apply",
    "message": "This action requires administrator privileges."
  }
}
```

- With **`--json`**, the error document goes to **stdout** (so parsers always
  read stdout).
- With **`--json-errors` alone**, the error document goes to **stderr**.

## Output modes

| Flag | Effect |
|------|--------|
| `--json` | Structured document on stdout |
| `--compact-json` | Minified JSON (no indentation) |
| `--ndjson` | One complete JSON document per line (implies compact) |
| `--json-errors` | Emit error JSON on failure |

Example compact scan:

```bash
wintune scan --json --compact-json > scan.min.json
```

Example NDJSON process sampling:

```bash
wintune top --watch --json --ndjson --duration 10000 --interval 2000
```

## Exit codes

Stable exit codes map to failure classes for automation:

| Code | Name | Typical cause |
|------|------|----------------|
| 0 | `ok` | Success |
| 2 | `usage` | Invalid arguments |
| 10 | `cancelled` | User declined confirmation |
| 11 | `access_denied` | Admin required |
| 12 | `not_found` | Missing resource (service, rollback id, …) |
| 13 | `not_supported` | Blocked or advisory action |
| 14 | `timeout` | Operation timed out |
| 20 | `error` | General failure (Win32, PDH, OOM, …) |
| 21 | `not_implemented` | Command recognized but not built yet |

Inspect `$?` / `%ERRORLEVEL%` in scripts. Pair with `--json-errors` when stdout
must always be parseable JSON.

## Stability policy

- **Patch** schema bumps (1.0.x): additive fields only; existing keys unchanged.
- **Minor** bumps (1.x.0): new optional sections; old fields retained when
  possible.
- **Major** bumps (x.0.0): breaking key renames or type changes — documented in
  the changelog with migration notes.

WinTune does not upload JSON anywhere by default. Reports stay local unless you
explicitly copy them.

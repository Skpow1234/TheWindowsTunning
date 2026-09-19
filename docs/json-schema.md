# WinTune JSON schema

WinTune JSON output is designed for scripting, SSH automation, and small-scale
fleet diagnostics. The **schema version** is independent of the tool release
version.

Current default: **`2.0.0`** (Phase 47). Core fields from **`1.0.0`** remain
stable; see [`json-changelog.md`](json-changelog.md).

## Version fields

Every versioned JSON document includes:

| Field | Example | Meaning |
|-------|---------|---------|
| `schema_version` | `"2.0.0"` | Active schema contract (semver) |
| `schema_compat_min` | `"1.0.0"` | Oldest major consumers can still treat as compatible (**v2+ only**) |
| `document` | `"scan"` | Document discriminator (**v2+ only**) |
| `version` | `"0.2.0"` | WinTune executable release |
| `timestamp_utc` | `"2026-09-19T12:00:00Z"` | Document generation time (UTC) |

When `schema_version` changes, see [`json-changelog.md`](json-changelog.md).

### Pinning schema 1 (deprecation window)

Scripts that match **exactly** `"schema_version":"1.0.0"` can pin during the
deprecation window:

```bash
wintune scan --json --schema-version 1
```

Accepted values: `1`, `1.0`, `1.0.0`, `2`, `2.0`, `2.0.0` (default **2**).

With `--schema-version 1`, WinTune emits `schema_version` as `1.0.0` and
**omits** `schema_compat_min` and `document`. All other payload keys are
unchanged (additive fields from later phases may still appear).

**Plan:** remove the schema-1 pin after the next major tool release that
needs a true breaking rename. Prefer checking the **major** component
(`schema_version` starting with `1.` or `2.`) instead of an exact string.

## Common envelope (schema 2)

```json
{
  "schema_version": "2.0.0",
  "schema_compat_min": "1.0.0",
  "document": "scan",
  "version": "0.2.0",
  "timestamp_utc": "2026-09-19T12:00:00Z",
  "session": {
    "interactive": false,
    "remote": true,
    "elevated": false
  }
}
```

### `document` values

| Value | Command / emitter |
|-------|-------------------|
| `scan` | `scan`, `doctor` (scan body) |
| `processes` | `top` |
| `recommendations` | `recommend` |
| `startup` | `startup` |
| `tasks` | `tasks` |
| `services` | `services` |
| `boot` | `boot` |
| `updates` | `updates` |
| `blockers` | `blockers` |
| `error` | `--json-errors` failure document |
| `fleet_pack_result` | `fleet pack --json` |

Scan/doctor documents add `system`, `metrics`/`cpu`/`memory`/`disk`/`gpu`,
`processes`, `recommendations`, etc. Partial failures use `"available": false`
on sub-objects rather than invalid JSON.

## Error documents (`--json-errors`)

```json
{
  "schema_version": "2.0.0",
  "schema_compat_min": "1.0.0",
  "document": "error",
  "version": "0.2.0",
  "timestamp_utc": "2026-09-19T12:00:00Z",
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
| `--schema-version 1\|2` | Pin envelope major (default 2) |

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

- **Patch** schema bumps (x.y.z): additive fields only; existing keys unchanged.
- **Minor** bumps (x.y.0): new optional sections; old fields retained when
  possible.
- **Major** bumps (x.0.0): breaking key renames or type changes — documented in
  the changelog with migration notes. Schema **2.0.0** is a **documentation /
  envelope** major: payload keys from 1.0 remain; new envelope keys are
  additive and gated behind the default major (or omitted with
  `--schema-version 1`).

WinTune does not upload JSON anywhere by default. Reports stay local unless you
explicitly copy them.

## Compatibility checklist for fleet scripts

1. Prefer `schema_version` **major** checks over exact equality.
2. Ignore unknown keys (forward compatible).
3. Treat missing optional sections as unavailable (same as `"available": false`).
4. Use `--schema-version 1` only while migrating exact-match parsers.
5. Fleet pack ZIP `manifest.json` uses its **own** pack `schema_version`
   (`1.0`) — independent of the CLI JSON API schema. See [`fleet.md`](fleet.md).

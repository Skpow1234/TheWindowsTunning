# JSON schema changelog

## 2.0.0 — 2026-09-19 (Phase 47)

Schema major bump for fleet/automation clarity after Phases 18–46.

**Added (envelope)**

- Default `schema_version` is now **`2.0.0`**.
- `schema_compat_min`: `"1.0.0"` — core 1.0 keys remain readable.
- `document`: discriminator (`scan`, `processes`, `recommendations`, …).
- CLI **`--schema-version 1|2`** pin (deprecation window for exact `1.0.0`
  matchers). With `1`, envelope omits `schema_compat_min` / `document` and
  reports `schema_version` as `1.0.0`.

**Documented as stable (additive since 1.0.0; already emitted)**

- Process: per-process disk/net rates, publisher/signature/`identity`.
- Scan: multi-sample `scan` confidence block, disk throughput/queue, GPU,
  thermal/power budget fields where collected.
- Recommendations: `confidence_percent`, `confidence_basis`.
- Boot: history, cold/warm kind, waterfall/components.
- Updates / blockers / tasks / apps JSON surfaces.
- Fleet pack result JSON (`document` / `kind`: `fleet_pack_result`).

**Unchanged**

- Existing 1.0 key names and types for `system`, `cpu`, `memory`, `disk`
  volumes, `session`, recommendations `id`/`title`/`severity`/`risk`, and
  error documents’ `error.code` / `exit_code`.
- Exit codes and `--json` / `--compact-json` / `--ndjson` / `--json-errors`.

**Migration**

```bash
# Default (schema 2)
wintune scan --json

# Temporary pin while updating exact-match parsers
wintune scan --json --schema-version 1
```

- Branch on major: accept `1.x` and `2.x` if you only use 1.0 core fields.
- Do **not** require exact `"1.0.0"` going forward; the pin may be removed in a
  later release once the deprecation window closes.
- Ignore unknown keys.

---

## 1.0.0 — 2026-06-11 (Phase 17)

Initial versioned schema for fleet/automation hardening.

**Added**

- `schema_version` on all JSON documents and error payloads.
- `session` block on scan/top/recommendation envelopes (already present; now
  documented as stable).
- Machine-readable **error documents** via `--json-errors`.
- **`--compact-json`** and **`--ndjson`** output modes.
- Stable **exit codes** per failure class (see [`json-schema.md`](json-schema.md)).

**Unchanged from prior releases**

- Existing keys (`version`, `timestamp_utc`, `metrics`, `recommendations`, …)
  retain the same meaning.
- Partial failure encoding (`"available": false`) unchanged.

**Migration**

- Scripts may ignore `schema_version` until they need to branch on new fields.
- Prefer checking `schema_version` major version before assuming new keys exist.

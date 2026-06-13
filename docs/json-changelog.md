# JSON schema changelog

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

# Fleet and automation

WinTune is **local-first**: no cloud account, no telemetry upload, no central
server. Phase 17 adds stable exit codes, versioned JSON, and batch-friendly
output for homelab and small IT teams running diagnostics over SSH.

See also: [`json-schema.md`](json-schema.md), [`ssh.md`](ssh.md).

---

## Quick patterns

### Scan many hosts

```bash
mkdir -p reports
for h in host1 host2 host3; do
  ssh "admin@${h}" "wintune scan --json --compact-json" > "reports/${h}.json" \
    || echo "FAIL ${h} exit=$?" >&2
done
```

### Doctor with error JSON

When a host may fail (service missing, access denied), use `--json-errors` so
stdout is always valid JSON on failure:

```bash
ssh "admin@${h}" "wintune doctor --json --json-errors" > "reports/${h}-doctor.json"
code=$?
if [ "$code" -ne 0 ]; then
  jq -r '.error.message // "unknown"' "reports/${h}-doctor.json"
fi
```

### Exit code branching

```bash
ssh user@host "wintune apply WT-POWER-001 --yes"
case $? in
  0)  echo "ok" ;;
  10) echo "cancelled" ;;
  11) echo "needs elevation" ;;
  13) echo "blocked or advisory" ;;
  *)  echo "other failure" ;;
esac
```

| Code | Meaning |
|------|---------|
| 0 | Success |
| 2 | Usage error |
| 10 | Cancelled |
| 11 | Access denied |
| 12 | Not found |
| 13 | Not supported / blocked |
| 14 | Timeout |
| 20 | General error |
| 21 | Not implemented |

---

## PowerShell loop

```powershell
$hosts = @("srv1", "srv2", "srv3")
New-Item -ItemType Directory -Force -Path reports | Out-Null

foreach ($h in $hosts) {
    $out = "reports\$h-scan.json"
    ssh "admin@$h" "wintune scan --json --compact-json" | Set-Content -Encoding utf8 $out
    if ($LASTEXITCODE -ne 0) {
        Write-Warning "$h scan failed with exit $LASTEXITCODE"
    }
}
```

---

## Ansible ad-hoc

```yaml
# playbook snippet
- name: WinTune scan each Windows host
  hosts: windows
  gather_facts: false
  tasks:
    - name: Run wintune scan
      ansible.windows.win_shell: >
        wintune scan --json --compact-json --json-errors
      register: wintune_scan
      failed_when: wintune_scan.rc not in [0]
      changed_when: false

    - name: Save report locally
      delegate_to: localhost
      copy:
        content: "{{ wintune_scan.stdout }}"
        dest: "reports/{{ inventory_hostname }}.json"
```

For mutating actions over Ansible, always pass `--yes` and expect exit `11` when
the task is not elevated.

---

## NDJSON sampling

Stream process snapshots for short monitoring windows (non-interactive SSH):

```bash
ssh user@host \
  "wintune top --watch --json --ndjson --duration 30000 --interval 5000" \
  > host-top.ndjson
```

Each line is a complete JSON document with `schema_version`, `processes`, and
`session`.

---

## Service-based fleet scans

When the WinTune service is installed on managed hosts:

```bash
ssh user@host "wintune scan --json --via-service"
ssh user@host "wintune apply WT-POWER-001 --via-service --yes"
```

See [`service.md`](service.md).

---

## What WinTune never does in fleet mode

- No default cloud upload or telemetry
- No hidden background agent unless the user installs the Windows Service
- No UAC bypass — elevation must come from the SSH session or service

Collect reports locally; aggregate with your own tools (`jq`, PowerShell,
Elastic, etc.).

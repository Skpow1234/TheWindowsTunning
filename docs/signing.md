# Authenticode signing (optional)

WinTune does **not** require code signing to build or use. For wider
distribution (winget, enterprise, fewer SmartScreen prompts), you may sign
`wintune.exe` and installers with an Authenticode certificate.

Portable unsigned ZIPs remain the **default** open-source release artifact.
Never commit certificates or private keys to this repository.

## Local / CI script

Use [`scripts/sign.ps1`](../scripts/sign.ps1):

```powershell
# Soft verify (unsigned OK — default OSS path)
.\scripts\sign.ps1 -Path .\dist\WinTune-0.2.0-win-x64\wintune.exe -Verify -AllowUnsigned

# Sign when a PFX is available
.\scripts\sign.ps1 -Path .\dist\...\wintune.exe -Sign -PfxPath cert.pfx

# Package with optional sign (credentials via env) then ZIP
.\scripts\package.ps1 -Config Release -Arch x64 -Version 0.2.0 -Zip -SignIfConfigured
```

`package.ps1` always writes `SIGNING.txt` (`signed` or `unsigned`) into the stage
directory so operators can see what shipped.

## Certificate options

1. Obtain a code-signing certificate (OV/EV) from a public CA, or use your org’s
   internal PKI for private fleets.
2. Prefer **Azure Trusted Signing** / cloud HSM over exporting private keys to
   developer machines when possible.
3. Sign **after** the Release build / `cmake --install`, **before** zipping
   (`-SignIfConfigured` on `package.ps1` does this).
4. Verify:

```powershell
signtool verify /pa .\dist\WinTune-0.2.0-win-x64\wintune.exe
# or
.\scripts\sign.ps1 -Path .\dist\...\wintune.exe -Verify -RequireSigned
```

## GitHub Actions (Release workflow)

[`.github/workflows/release.yml`](../.github/workflows/release.yml) packages with
`-SignIfConfigured` and then runs Authenticode verify:

| Name | Type | Purpose |
| ---- | ---- | ------- |
| `WINTUNE_SIGN_ENABLED` | Repository **variable** | Set to `true` to require a valid signature after packaging |
| `WINTUNE_SIGN_PFX_BASE64` | Repository **secret** | Base64-encoded `.pfx` (optional) |
| `WINTUNE_SIGN_PASSWORD` | Repository **secret** | PFX password (optional) |
| `WINTUNE_SIGN_TIMESTAMP` | Repository **variable** | Timestamp URL (default DigiCert) |

Without secrets, packaging stays unsigned and verify soft-passes
(`-AllowUnsigned`). With secrets present (or `WINTUNE_SIGN_ENABLED=true` plus
PFX), the binary is signed before ZIP and verify must succeed.

### Azure Trusted Signing / OIDC

Preferred for production: use Azure Trusted Signing with OIDC federation
instead of a long-lived PFX in GitHub secrets. Typical pattern:

1. Register an Azure Trusted Signing account and certificate profile.
2. Federate the GitHub repo via Azure AD app + OIDC.
3. Add a Release job step that invokes the Trusted Signing action / `Invoke-TrustedSigning`
   on the staged `wintune.exe` **before** ZIP (or replace the PFX path in
   `sign.ps1` with your org’s wrapper).
4. Keep `signtool verify /pa` (or `sign.ps1 -RequireSigned`) as the release gate.

Exact Azure action versions and secret names vary by tenant; document them in
your private ops runbook. Do not put client secrets in the public repo.

## CI dry-run

The CI `package-dry-run` job expects `SIGNING.txt` and runs
`sign.ps1 -Verify -AllowUnsigned` so unsigned builds stay green while signed
builds are still checked when credentials exist.

## Safety

- Never commit `.pfx`, `.p12`, or private keys.
- Never log PFX passwords.
- Temp PFX material written from `WINTUNE_SIGN_PFX_BASE64` is deleted after sign.

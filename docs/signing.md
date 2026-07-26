# Authenticode signing (optional)

WinTune does **not** require code signing to build or use. For wider
distribution (winget, enterprise, fewer SmartScreen prompts), you may sign
`wintune.exe` and installers with an Authenticode certificate.

## Guidance

1. Obtain a code-signing certificate (OV/EV) from a public CA, or use your org’s
   internal PKI for private fleets.
2. Prefer **Azure Trusted Signing** / cloud HSM over exporting private keys to
   developer machines when possible.
3. Sign **after** the Release build, **before** zipping:

```powershell
# Example with signtool (Windows SDK)
signtool sign /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 `
  /a .\dist\WinTune-0.2.0-win-x64\wintune.exe
signtool verify /pa .\dist\WinTune-0.2.0-win-x64\wintune.exe
```

4. Re-run packaging/ZIP **after** signing so the published artifact contains the
   signed binary.
5. Never commit certificates or private keys to this repository.

## CI note

Signing secrets (if any) should live in GitHub Actions secrets / OIDC and are
out of scope for the default open-source workflow. Portable unsigned ZIPs remain
the primary release artifact.

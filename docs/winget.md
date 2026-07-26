# Winget (optional)

Draft manifests live in [`packaging/winget/`](../packaging/winget/). They are
**not** published automatically. Submit them to
[microsoft/winget-pkgs](https://github.com/microsoft/winget-pkgs) after a stable
GitHub Release.

## Package identity

| Field | Value |
|-------|-------|
| PackageIdentifier | `WinTune.WinTune` |
| Command alias | `wintune` |
| Installer | Portable ZIP (`NestedInstallerType: portable`) |

## Before each submission

1. Tag and publish a release (`scripts/release.ps1` / Release workflow).
2. Copy SHA-256 from `WinTune-<ver>-win-x64.zip.sha256` (and arm64) into
   `WinTune.WinTune.installer.yaml` (replace `REPLACE_WITH_SHA256_*`).
3. Bump `PackageVersion` in all three YAML files to match the tag (no `v` prefix).
4. Validate locally if you have the winget client tools:

```powershell
winget validate .\packaging\winget\
```

5. Open a PR under `manifests/w/WinTune/WinTune/<version>/` in `winget-pkgs`.

## Notes

- Only `wintune.exe` is registered as the portable command. Other ZIP files
  (`Launch-WinTune.cmd`, docs) remain in the extract folder but are not aliased.
- Unsigned builds may show SmartScreen warnings; see [`signing.md`](signing.md).

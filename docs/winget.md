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
   Open the download URL in a browser before writing it into the manifest.
   A git tag is not a release asset: `v0.3.0` exists, but
   `.../releases/download/v0.3.0/WinTune-0.3.0-win-x64.zip` returns 404 until
   the Release workflow uploads the zip. The x64 job publishes on its own;
   a failed ARM64 runner no longer blocks that upload.
2. Copy SHA-256 from `WinTune-<ver>-win-x64.zip.sha256` into
   `WinTune.WinTune.installer.yaml`. Add the arm64 installer only when that
   zip is actually on the release. A second 404 fails the whole PR.
3. Bump `PackageVersion` in all three YAML files to match the tag (no `v` prefix).
   The binary must be built from a commit that statically links the CRT
   (`MSVC_RUNTIME_LIBRARY=MultiThreaded`). An older zip imports
   `VCRUNTIME140.dll`. WinGet then runs `wintune.exe` on a clean image and
   records exit `-1073741515` (`STATUS_DLL_NOT_FOUND`). Do not declare a
   `Microsoft.VCRedist` dependency for that; ship the static binary instead.
4. Validate locally if you have the winget client tools:

```powershell
winget validate .\packaging\winget\
```

5. Open a PR under `manifests/w/WinTune/WinTune/<version>/` in `winget-pkgs`.

## Notes

- Only `wintune.exe` is registered as the portable command. Other ZIP files
  (`Launch-WinTune.cmd`, docs) remain in the extract folder but are not aliased.
- Unsigned builds may show SmartScreen warnings; see [`signing.md`](signing.md).

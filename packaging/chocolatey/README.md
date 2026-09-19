# Chocolatey (future)

WinTune does **not** ship a Chocolatey package yet. Distribution today:

1. **Portable ZIP** (primary) — GitHub Releases
2. **Optional Setup.exe** — Inno Setup via `scripts/installer.ps1`
3. **winget drafts** — `packaging/winget/` (see [`docs/winget.md`](../../docs/winget.md))

A future `.nuspec` here should wrap the same GitHub Release assets, with:

- No adware / bundled offers
- No silent tray or service auto-start without explicit consent
- Stable channel packages tracking non-beta tags only

Until then, use the portable ZIP or Setup.exe from Releases.

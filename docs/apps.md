# Apps / uninstall advisor

WinTune helps you **find** installed software that may be worth removing. It
never uninstalls anything.

## Command

```powershell
wintune apps
wintune apps --json
```

## What it does

1. Reads Add/Remove Programs (ARP) uninstall registry keys (HKLM / HKCU,
   including WOW6432Node).
2. Skips Windows system components and update/hotfix style entries.
3. Flags **candidates** when the app correlates with a **high-impact startup**
   entry (login impact leftovers).
4. With `--large`, also flags large third-party installs (≥ ~2 GiB).
5. Prints official removal guidance only.

## What it never does

- Run `UninstallString`
- Call `winget uninstall` for you
- Delete Program Files / AppData
- Disable Microsoft / security products

## Official paths (you run these)

- Settings → Apps → Installed apps
- `winget list` then `winget uninstall "<exact name>"`
- Control Panel → Programs and Features

## Recommendation

When candidates exist, `wintune recommend` / `doctor` may emit `WT-UNINSTALL-001`
(advisory). Follow with `wintune apps`.
